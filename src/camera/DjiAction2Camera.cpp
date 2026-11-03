#include "DjiAction2Camera.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Legacy DUML frame: 55 len ver|len_hi crc8 | sender receiver seq(BE) flags set id | payload | crc16(LE).
// Pairing and 02/02 REC/STOP follow ShutterLink (MIT, https://github.com/rover1312/shutterlink);
// 02/70 record state and 0D/02 battery follow the Action 2 captures documented by RotorREC
// (https://github.com/flosean/RotorREC). Both were verified on Action 2 hardware by their authors.
DjiAction2Camera* DjiAction2Camera::instance=nullptr;
static const NimBLEUUID DJI_SERVICE((uint16_t)0xFFF0);
static const NimBLEUUID DJI_PAIR((uint16_t)0xFFF4);
static const NimBLEUUID DJI_WRITE((uint16_t)0xFFF5);
static constexpr uint8_t APP=0x02,CAMERA=0x01,BATTERY=0x05,WIFI=0x07;
static constexpr uint8_t REC_IDLE=0x01,REC_ACTIVE=0x81,REC_SAVING=0xC1;

DjiAction2Camera::DjiAction2Camera():callbacks(this){instance=this;}
uint8_t DjiAction2Camera::crc8(const uint8_t*d,size_t n){uint8_t c=0x77;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c&1)?(uint8_t)((c>>1)^0x8C):(uint8_t)(c>>1);}return c;}
uint16_t DjiAction2Camera::crc16(const uint8_t*d,size_t n){uint16_t c=0x3692;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c&1)?(uint16_t)((c>>1)^0x8408):(uint16_t)(c>>1);}return c;}
void DjiAction2Camera::log(const char*fmt,...){char b[180];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);Serial.printf("[DJI2 %8lu] %s\n",(unsigned long)millis(),b);}

void DjiAction2Camera::begin(){
    prefs.begin("fpvcam-dji2",false);
    savedAddress=prefs.getString("address","");savedAddressType=prefs.getUChar("type",0);
    if(!NimBLEDevice::init("FPVCineCam32")){camState.status="DJI BLE INIT FAIL";return;}
    NimBLEDevice::setPower(3);
    camState.status="DJI OFFLINE";camState.model="DJI Osmo Action 2";camState.protocolVersion="DJI DUML";
    if(savedAddress.length()){requestedAddress=savedAddress;requestedAddressType=savedAddressType;reconnectWanted=true;nextReconnectMs=millis()+500;}
}

bool DjiAction2Camera::startScan(String&j){
    auto*s=NimBLEDevice::getScan();s->setActiveScan(true);s->setInterval(60);s->setWindow(45);auto r=s->getResults(4000,false);j="[";bool first=true;
    for(int i=0;i<r.getCount();i++){auto*d=r.getDevice(i);const std::string name=d->getName();if(!d->isAdvertisingService(DJI_SERVICE)&&name.find("Action")==std::string::npos)continue;if(!first)j+=',';first=false;String n=name.c_str(),a=d->getAddress().toString().c_str();j+="{\"name\":\""+n+"\",\"address\":\""+a+"\",\"type\":"+String(d->getAddress().getType())+"}";}
    j+="]";s->clearResults();return true;
}
bool DjiAction2Camera::connectTo(const String&a,uint8_t t){if(!a.length()||connectTaskRunning)return false;requestedAddress=a;requestedAddressType=t;connectRequested=true;camState.status="DJI CONNECT QUEUED";return true;}
void DjiAction2Camera::connectTaskThunk(void*a){auto*self=(DjiAction2Camera*)a;String x=self->requestedAddress;uint8_t t=self->requestedAddressType;self->connectRequested=false;self->performConnect(x,t);self->connectTaskRunning=false;vTaskDelete(nullptr);}
void DjiAction2Camera::performConnect(const String&a,uint8_t t){
    authStartMs=millis();if(client&&client->isConnected())client->disconnect();pairChar=writeChar=nullptr;rxLen=0;sessionReady=false;pinPending=false;approvalAckPending=false;camState.connected=false;camState.controlReady=false;camState.ready=false;camState.status="DJI CONNECTING";
    if(!client){client=NimBLEDevice::createClient();if(!client){camState.status="DJI CLIENT FAIL";return;}client->setClientCallbacks(&callbacks,false);client->setConnectTimeout(8000);}
    NimBLEAddress ad(a.c_str(),t);if(!client->connect(ad,true,false,true)){camState.status="DJI CONNECT FAIL";log("BLE connect failed error=%d",client->getLastError());return;}log("BLE connected MTU=%u",(unsigned)client->getMTU());
    auto*svc=client->getService(DJI_SERVICE);if(!svc){camState.status="DJI SERVICE MISSING";client->disconnect();return;}pairChar=svc->getCharacteristic(DJI_PAIR);writeChar=svc->getCharacteristic(DJI_WRITE);
    if(!pairChar||!writeChar){camState.status="DJI CHAR MISSING";client->disconnect();return;}
    // Replies can arrive on either characteristic.
    if(pairChar->canNotify())pairChar->subscribe(true,notifyCb);if(writeChar->canNotify())writeChar->subscribe(true,notifyCb);
    savedAddress=a;savedAddressType=t;prefs.putString("address",a);prefs.putUChar("type",t);camState.connected=true;camState.status="DJI PAIRING";
    // Arm pairing on FFF4, then send the pairing request shortly after from loop().
    const uint8_t arm[2]={0x01,0x00};pairChar->writeValue(arm,sizeof(arm),true);
    lastRxMs=millis();pinAtMs=lastRxMs+200;pinPending=true;
}

bool DjiAction2Camera::sendFrame(uint8_t receiver,uint8_t flags,uint8_t set,uint8_t id,const uint8_t*p,size_t pn,int32_t fixedSeq){
    if(!writeChar||!client||!client->isConnected()||pn>48)return false;uint8_t f[64]{};const size_t total=13+pn;const uint16_t s=fixedSeq>=0?(uint16_t)fixedSeq:seq++;
    f[0]=0x55;f[1]=total&0xFF;f[2]=0x04|((total>>8)&0x03);f[3]=crc8(f,3);f[4]=APP;f[5]=receiver;f[6]=s>>8;f[7]=s&0xFF;f[8]=flags;f[9]=set;f[10]=id;if(pn)memcpy(f+11,p,pn);
    const uint16_t c=crc16(f,11+pn);f[11+pn]=c&0xFF;f[12+pn]=c>>8;
    return writeChar->writeValue(f,total,false);
}
void DjiAction2Camera::sendPairingPin(){
    // Fixed identifier: the camera stores its approval under it, so later connects skip the prompt.
    static const char id[]="001749319286102",token[]="osmo";uint8_t p[2+sizeof(id)+sizeof(token)];size_t n=0;
    p[n++]=strlen(id);memcpy(p+n,id,strlen(id));n+=strlen(id);p[n++]=strlen(token);memcpy(p+n,token,strlen(token));n+=strlen(token);
    camState.lastCommand="DJI PAIR";camState.lastWrite=sendFrame(WIFI,0x40,0x07,0x45,p,n)?"sent":"failed";log("PAIR request %s",camState.lastWrite.c_str());
}

void DjiAction2Camera::notifyCb(NimBLERemoteCharacteristic*,uint8_t*d,size_t n,bool){if(instance)instance->handleNotify(d,n);}
void DjiAction2Camera::dropRx(size_t n){if(n>=rxLen){rxLen=0;return;}memmove(rxBuf,rxBuf+n,rxLen-n);rxLen-=n;}
void DjiAction2Camera::handleNotify(const uint8_t*d,size_t n){
    camState.incomingPackets++;lastRxMs=millis();
    if(rxLen+n>sizeof(rxBuf))rxLen=0;if(n>sizeof(rxBuf))return;
    memcpy(rxBuf+rxLen,d,n);rxLen+=n;
    // Frames may be split across or concatenated within notifications.
    while(rxLen){
        size_t start=0;while(start<rxLen&&rxBuf[start]!=0x55)start++;dropRx(start);
        if(rxLen<4)return;
        if(crc8(rxBuf,3)!=rxBuf[3]){dropRx(1);continue;}
        const size_t total=rxBuf[1]|((rxBuf[2]&0x03)<<8);if(total<13||total>sizeof(rxBuf)){dropRx(1);continue;}
        if(rxLen<total)return;
        if(crc16(rxBuf,total-2)!=(uint16_t)(rxBuf[total-2]|(rxBuf[total-1]<<8))){dropRx(1);continue;}
        handleFrame(rxBuf,total);dropRx(total);
    }
}
void DjiAction2Camera::handleFrame(const uint8_t*f,size_t len){
    const uint8_t flags=f[8],set=f[9],id=f[10];const uint8_t*p=f+11;const size_t n=len-13;char b[40];snprintf(b,sizeof(b),"%02X/%02X flags=%02X len=%u",set,id,flags,(unsigned)n);camState.lastIncoming=b;
    if(flags==0xC0&&set==0x07&&id==0x45&&n>=2){if(p[1]==0x01){sessionReady=true;log("PAIR already approved");}else if(p[1]==0x02){camState.status="APPROVE ON CAMERA";log("PAIR approve on camera screen");}return;}
    if(flags==0x40&&set==0x07&&id==0x46&&n>=1&&p[0]==0x01){approvalSeq=(uint16_t)(f[6]<<8|f[7]);approvalAckPending=true;return;}
    if(flags==0xC0&&set==0x02&&id==0x02&&n>=1){char w[24];snprintf(w,sizeof(w),"DJI RECORD ACK %02X",p[0]);camState.lastWrite=p[0]==0?"DJI RECORD ACK OK":w;return;}
    if(flags==0xC0&&set==0x02&&id==0x70&&n>=2&&p[0]==0x00){
        if(p[1]!=REC_IDLE&&p[1]!=REC_ACTIVE&&p[1]!=REC_SAVING)return;
        camState.recording=p[1]==REC_ACTIVE;camState.status=p[1]==REC_ACTIVE?"DJI REC":(p[1]==REC_SAVING?"DJI SAVING":"DJI READY");return;
    }
    if(flags==0xC0&&set==0x0D&&id==0x02&&n>=22&&p[0]==0x00&&p[21]<=100){camState.batteryPercent=p[21];return;}
}

bool DjiAction2Camera::setRecording(bool on){if(!camState.controlReady)return false;const uint8_t p=on?1:0;camState.lastCommand=on?"REC":"STOP";camState.lastWrite=sendFrame(CAMERA,0x40,0x02,0x02,&p,1)?"sent":"failed";return camState.lastWrite=="sent";}
void DjiAction2Camera::loop(){
    const uint32_t now=millis();
    if(pinPending&&(int32_t)(now-pinAtMs)>=0){pinPending=false;sendPairingPin();}
    if(approvalAckPending){approvalAckPending=false;const uint8_t ok=0x01;sendFrame(WIFI,0xC0,0x07,0x46,&ok,1,approvalSeq);sessionReady=true;log("PAIR approved on camera");}
    if(sessionReady&&!camState.controlReady){camState.paired=camState.ready=camState.controlReady=true;camState.status="DJI READY";lastPollMs=now;}
    if(camState.connected&&!camState.controlReady&&now-authStartMs>=30000){log("PAIR timeout");client->disconnect();}
    if(camState.controlReady){
        if(now-lastRxMs>=15000){log("No camera traffic for 15 s, reconnecting");client->disconnect();}
        else if(now-lastPollMs>=500){
            // Poll record state every 500 ms, keep-alive every second, battery every 2 s.
            lastPollMs=now;pollCycle++;
            if(pollCycle%2==0){const uint8_t k[2]={0x01,0x01};sendFrame(0xF0,0x40,0x00,0x2B,k,sizeof(k));}
            const uint8_t q=0x01;sendFrame(CAMERA,0x20,0x02,0x70,&q,1);
            if(pollCycle%4==0){const uint8_t z[4]={0,0,0,0};sendFrame(BATTERY,0x20,0x0D,0x02,z,sizeof(z));}
        }
    }
    if(connectRequested&&!connectTaskRunning){connectTaskRunning=true;if(xTaskCreate(connectTaskThunk,"dji2-connect",8192,this,1,nullptr)!=pdPASS){connectTaskRunning=false;connectRequested=false;camState.status="DJI TASK FAIL";}}
    if(reconnectWanted&&!connectTaskRunning&&!connectRequested&&(int32_t)(now-nextReconnectMs)>=0&&savedAddress.length()){reconnectWanted=false;connectTo(savedAddress,savedAddressType);}
}
void DjiAction2Camera::disconnect(){reconnectWanted=false;if(client&&client->isConnected())client->disconnect();}
void DjiAction2Camera::forgetPairing(){savedAddress="";disconnect();prefs.remove("address");prefs.remove("type");camState=CameraState{};camState.status="DJI OFFLINE";camState.model="DJI Osmo Action 2";camState.protocolVersion="DJI DUML";log("DJI saved target cleared");}
void DjiAction2Camera::ClientCallbacks::onConnect(NimBLEClient*){o->camState.connected=true;o->camState.status="DJI BLE LINK";}
void DjiAction2Camera::ClientCallbacks::onDisconnect(NimBLEClient*,int r){o->sessionReady=false;o->pinPending=false;o->approvalAckPending=false;o->rxLen=0;o->camState.connected=false;o->camState.controlReady=false;o->camState.ready=false;o->camState.recording=false;o->camState.status="DJI OFFLINE";o->log("BLE disconnected reason=%d",r);if(o->savedAddress.length()){o->reconnectWanted=true;o->nextReconnectMs=millis()+3000;}}
