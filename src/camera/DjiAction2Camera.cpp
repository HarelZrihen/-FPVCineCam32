#include "DjiAction2Camera.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Legacy DUML frame: 55 len ver|len_hi crc8 | sender receiver seq(BE) flags set id | payload | crc16(LE).
// Pairing and 02/02 REC/STOP follow ShutterLink (MIT, https://github.com/rover1312/shutterlink);
// 02/70 record state and 0D/02 battery follow the Action 2 captures documented by RotorREC
// (https://github.com/flosean/RotorREC). Both were verified on Action 2 hardware by their authors.
// Osmo Nano: same framing, pairing and 02/02 REC/STOP. After pairing the Nano needs a short
// session-init sequence, then pushes camera state (02/80), battery (0D/02) and subscribed named
// parameters (00/99 from the parameter module 0x28). Command layout as observed and documented
// by ShutterBridge (https://github.com/YLabs-FPV/ShutterBridge); implemented independently here.
DjiAction2Camera* DjiAction2Camera::instance=nullptr;
static const NimBLEUUID DJI_SERVICE((uint16_t)0xFFF0);
static const NimBLEUUID DJI_PAIR((uint16_t)0xFFF4);
static const NimBLEUUID DJI_WRITE((uint16_t)0xFFF5);
static constexpr uint8_t APP=0x02,CAMERA=0x01,BATTERY=0x05,WIFI=0x07,PARAMS=0x28;
static constexpr uint8_t DJI_MFR_NANO=0x19;  // model byte in DJI manufacturer data (AA 08 <model>)
static const char NANO_VIDEO_PARAM[]="cam_video_param_v2";  // [resolution idx][fps idx]...
static uint16_t le16(const uint8_t*p){return (uint16_t)(p[0]|(p[1]<<8));}
static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static const char* nanoResolution(uint8_t i){
    switch(i){case 10:return "1080P";case 12:return "1080P 4:3";case 16:return "4K";case 45:return "2.7K";case 66:return "1080P 9:16";
    case 67:return "2.7K 9:16";case 95:return "2.7K 4:3";case 103:return "4K 4:3";case 105:return "1080P 1:1";case 106:return "2160P 1:1";
    case 107:return "3K 1:1";case 108:return "3K 9:16";case 109:return "4K 9:16";case 125:return "4K OPEN";default:return "";}
}
static int nanoFps(uint8_t i){
    switch(i){case 1:return 24;case 2:return 25;case 3:return 30;case 4:return 48;case 5:return 50;case 6:return 60;case 7:return 120;
    case 8:return 240;case 10:return 100;case 11:return 96;case 19:return 200;case 29:return 15;default:return 0;}
}
static constexpr uint8_t REC_IDLE=0x01,REC_ACTIVE=0x81,REC_SAVING=0xC1;

DjiAction2Camera::DjiAction2Camera(bool osmoNano):callbacks(this),nano(osmoNano){instance=this;}
uint8_t DjiAction2Camera::crc8(const uint8_t*d,size_t n){uint8_t c=0x77;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c&1)?(uint8_t)((c>>1)^0x8C):(uint8_t)(c>>1);}return c;}
uint16_t DjiAction2Camera::crc16(const uint8_t*d,size_t n){uint16_t c=0x3692;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c&1)?(uint16_t)((c>>1)^0x8408):(uint16_t)(c>>1);}return c;}
void DjiAction2Camera::log(const char*fmt,...){char b[180];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);Serial.printf("[DJI2 %8lu] %s\n",(unsigned long)millis(),b);}

void DjiAction2Camera::begin(){
    prefs.begin(nano?"fpvcam-nano":"fpvcam-dji2",false);
    savedAddress=prefs.getString("address","");savedAddressType=prefs.getUChar("type",0);
    if(!NimBLEDevice::init("FPVCineCam32")){camState.status="DJI BLE INIT FAIL";return;}
    NimBLEDevice::setPower(3);
    camState.status="DJI OFFLINE";camState.model=modelName();camState.protocolVersion="DJI DUML";
    if(savedAddress.length()){requestedAddress=savedAddress;requestedAddressType=savedAddressType;reconnectWanted=true;nextReconnectMs=millis()+500;}
}

bool DjiAction2Camera::startScan(String&j){
    auto*s=NimBLEDevice::getScan();s->setActiveScan(true);s->setInterval(60);s->setWindow(45);auto r=s->getResults(4000,false);j="[";bool first=true;
    for(int i=0;i<r.getCount();i++){auto*d=r.getDevice(i);const std::string name=d->getName();const std::string md=d->getManufacturerData();const bool isNano=md.size()>=3&&(uint8_t)md[0]==0xAA&&(uint8_t)md[1]==0x08&&(uint8_t)md[2]==DJI_MFR_NANO;
        if(nano?!(isNano||name.find("Nano")!=std::string::npos):(!d->isAdvertisingService(DJI_SERVICE)&&name.find("Action")==std::string::npos))continue;if(!first)j+=',';first=false;String n=name.c_str(),a=d->getAddress().toString().c_str();j+="{\"name\":\""+n+"\",\"address\":\""+a+"\",\"type\":"+String(d->getAddress().getType())+"}";}
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
    // Action 2: arm pairing on FFF4 first. The Nano takes the pairing request directly.
    if(!nano){const uint8_t arm[2]={0x01,0x00};pairChar->writeValue(arm,sizeof(arm),true);}
    workMode=0xFF;camState.videoMode="";
    lastRxMs=millis();pinAtMs=lastRxMs+200;pinPending=true;
    if(nano){nanoInitAtMs=pinAtMs+300;nanoInitPending=true;}
}

bool DjiAction2Camera::sendFrame(uint8_t receiver,uint8_t flags,uint8_t set,uint8_t id,const uint8_t*p,size_t pn,int32_t fixedSeq){
    if(!writeChar||!client||!client->isConnected()||pn>48)return false;uint8_t f[64]{};const size_t total=13+pn;const uint16_t s=fixedSeq>=0?(uint16_t)fixedSeq:seq++;
    f[0]=0x55;f[1]=total&0xFF;f[2]=0x04|((total>>8)&0x03);f[3]=crc8(f,3);f[4]=APP;f[5]=receiver;f[6]=s>>8;f[7]=s&0xFF;f[8]=flags;f[9]=set;f[10]=id;if(pn)memcpy(f+11,p,pn);
    const uint16_t c=crc16(f,11+pn);f[11+pn]=c&0xFF;f[12+pn]=c>>8;
    return writeChar->writeValue(f,total,false);
}
void DjiAction2Camera::sendPairingPin(){
    // Fixed identifier: the camera stores its approval under it, so later connects skip the prompt.
    // The Nano expects the numeric code "2050" where the Action 2 takes "osmo".
    static const char id[]="001749319286102";const char* token=nano?"2050":"osmo";uint8_t p[2+sizeof(id)+8];size_t n=0;
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
    if(!nano)return;
    // Nano pushes (no status byte in front of the data).
    if((flags&0x80)==0&&set==0x02&&id==0x80&&n>=33){handleNanoState(p,n);return;}
    if((flags&0x80)==0&&set==0x0D&&id==0x02&&n>=21&&p[20]<=100){camState.batteryPercent=p[20];return;}
    if((flags&0x80)==0&&set==0x00&&id==0x99&&f[4]==PARAMS){handleParam(p,n);return;}
}

void DjiAction2Camera::handleNanoState(const uint8_t*p,size_t n){
    const uint8_t mode=p[4];
    if(mode!=workMode){log("Nano work mode %u%s",mode,mode>1?" (not shooting)":"");workMode=mode;}
    const uint8_t rec=(le32(p)>>6)&0x03;  // 0 idle, 1 starting, 2 recording, 3 stopping
    camState.recording=rec==1||rec==2;
    const uint32_t left=le32(p+17);char t[24];snprintf(t,sizeof(t),"%luH:%02lu",(unsigned long)(left/3600),(unsigned long)((left%3600)/60));camState.mediaRemaining=t;
    camState.ready=mode<=1;
    camState.status=camState.recording?"DJI REC":(mode<=1?"DJI READY":"DJI ASLEEP");
    sessionReady=true;  // state pushes only arrive once the session is open
}

void DjiAction2Camera::handleParam(const uint8_t*p,size_t n){
    // p[1]==0x06 is a value push; name at 15, value length + value after a 6-byte gap.
    if(n<15||p[1]!=0x06)return;
    const size_t nameLen=le16(p+13),vOff=15+nameLen+6;if(vOff+2>n)return;
    const size_t vLen=le16(p+vOff);if(vOff+2+vLen>n||vLen<2)return;
    if(nameLen!=strlen(NANO_VIDEO_PARAM)||memcmp(p+15,NANO_VIDEO_PARAM,nameLen)!=0)return;
    const uint8_t*v=p+vOff+2;const char*res=nanoResolution(v[0]);const int fps=nanoFps(v[1]);
    String m=res;if(fps){if(m.length())m+=' ';m+=String(fps);}
    if(m!=camState.videoMode){camState.videoMode=m;log("Nano video mode %s (res=%u fps=%u)",m.c_str(),v[0],v[1]);}
}

void DjiAction2Camera::subscribeParam(const char*name){
    const size_t nl=strlen(name);uint8_t pl[48];if(15+nl+4>sizeof(pl))return;size_t i=0;
    pl[i++]=0x02;pl[i++]=0x02;pl[i++]=0;pl[i++]=0;  // subscribe
    const uint32_t sid=paramSubId++;for(int b=0;b<4;b++)pl[i++]=(sid>>(8*b))&0xFF;
    pl[i++]=0;pl[i++]=0;pl[i++]=0;pl[i++]=(nl+6)&0xFF;pl[i++]=(nl+6)>>8;pl[i++]=nl&0xFF;pl[i++]=nl>>8;
    memcpy(pl+i,name,nl);i+=nl;memset(pl+i,0,4);i+=4;
    sendFrame(PARAMS,0x40,0x00,0x99,pl,i);
}

void DjiAction2Camera::sendNanoInit(){
    // Session init that makes the Nano start pushing state, then ask for video-format updates.
    const uint8_t init[4]={0,0,0,0},info[5]={0x31,0x31,0,0,0},cap[4]={0x00,0x01,0x1C,0x00},nm=0xA0,tk=0xB3;
    sendFrame(CAMERA,0x40,0x53,0x10,init,sizeof(init));
    sendFrame(CAMERA,0x40,0x00,0x32,info,sizeof(info));
    sendFrame(CAMERA,0x40,0x02,0x8E,cap,sizeof(cap));
    sendFrame(CAMERA,0x40,0x07,0x07,&nm,1);
    sendFrame(CAMERA,0x40,0x07,0x0E,&tk,1);
    sendFrame(CAMERA,0x40,0x07,0x0C,nullptr,0);
    subscribeParam(NANO_VIDEO_PARAM);
    log("Nano session init sent");
}

bool DjiAction2Camera::setRecording(bool on){if(!camState.controlReady)return false;
    // A Nano told to record while asleep writes a broken low-FPS clip; wait until it is awake.
    if(nano&&on&&workMode>1){camState.lastWrite="waiting for camera to wake";return false;}const uint8_t p=on?1:0;camState.lastCommand=on?"REC":"STOP";camState.lastWrite=sendFrame(CAMERA,0x40,0x02,0x02,&p,1)?"sent":"failed";return camState.lastWrite=="sent";}
void DjiAction2Camera::loop(){
    const uint32_t now=millis();
    if(pinPending&&(int32_t)(now-pinAtMs)>=0){pinPending=false;sendPairingPin();}
    if(nanoInitPending&&!pinPending&&(int32_t)(now-nanoInitAtMs)>=0){nanoInitPending=false;sendNanoInit();}
    if(approvalAckPending){approvalAckPending=false;const uint8_t ok=0x01;sendFrame(WIFI,0xC0,0x07,0x46,&ok,1,approvalSeq);sessionReady=true;log("PAIR approved on camera");}
    if(sessionReady&&!camState.controlReady){camState.paired=camState.ready=camState.controlReady=true;camState.status="DJI READY";lastPollMs=now;}
    if(camState.connected&&!camState.controlReady&&now-authStartMs>=30000){log("PAIR timeout");client->disconnect();}
    if(camState.controlReady){
        if(now-lastRxMs>=15000){log("No camera traffic for 15 s, reconnecting");client->disconnect();}
        else if(!nano&&now-lastPollMs>=500){
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
void DjiAction2Camera::forgetPairing(){savedAddress="";disconnect();prefs.remove("address");prefs.remove("type");camState=CameraState{};camState.status="DJI OFFLINE";camState.model=modelName();camState.protocolVersion="DJI DUML";log("DJI saved target cleared");}
void DjiAction2Camera::ClientCallbacks::onConnect(NimBLEClient*){o->camState.connected=true;o->camState.status="DJI BLE LINK";}
void DjiAction2Camera::ClientCallbacks::onDisconnect(NimBLEClient*,int r){o->sessionReady=false;o->pinPending=false;o->nanoInitPending=false;o->workMode=0xFF;o->camState.videoMode="";o->approvalAckPending=false;o->rxLen=0;o->camState.connected=false;o->camState.controlReady=false;o->camState.ready=false;o->camState.recording=false;o->camState.status="DJI OFFLINE";o->log("BLE disconnected reason=%d",r);if(o->savedAddress.length()){o->reconnectWanted=true;o->nextReconnectMs=millis()+3000;}}
