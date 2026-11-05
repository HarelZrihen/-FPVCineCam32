#include "DjiActionCamera.h"
#include "DjiVideoFormats.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_system.h>

DjiActionCamera* DjiActionCamera::instance=nullptr;
static const NimBLEUUID DJI_SERVICE((uint16_t)0xFFF0);
static const NimBLEUUID DJI_NOTIFY((uint16_t)0xFFF4);
static const NimBLEUUID DJI_WRITE((uint16_t)0xFFF5);

DjiActionCamera::DjiActionCamera():callbacks(this){instance=this;}
uint16_t DjiActionCamera::read16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
uint32_t DjiActionCamera::read32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
uint16_t DjiActionCamera::crc16(const uint8_t*d,size_t n){uint16_t c=0x3AA3;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c&1)?(uint16_t)((c>>1)^0xA001):(uint16_t)(c>>1);}return c;}
uint32_t DjiActionCamera::crc32(const uint8_t*d,size_t n){uint32_t c=0x00003AA3;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c&1)?((c>>1)^0xEDB88320UL):(c>>1);}return c;}
void DjiActionCamera::log(const char*fmt,...){char b[180];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);Serial.printf("[DJI %8lu] %s\n",(unsigned long)millis(),b);logText+=String(millis())+" "+b+"\n";if(logText.length()>7000)logText.remove(0,logText.length()-6000);}

void DjiActionCamera::begin(){
    prefs.begin("fpvcam-dji",false);
    savedAddress=prefs.getString("address",savedAddress);savedAddressType=prefs.getUChar("type",savedAddressType);approvedBefore=prefs.getBool("approved",false);
    if(!NimBLEDevice::init("FPVCineCam32")){camState.status="DJI BLE INIT FAIL";return;}
    NimBLEDevice::setMTU(500);
    NimBLEDevice::setPower(3);
    camState.status="DJI OFFLINE";camState.model="DJI Osmo";camState.protocolVersion="DJI R SDK";
    if(savedAddress.length()){requestedAddress=savedAddress;requestedAddressType=savedAddressType;reconnectWanted=true;nextReconnectMs=millis()+500;}
}

bool DjiActionCamera::startScan(String&j){
    auto*s=NimBLEDevice::getScan();s->setActiveScan(true);s->setInterval(60);s->setWindow(45);auto r=s->getResults(4000,false);j="[";bool first=true;
    for(int i=0;i<r.getCount();i++){auto*d=r.getDevice(i);if(!d->isAdvertisingService(DJI_SERVICE))continue;if(!first)j+=',';first=false;String n=d->getName().c_str(),a=d->getAddress().toString().c_str();j+="{\"name\":\""+n+"\",\"address\":\""+a+"\",\"type\":"+String(d->getAddress().getType())+"}";}
    j+="]";s->clearResults();return true;
}
bool DjiActionCamera::connectTo(const String&a,uint8_t t){if(!a.length()||connectTaskRunning)return false;requestedAddress=a;requestedAddressType=t;connectRequested=true;camState.status="DJI CONNECT QUEUED";return true;}
void DjiActionCamera::connectTaskThunk(void*a){auto*self=(DjiActionCamera*)a;String x=self->requestedAddress;uint8_t t=self->requestedAddressType;self->connectRequested=false;self->performConnect(x,t);self->connectTaskRunning=false;vTaskDelete(nullptr);}
void DjiActionCamera::performConnect(const String&a,uint8_t t){
    if(client&&client->isConnected())client->disconnect();notifyChar=writeChar=nullptr;rxLen=0;pendingConnectionResponse=false;camState.connected=false;camState.controlReady=false;camState.ready=false;camState.status="DJI CONNECTING";
    if(!client){client=NimBLEDevice::createClient();if(!client){camState.status="DJI CLIENT FAIL";return;}client->setClientCallbacks(&callbacks,false);client->setConnectTimeout(8000);}
    NimBLEAddress ad(a.c_str(),t);if(!client->connect(ad,true,false,true)){camState.status="DJI CONNECT FAIL";log("BLE connect failed error=%d",client->getLastError());return;}log("BLE connected MTU=%u",(unsigned)client->getMTU());
    auto*svc=client->getService(DJI_SERVICE);if(!svc){camState.status="DJI SERVICE MISSING";client->disconnect();return;}notifyChar=svc->getCharacteristic(DJI_NOTIFY);writeChar=svc->getCharacteristic(DJI_WRITE);
    if(!notifyChar||!writeChar){camState.status="DJI CHAR MISSING";client->disconnect();return;}
    bool n=notifyChar->canNotify()?notifyChar->subscribe(true,notifyCb):(notifyChar->canIndicate()?notifyChar->subscribe(false,notifyCb):false);if(!n){camState.status="DJI NOTIFY FAIL";client->disconnect();return;}
    const bool known=approvedBefore&&a.equalsIgnoreCase(savedAddress)&&t==savedAddressType;if(!known)approvedBefore=false;
    savedAddress=a;savedAddressType=t;prefs.putString("address",a);prefs.putUChar("type",t);camState.connected=true;camState.paired=approvedBefore;camState.status="DJI APPROVAL";sendConnectionRequest();
}

bool DjiActionCamera::sendFrame(uint8_t set,uint8_t id,uint8_t type,const uint8_t*p,size_t pn,uint16_t fixedSeq){
    if(!writeChar||!client||!client->isConnected()||pn>220)return false;uint8_t f[256]{};size_t total=18+pn;uint16_t s=fixedSeq?fixedSeq:++seq;
    f[0]=0xAA;f[1]=total&0xFF;f[2]=(total>>8)&0x03;f[3]=type;f[8]=s&0xFF;f[9]=s>>8;uint16_t h=crc16(f,10);f[10]=h&0xFF;f[11]=h>>8;f[12]=set;f[13]=id;if(pn)memcpy(f+14,p,pn);uint32_t tail=crc32(f,14+pn);f[14+pn]=tail&0xFF;f[15+pn]=(tail>>8)&0xFF;f[16+pn]=(tail>>16)&0xFF;f[17+pn]=(tail>>24)&0xFF;
    bool ok=writeChar->writeValue(f,total,true);log("TX %02X/%02X type=%02X seq=%u %s",set,id,type,s,ok?"OK":"FAIL");return ok;
}
void DjiActionCamera::sendConnectionRequest(){
    uint8_t p[33]{};uint32_t controllerId=0x12345678;p[0]=controllerId&0xFF;p[1]=controllerId>>8;p[2]=controllerId>>16;p[3]=controllerId>>24;p[4]=6;uint64_t mac=ESP.getEfuseMac();for(int i=0;i<6;i++)p[5+i]=(mac>>(8*i))&0xFF;
    const uint8_t verifyMode=approvedBefore?0:1;const uint16_t verify=(uint16_t)(esp_random()%10000);p[26]=verifyMode;p[27]=verify&0xFF;p[28]=verify>>8;
    char code[8];snprintf(code,sizeof(code),"%04u",(unsigned)verify);
    camState.lastCommand="DJI CONNECT";
    if(!approvedBefore){camState.status="CONFIRM CODE "+String(code);log("PAIR first-time code=%s; confirm matching code on camera",code);}else{camState.status="DJI APPROVAL";log("PAIR known-camera mode=0");}
    camState.lastWrite=sendFrame(0x00,0x19,0x02,p,sizeof(p))?"sent":"failed";
}
void DjiActionCamera::sendConnectionResponse(uint16_t incomingSeq){uint8_t p[9]{};uint32_t controllerId=0x12345678;p[0]=controllerId&0xFF;p[1]=controllerId>>8;p[2]=controllerId>>16;p[3]=controllerId>>24;p[4]=0;sendFrame(0x00,0x19,0x20,p,sizeof(p),incomingSeq);}
void DjiActionCamera::subscribeStatus(){uint8_t p[6]={3,20,0,0,0,0};if(sendFrame(0x1D,0x05,0x01,p,sizeof(p)))camState.incomingSubscription="DJI 1D05 2Hz";}
void DjiActionCamera::setModel(uint32_t id){cameraDeviceId=id;switch(id){case 0xFF33:camState.model="DJI Osmo Action 4";break;case 0xFF44:camState.model="DJI Osmo Action 5 Pro";break;case 0xFF55:camState.model="DJI Osmo Action 6";break;case 0xFF66:camState.model="DJI Osmo 360";break;default:camState.model="DJI Osmo";break;}}

void DjiActionCamera::notifyCb(NimBLERemoteCharacteristic*,uint8_t*d,size_t n,bool){if(instance)instance->handleNotify(d,n);}
void DjiActionCamera::handleNotify(const uint8_t*d,size_t n){
    camState.incomingPackets++;
    if(!n||d[0]!=0xAA)return;
    rxLen=0;
    if(n>sizeof(rxBuf)){log("RX DJI frame too large n=%u",(unsigned)n);return;}
    memcpy(rxBuf,d,n);rxLen=n;parseFrames();
}
void DjiActionCamera::parseFrames(){
    static uint32_t rejectCount=0,lastRejectLogMs=0;
    while(rxLen>=3){
        size_t start=0;while(start<rxLen&&rxBuf[start]!=0xAA)start++;if(start){memmove(rxBuf,rxBuf+start,rxLen-start);rxLen-=start;if(rxLen<3)return;}
        uint16_t total=read16(rxBuf+1)&0x03FF;if(total<18||total>sizeof(rxBuf)){memmove(rxBuf,rxBuf+1,--rxLen);continue;}if(rxLen<total){log("RX DJI short frame n=%u expected=%u",(unsigned)rxLen,(unsigned)total);rxLen=0;return;}
        const uint16_t got16=read16(rxBuf+10),want16=crc16(rxBuf,10);const uint32_t got32=read32(rxBuf+total-4),want32=crc32(rxBuf,total-4);
        if(want16!=got16||want32!=got32){
            rejectCount++;const uint32_t now=millis();
            if(rejectCount<=6||now-lastRejectLogMs>=1000){char hex[73]{};size_t shown=total<24?total:24;for(size_t i=0;i<shown;i++)snprintf(hex+i*3,sizeof(hex)-i*3,"%02X ",rxBuf[i]);log("RX CRC reject #%lu len=%u c16=%04X/%04X c32=%08lX/%08lX head=%s",(unsigned long)rejectCount,(unsigned)total,(unsigned)got16,(unsigned)want16,(unsigned long)got32,(unsigned long)want32,hex);lastRejectLogMs=now;}
            rxLen=0;return;
        }
        handleFrame(rxBuf,total);rxLen=0;return;
    }
}
void DjiActionCamera::handleFrame(const uint8_t*f,size_t len){
    uint8_t type=f[3],set=f[12],id=f[13];uint16_t frameSeq=read16(f+8);const uint8_t*p=f+14;size_t n=len-18;char b[48];snprintf(b,sizeof(b),"%02X/%02X type=%02X len=%u",set,id,type,(unsigned)n);camState.lastIncoming=b;if(!(set==0x1D&&id==0x02))log("RX %s",b);
    if(set==0x00&&id==0x19){
        if((type&0x20)==0&&n>=33){
            if(p[26]==2){setModel(read32(p));if(read16(p+27)==0){pendingConnectionSeq=frameSeq;pendingConnectionResponse=true;camState.status="DJI APPROVED";}else{camState.status="DJI REJECTED";client->disconnect();}}
        }
        return;
    }
    if(set==0x1D&&id==0x02&&(type&0x20)==0&&n>=38){uint8_t mode=p[0],status=p[1];uint16_t recordTime=read16(p+5);bool videoMode=(mode==0x00||mode==0x01||mode==0x02||mode==0x0A||mode==0x28||mode==0x38||mode==0x3A||mode==0x41||mode==0x43||mode==0x44||mode==0x4A);camState.recording=videoMode&&status==0x03&&(recordTime>0||mode!=0x05);remainingSeconds=read32(p+23);char t[24];snprintf(t,sizeof(t),"%luH:%02lu",(unsigned long)(remainingSeconds/3600),(unsigned long)((remainingSeconds%3600)/60));camState.mediaRemaining=t;camState.batteryPercent=p[37];camState.ready=true;camState.controlReady=true;camState.status=camState.recording?"DJI REC":"DJI READY";lastCameraMode=mode;userMode=p[27]<=5?(int8_t)p[27]:0;nextModeFlag=p[29];lastStatusMs=millis();updateVideoMode(mode,p[2],p[3],p[4]);return;}
    // 1D06 (new camera status push): ASCII mode name at 2 (length at 1) and parameters at 25
    // (length at 24). Follows 1D02 when camera_mode > 0x34; only used for those modes.
    if(set==0x1D&&id==0x06&&(type&0x20)==0&&n>=46&&p[0]==0x01&&p[23]==0x02){
        if(lastCameraMode<=0x34)return;
        auto field=[&](size_t off,uint8_t len){String s;for(uint8_t i=0;i<len&&i<20;++i){const char c=(char)p[off+i];if(!c)break;s+=(c>=32&&c<127)?c:' ';}s.trim();return s;};
        String m=field(2,p[1]);const String param=field(25,p[24]);if(param.length()){if(m.length())m+=' ';m+=param;}
        m.toUpperCase();if(m.length()>31)m.remove(31);
        if(m!=camState.videoMode){camState.videoMode=m;Serial.printf("[DJI] Video mode %s (1D06)\n",m.c_str());}
        return;
    }
    if(set==0x00&&id==0x11&&(type&0x20)&&n>=1){if(p[0]!=0)log("QS key rejected, code %u",p[0]);return;}
    if(set==0x1D&&id==0x03&&(type&0x20)&&n>=1)camState.lastWrite=p[0]==0?"DJI RECORD ACK OK":"DJI RECORD ACK FAIL";
}

// Video mode from the 1D02 status push: camera_mode (0), video_resolution (2), fps_idx (3), EIS_mode (4).
void DjiActionCamera::updateVideoMode(uint8_t mode,uint8_t res,uint8_t fps,uint8_t eis){
    static const char* eisNames[]={"","RS","HS","RS+","HB"};
    const char* e=eis<5?eisNames[eis]:"";
    String m;
    switch(mode){
    case 0x01:case 0x28:{  // Video, SuperNight
        m=djiformat::videoText(res,fps);if(mode==0x28)m="NIGHT "+m;if(*e){m+=' ';m+=e;}break;}
    case 0x00:{  // Slow motion: fps_idx is still the frame rate; DJI shows multiplier = frame rate / 30
        m=djiformat::resolution(res);const int f=djiformat::fps(fps);m+=" SLOMO";if(f>=60)m+=" "+String(f/30)+"X";break;}
    case 0x02:m="TIMELAPSE";break;
    case 0x0A:m="HYPERLAPSE";break;
    case 0x05:m="PHOTO";break;
    default:  // Modes > 0x34 (Osmo 360) report name and parameters in 1D06 instead
        if(mode>0x34)return;
        m="";break;
    }
    m.trim();
    if(m!=camState.videoMode){camState.videoMode=m;Serial.printf("[DJI] Video mode %s (mode=0x%02X res=%u fps=%u eis=%u)\n",m.c_str(),mode,res,fps,eis);}
}
bool DjiActionCamera::setRecording(bool on){if(!camState.controlReady)return false;if(on)cancelPreset("REC requested");uint8_t p[9]{};p[0]=cameraDeviceId&0xFF;p[1]=cameraDeviceId>>8;p[2]=cameraDeviceId>>16;p[3]=cameraDeviceId>>24;p[4]=on?0:1;camState.lastCommand=on?"REC":"STOP";camState.lastWrite=sendFrame(0x1D,0x03,0x02,p,sizeof(p))?"sent":"failed";return camState.lastWrite=="sent";}
// DJI documents no command to load a custom mode, but it does document the QS key (0011,
// key_code 0x02: "each press changes to the next mode") and reports the active user_mode (1D02
// byte 27). So: press QS, wait for the camera to leave its pre-switch screen
// (camera_mode_next_flag == camera_mode) and report a fresh status, then check user_mode and
// press again if needed. Stops when the target is reached, after a full loop of the QS list
// (target not in the list), or after QS_MAX presses.
static constexpr uint32_t QS_SETTLE_MS=2500;
static constexpr uint8_t QS_MAX=12;
bool DjiActionCamera::requestPreset(int32_t mode){
    if(mode<0||mode>5||!camState.controlReady||camState.recording)return false;
    if(userMode==mode&&targetUserMode<0)return true;  // already there
    targetUserMode=(int8_t)mode;qsPresses=0;lastQsMs=millis()-QS_SETTLE_MS;startCameraMode=lastCameraMode;startUserMode=userMode;
    log("Switching to %s (now %s)",mode?("C"+String(mode)).c_str():"General",userMode>0?("C"+String(userMode)).c_str():"General");
    return true;
}
void DjiActionCamera::cancelPreset(const char* why){
    if(targetUserMode<0)return;
    log("Custom mode switch stopped: %s",why);targetUserMode=-1;
}
void DjiActionCamera::stepPreset(uint32_t now){
    if(targetUserMode<0)return;
    if(!camState.controlReady){cancelPreset("camera disconnected");return;}
    if(camState.recording)return;  // never step while recording
    // Wait for a status push that came after the camera finished switching.
    if(nextModeFlag!=lastCameraMode||(int32_t)(lastStatusMs-lastQsMs)<(int32_t)QS_SETTLE_MS)return;
    if(userMode==targetUserMode){log("Custom mode reached after %u QS press(es)",qsPresses);targetUserMode=-1;return;}
    if(qsPresses>0&&lastCameraMode==startCameraMode&&userMode==startUserMode){cancelPreset("target is not in the camera's quick-switch list");return;}
    if(qsPresses>=QS_MAX){cancelPreset("too many QS presses");return;}
    const uint8_t key[4]={0x02,0x01,0x00,0x00};  // QS button, key event, short press
    if(sendFrame(0x00,0x11,0x02,key,sizeof(key))){qsPresses++;lastQsMs=now;camState.lastCommand="QS";}
}
void DjiActionCamera::loop(){
    stepPreset(millis());
    if(pendingConnectionResponse){
        const uint16_t s=pendingConnectionSeq;pendingConnectionResponse=false;sendConnectionResponse(s);approvedBefore=true;prefs.putBool("approved",true);camState.paired=true;camState.ready=true;camState.controlReady=true;camState.status="DJI READY";subscribeStatus();
    }
    if(connectRequested&&!connectTaskRunning){connectTaskRunning=true;if(xTaskCreate(connectTaskThunk,"dji-connect",8192,this,1,nullptr)!=pdPASS){connectTaskRunning=false;connectRequested=false;camState.status="DJI TASK FAIL";}}
    if(reconnectWanted&&!connectTaskRunning&&!connectRequested&&(int32_t)(millis()-nextReconnectMs)>=0&&savedAddress.length()){reconnectWanted=false;connectTo(savedAddress,savedAddressType);}
}
void DjiActionCamera::disconnect(){reconnectWanted=false;if(client&&client->isConnected())client->disconnect();}
void DjiActionCamera::forgetPairing(){disconnect();prefs.remove("address");prefs.remove("type");prefs.remove("approved");savedAddress="";approvedBefore=false;pendingConnectionResponse=false;camState=CameraState{};camState.status="DJI OFFLINE";camState.model="DJI Osmo";camState.protocolVersion="DJI R SDK";log("DJI saved target cleared; unrelated BLE bonds untouched");}
void DjiActionCamera::ClientCallbacks::onConnect(NimBLEClient*){o->camState.connected=true;o->camState.status="DJI BLE LINK";}
void DjiActionCamera::ClientCallbacks::onDisconnect(NimBLEClient*,int r){o->pendingConnectionResponse=false;o->camState.videoMode="";o->userMode=-1;o->targetUserMode=-1;o->camState.connected=false;o->camState.controlReady=false;o->camState.ready=false;o->camState.recording=false;o->camState.status="DJI OFFLINE";o->log("BLE disconnected reason=%d",r);if(o->savedAddress.length()){o->reconnectWanted=true;o->nextReconnectMs=millis()+3000;}}