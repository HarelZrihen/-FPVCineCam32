#include "GoProCamera.h"
#include <cstdarg>
#include <memory>
#include <new>

// Source of truth: https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/
// GP-XXXX = b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b.
static const NimBLEUUID SERVICE((uint16_t)0xFEA6);
static NimBLEUUID gpUuid(uint16_t id) {
    char value[37];
    snprintf(value,sizeof(value),"b5f9%04x-aa8d-11e3-9046-0002a5d5c51b",id);
    return NimBLEUUID(value);
}
GoProCamera::GoProCamera():callbacks(this),storeCallbacks(this){}

void GoProCamera::begin(const String& blackmagicAddress) {
    protectedAddress=blackmagicAddress;
    storageReady=prefs.begin("fpvcam-gopro",false);
    if(!storageReady){fail("GoPro NVS open failed");return;}
    savedAddress=prefs.getString("address",""); savedType=prefs.getUChar("type",0);
    identityAddress=prefs.getString("identity",""); identityType=prefs.getUChar("idtype",0);
    if(!NimBLEDevice::init("FPVCineCam32")){storageReady=false;fail("BLE initialization failed");return;}
    // GoPro can read the central's GAP Device Name after pairing. No advertising.
    NimBLEDevice::createServer()->start();
    NimBLEDevice::setPower(3);
    // Legacy Just Works bonding: encrypted link, no PIN entry. GoPro-only config.
    NimBLEDevice::setSecurityAuth(true,false,false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    // Never let the default round-robin store handler evict a Blackmagic bond.
    NimBLEDevice::setDeviceCallbacks(&storeCallbacks);
    log("BLE initialized; NVS bond persistence enabled");
    loadSaved();
    // Migrate the old single saved peer only if its bond actually exists.
    const String legacyIdentity=identityAddress.length()?identityAddress:savedAddress;
    const uint8_t legacyType=identityAddress.length()?identityType:savedType;
    if(legacyIdentity.length() && !protectedAddress.equalsIgnoreCase(legacyIdentity) &&
       NimBLEDevice::isBonded(NimBLEAddress(legacyIdentity.c_str(),legacyType))){
        Found legacy{};
        snprintf(legacy.address,sizeof(legacy.address),"%s",savedAddress.c_str());legacy.type=savedType;
        rememberPaired(legacyIdentity,legacyType,legacy);
    }
    if(savedAddress.length()){
        snprintf(requested.address,sizeof(requested.address),"%s",savedAddress.c_str());
        requested.type=savedType;
        log("Boot reconnect queued");
        retryAllowed=true; reconnect=true; retryAt=millis();
    }
}

String GoProCamera::savedId(const SavedCamera& value){
    return String(value.type)+":"+value.address;
}
bool GoProCamera::matches(const SavedCamera& value,const String& address,uint8_t type){
    return (value.type==type && address.equalsIgnoreCase(value.address)) ||
           (value.advertisedType==type && address.equalsIgnoreCase(value.advertisedAddress));
}
GoProCamera::SavedList GoProCamera::savedSnapshot(){
    SavedList copy;
    portENTER_CRITICAL(&mux);copy=savedList;portEXIT_CRITICAL(&mux);
    return copy;
}
void GoProCamera::loadSaved(){
    if(!prefs.isKey("cards_v1"))return;
    SavedList copy;
    bool valid=prefs.getBytesLength("cards_v1")==sizeof(copy) &&
        prefs.getBytes("cards_v1",&copy,sizeof(copy))==sizeof(copy) && copy.version==1 && copy.count<=MAX_SAVED;
    if(valid)for(size_t i=0;i<copy.count;++i){
        const auto& c=copy.cameras[i];
        if(c.address[17]!=0 || strlen(c.address)!=17 || c.type>3 ||
           c.advertisedAddress[17]!=0 || c.advertisedType>3 ||
           c.reportedName[63]!=0 || c.friendlyName[48]!=0)valid=false;
        for(size_t j=0;j<i;++j)if(savedId(c)==savedId(copy.cameras[j]))valid=false;
    }
    if(!valid){savedListAvailable=false;savedListError=true;log("Saved camera list invalid; preserved without overwrite");return;}
    portENTER_CRITICAL(&mux);savedList=copy;portEXIT_CRITICAL(&mux);
}
bool GoProCamera::persistSaved(const SavedList& value){
    if(!savedListAvailable)return false;
    if(prefs.putBytes("cards_v1",&value,sizeof(value))!=sizeof(value)){
        savedListError=true;log("Saved camera list write failed");return false;
    }
    portENTER_CRITICAL(&mux);savedList=value;portEXIT_CRITICAL(&mux);
    savedListError=false;return true;
}
bool GoProCamera::findSaved(const String& id,SavedCamera& value){
    auto copy=savedSnapshot();
    for(size_t i=0;i<copy.count;++i)if(savedId(copy.cameras[i])==id){value=copy.cameras[i];return true;}
    return false;
}
void GoProCamera::rememberPaired(const String& address,uint8_t type,const Found& discovered){
    portENTER_CRITICAL(&mux);
    snprintf(activeAddress,sizeof(activeAddress),"%s",address.c_str());activeType=type;
    portEXIT_CRITICAL(&mux);
    auto copy=savedSnapshot();size_t index=copy.count;
    for(size_t i=0;i<copy.count;++i)if(matches(copy.cameras[i],address,type) ||
        matches(copy.cameras[i],discovered.address,discovered.type)){index=i;break;}
    if(index==MAX_SAVED){savedListError=true;log("Saved camera list full");return;}
    if(index==copy.count)++copy.count;
    auto& c=copy.cameras[index];
    snprintf(c.address,sizeof(c.address),"%s",address.c_str());c.type=type;
    // Keep a known advertisement alias when reconnecting directly to the identity.
    if(discovered.address[0] && (!c.advertisedAddress[0] || !address.equalsIgnoreCase(discovered.address))){
        snprintf(c.advertisedAddress,sizeof(c.advertisedAddress),"%s",discovered.address);c.advertisedType=discovered.type;
    }
    if(discovered.name[0])snprintf(c.reportedName,sizeof(c.reportedName),"%s",discovered.name);
    auto old=savedSnapshot();
    if(memcmp(&old,&copy,sizeof(copy))!=0)persistSaved(copy);
}
bool GoProCamera::prepareConnect(){
    // Only an explicit camera switch reaches here with an existing connection.
    if(client && client->isConnected()){
        retryAllowed=false;reconnect=false;client->disconnect();
        for(unsigned i=0;i<30 && linked;++i)vTaskDelay(pdMS_TO_TICKS(100));
        if(linked){log("Camera switch failed: disconnect timeout");return false;}
        retryAllowed=true;
    }
    portENTER_CRITICAL(&mux);
    snprintf(activeAddress,sizeof(activeAddress),"%s",requested.address);activeType=requested.type;
    portEXIT_CRITICAL(&mux);
    return true;
}

bool GoProCamera::launch(Job value) {
    bool expected=false;
    if(!storageReady || !working.compare_exchange_strong(expected,true))return false;
    job=value;
    if(xTaskCreate(task,"gopro-link",8192,this,1,nullptr)!=pdPASS){
        working=false;fail("Task allocation failed");return false;
    }
    return true;
}
void GoProCamera::task(void* context) {
    auto* self=static_cast<GoProCamera*>(context);
    switch(self->job){
        case Job::Scan: self->runScan();break;
        case Job::Connect: if(self->prepareConnect())self->runConnect();break;
        case Job::Forget: self->runForget();break;
        case Job::Rec: self->runShutter(true);break;
        case Job::Stop: self->runShutter(false);break;
        case Job::Preset: self->runPreset();break;
        case Job::ReadPreset: self->runReadPreset();break;
    }
    self->working=false;
    vTaskDelete(nullptr);
}
bool GoProCamera::scan(){
    if(working)return false;
    reconnect=false;
    return launch(Job::Scan);
}
bool GoProCamera::connectDiscovered(unsigned index){
    if(working || !savedListAvailable)return false;
    portENTER_CRITICAL(&mux);
    bool valid=index<foundCount;
    if(valid)requested=found[index];
    portEXIT_CRITICAL(&mux);
    if(!valid || protectedAddress.equalsIgnoreCase(requested.address))return false;
    reconnect=false;retryAllowed=true;
    return launch(Job::Connect);
}
bool GoProCamera::connectSaved(const String& id){
    if(working)return false;
    SavedCamera value{};
    if(!findSaved(id,value) || protectedAddress.equalsIgnoreCase(value.address))return false;
    snprintf(requested.address,sizeof(requested.address),"%s",value.address);requested.type=value.type;
    snprintf(requested.name,sizeof(requested.name),"%s",value.reportedName);
    reconnect=false;retryAllowed=true;
    return launch(Job::Connect);
}
bool GoProCamera::forget(const String& id){
    if(working || !findSaved(id,removal))return false;
    return launch(Job::Forget);
}
bool GoProCamera::renameSaved(const String& id,const String& name){
    if(name.length()>48 || !storageReady || !savedListAvailable)return false;
    for(size_t i=0;i<name.length();++i)if((uint8_t)name[i]<32)return false;
    bool expected=false;
    if(!working.compare_exchange_strong(expected,true))return false;
    auto list=savedSnapshot();bool ok=false;
    for(size_t i=0;i<list.count;++i)if(savedId(list.cameras[i])==id){
        snprintf(list.cameras[i].friendlyName,sizeof(list.cameras[i].friendlyName),"%s",name.c_str());
        ok=persistSaved(list);break;
    }
    working=false;return ok;
}
void GoProCamera::loop(){
    bool timedOut=false;
    portENTER_CRITICAL(&mux);
    if(statusPending && (int32_t)(millis()-statusDeadline)>=0){statusPending=false;timedOut=true;}
    portEXIT_CRITICAL(&mux);
    if(timedOut)statusFault("registration timeout");
    if(queryFragmentPending && millis()-queryFragmentAt.load()>=2000 && queryFragmentPending.exchange(false))
        statusFault("fragment timeout");
    if(!working && reconnect && (int32_t)(millis()-retryAt.load())>=0){
        reconnect=false;
        if(savedAddress.length()){
            snprintf(requested.address,sizeof(requested.address),"%s",savedAddress.c_str());
            requested.type=savedType;
            log("Reconnect attempt");
            launch(Job::Connect);
        }
    }
}
void GoProCamera::runScan(){
    const State previous=state.load();
    auto cards=savedSnapshot();bool namesChanged=false;
    state=State::Scanning;log("Scan started: FEA6");
    portENTER_CRITICAL(&mux);foundCount=0;portEXIT_CRITICAL(&mux);
    auto* scanner=NimBLEDevice::getScan();
    scanner->setActiveScan(true);scanner->setInterval(60);scanner->setWindow(45);
    auto results=scanner->getResults(3500,false);
    for(int i=0;i<results.getCount();++i){
        const auto* d=results.getDevice(i);
        if(!d->isAdvertisingService(SERVICE))continue;
        Found item{};
        snprintf(item.name,sizeof(item.name),"%s",d->getName().c_str());
        snprintf(item.address,sizeof(item.address),"%s",d->getAddress().toString().c_str());
        item.type=d->getAddress().getType();
        if(protectedAddress.equalsIgnoreCase(item.address))continue;
        portENTER_CRITICAL(&mux);
        if(foundCount<MAX_FOUND)found[foundCount++]=item;
        portEXIT_CRITICAL(&mux);
        log("Camera found: %.50s %s type=%u",item.name,item.address,item.type);
        for(size_t j=0;j<cards.count;++j)if(matches(cards.cameras[j],item.address,item.type) && item.name[0] &&
            strcmp(cards.cameras[j].reportedName,item.name)!=0){
            snprintf(cards.cameras[j].reportedName,sizeof(cards.cameras[j].reportedName),"%s",item.name);namesChanged=true;
        }
    }
    scanner->clearResults();
    if(namesChanged)persistSaved(cards);
    state=linked?previous:State::Offline;log("Scan complete");
}
void GoProCamera::fail(const char* reason,bool retry){
    log("Connection failed: %s error=%d",reason,client?client->getLastError():0);
    retryAllowed=retry;reconnect=false;state=State::Failed;secured=false;bonded=false;
    if(client && client->isConnected())client->disconnect();
    state=State::Failed;
    if(retry && savedAddress.length()){retryAt=millis()+5000;reconnect=true;}
}
void GoProCamera::runConnect(){
    if(protectedAddress.equalsIgnoreCase(requested.address)){fail("Protected Blackmagic address");return;}
    clearStatus();
    state=State::Connecting;hardwareReady=false;
    log("Connect attempt %s type=%u",requested.address,requested.type);
    if(savedAddress != requested.address){
        // Do not associate a newly selected camera with a previous GoPro identity.
        identityAddress="";prefs.remove("identity");prefs.remove("idtype");
    }
    // Save only the chosen GoPro target; never write Blackmagic's namespace/keys.
    if(!prefs.putString("address",requested.address) || !prefs.putUChar("type",requested.type)){
        fail("Target save failed");return;
    }
    savedAddress=requested.address;savedType=requested.type;
    if(!client){
        client=NimBLEDevice::createClient();
        if(!client){fail("Client allocation failed");return;}
        client->setClientCallbacks(&callbacks,false);client->setConnectTimeout(8000);
    }
    NimBLEAddress address(savedAddress.c_str(),savedType);
    if(!client->connect(address,true,false,false)){fail("BLE connect",true);return;}
    state=State::Pairing;
    log("Security/bonding started; stored bond=%d",NimBLEDevice::isBonded(address));
    if(!client->secureConnection() || !client->isConnected() || !client->getConnInfo().isEncrypted()){
        fail("Security/bonding failed");return;
    }
    auto info=client->getConnInfo();
    NimBLEAddress identity=info.getIdAddress();
    if(!info.isBonded() || !NimBLEDevice::isBonded(identity)){
        fail("Encrypted but persistent bond missing");return;
    }
    if(protectedAddress.equalsIgnoreCase(identity.toString().c_str())){
        fail("Protected Blackmagic identity");return;
    }
    // Store the resolved identity for targeted bond deletion after an ESP restart.
    if(!prefs.putString("identity",identity.toString().c_str()) || !prefs.putUChar("idtype",identity.getType())){
        fail("Bond identity save failed");return;
    }
    identityAddress=identity.toString().c_str();identityType=identity.getType();
    // Reconnect to the bonded identity, not a potentially rotating advertisement.
    if(!prefs.putString("address",identityAddress) || !prefs.putUChar("type",identityType)){
        fail("Identity target save failed");return;
    }
    savedAddress=identityAddress;savedType=identityType;
    secured=true;bonded=true;state=State::Connected;
    log("Bonding success: encrypted=1 stored=1");
    rememberPaired(identityAddress,identityType,requested);
    auto* service=client->getService(SERVICE);
    if(!service){fail("FEA6 service missing");return;}
    log("Service discovered: FEA6");
    NimBLERemoteCharacteristic* chars[6]{};
    for(unsigned i=0;i<6;++i){
        chars[i]=service->getCharacteristic(gpUuid(0x72+i));
        const bool valid=chars[i] && ((i&1)?chars[i]->canNotify():chars[i]->canWrite());
        log("Characteristic GP-%04X %s",0x72+i,valid?"discovered":"missing/wrong properties");
        if(!valid){fail("Required characteristic missing");return;}
    }
    for(unsigned i=1;i<6;i+=2){
        const bool commandResponse=i==1, statusResponse=i==5;
        bool ok=chars[i]->subscribe(true,[this,commandResponse,statusResponse](NimBLERemoteCharacteristic*,uint8_t* bytes,size_t length,bool){
            if(commandResponse)response(bytes,length);
            if(statusResponse)queryResponse(bytes,length);
        });
        log("Subscription GP-%04X %s",0x72+i,ok?"success":"failure");
        if(!ok){fail("Required notification subscription failed");return;}
    }
    // Official BLE readiness check: read-only Get Hardware Info (0x3C).
    // No shutter, settings, Wi-Fi, media or keep-alive commands in this milestone.
    const uint8_t hardwareInfo[]={0x01,0x3C};
    for(unsigned attempt=0;attempt<5 && client->isConnected() && !hardwareReady;++attempt){
        log("Hardware Info readiness probe %u",attempt+1);
        if(!chars[0]->writeValue(hardwareInfo,sizeof(hardwareInfo),true)){
            fail("Hardware Info write failed");return;
        }
        for(unsigned wait=0;wait<40 && client->isConnected() && !hardwareReady;++wait)vTaskDelay(pdMS_TO_TICKS(100));
    }
    if(!client->isConnected() || !hardwareReady){fail("Hardware Info readiness timeout");return;}
    state=State::Ready;reconnect=false;retryAllowed=true;log("Control ready");
    startStatus(chars[4]);
}
bool GoProCamera::setShutter(const String& id,bool on){
    if(working || state!=State::Ready || !linked || !secured || !hardwareReady)return false;
    char address[18];uint8_t type;
    portENTER_CRITICAL(&mux);
    memcpy(address,activeAddress,sizeof(address));type=activeType;
    portEXIT_CRITICAL(&mux);
    // Reject stale browser cards; never switch camera or change the saved target.
    if(id!=String(type)+":"+address)return false;
    log("%s command requested",on?"REC":"STOP");
    return launch(on?Job::Rec:Job::Stop);
}
void GoProCamera::runShutter(bool on){
    const char* label=on?"REC":"STOP";
    if(state!=State::Ready || !linked || !secured || !hardwareReady || !client || !client->isConnected()){
        log("%s rejected: control not ready",label);return;
    }
    auto* service=client->getService(SERVICE);
    auto* command=service?service->getCharacteristic(gpUuid(0x72)):nullptr;
    if(!command){log("%s write failed: command characteristic unavailable",label);return;}
    // Official Set Shutter TLV: ID=01, length=01, on/off. 03 is packet payload length.
    // https://gopro.github.io/OpenGoPro/docs/ble/control/#set-shutter
    const uint8_t packet[]={0x03,0x01,0x01,static_cast<uint8_t>(on?1:0)};
    const uint32_t before=shutterResponses.load();
    if(!command->writeValue(packet,sizeof(packet),true)){
        log("%s write failed error=%d",label,client->getLastError());return;
    }
    log("%s write succeeded",label);
    for(unsigned i=0;i<30 && linked && shutterResponses==before;++i)vTaskDelay(pdMS_TO_TICKS(100));
    if(shutterResponses==before)log("%s response not observed: %s",label,linked?"timeout":"disconnected");
    // No automatic command retry, reconnect, or inferred recording state.
}
bool GoProCamera::controlUsable(){
    return state==State::Ready && linked && secured && hardwareReady && client && client->isConnected();
}
bool GoProCamera::loadPreset(const String& id,int32_t preset){
    if(working || !controlUsable() || preset<0 || id!=activeCameraId())return false;
    requestedPreset=preset;
    return launch(Job::Preset);
}
bool GoProCamera::readPreset(){
    if(working || !controlUsable())return false;
    return launch(Job::ReadPreset);
}
void GoProCamera::runPreset(){
    const int32_t preset=requestedPreset;
    auto* service=client?client->getService(SERVICE):nullptr;
    auto* command=service?service->getCharacteristic(gpUuid(0x72)):nullptr;
    if(!command){log("Preset %ld: command characteristic unavailable",(long)preset);return;}
    // Load Preset: command 0x40, 4-byte big-endian preset ID. Response is {0x40,status}.
    const uint8_t packet[]={0x06,0x40,0x04,(uint8_t)(preset>>24),(uint8_t)(preset>>16),(uint8_t)(preset>>8),(uint8_t)preset};
    // The camera refuses presets while busy (e.g. just after connecting), so retry a few times.
    for(unsigned attempt=1;attempt<=3 && controlUsable();++attempt){
        const uint32_t before=presetResponses.load();
        if(!command->writeValue(packet,sizeof(packet),true)){log("Preset %ld write failed",(long)preset);return;}
        for(unsigned i=0;i<30 && linked && presetResponses==before;++i)vTaskDelay(pdMS_TO_TICKS(100));
        if(presetResponses==before){log("Preset %ld: no response",(long)preset);continue;}
        const uint8_t status=presetStatus.load();
        log("Preset %ld load status=%u (0=success) attempt %u",(long)preset,status,attempt);
        if(status==0){activePreset=preset;return;}
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}
void GoProCamera::runReadPreset(){
    auto* service=client?client->getService(SERVICE):nullptr;
    auto* query=service?service->getCharacteristic(gpUuid(0x76)):nullptr;
    if(!query){log("Preset read: query characteristic unavailable");return;}
    // Get Status 0x13 for status 97 (active preset ID); decoded in decodeStatus().
    const uint8_t request[]={0x02,0x13,0x61};
    if(!query->writeValue(request,sizeof(request),true))log("Preset read write failed");
    // Let that reply arrive before the next query: replies share one reassembly buffer.
    vTaskDelay(pdMS_TO_TICKS(250));
    // Get Setting Values 0x12 for video resolution (2), frame rate (3) and lens (121).
    const uint8_t settingsRequest[]={0x04,0x12,2,3,121};
    if(!query->writeValue(settingsRequest,sizeof(settingsRequest),true))log("Video mode read write failed");
}
void GoProCamera::runForget(){
    const bool current=matches(removal,savedAddress,savedType);
    if(current){reconnect=false;retryAllowed=false;}
    if(current && client && client->isConnected()){
        client->disconnect();
        for(unsigned i=0;i<30 && linked;++i)vTaskDelay(pdMS_TO_TICKS(100));
        if(linked){log("Forget failed: disconnect timeout");state=State::Failed;return;}
    }
    if(protectedAddress.equalsIgnoreCase(removal.address)){log("Refused protected bond deletion");return;}
    NimBLEAddress address(removal.address,removal.type);
    if(NimBLEDevice::isBonded(address) && !NimBLEDevice::deleteBond(address)){
        log("GoPro bond deletion failed");return;
    }
    auto cards=savedSnapshot();
    for(size_t i=0;i<cards.count;++i)if(savedId(cards.cameras[i])==savedId(removal)){
        for(size_t j=i+1;j<cards.count;++j)cards.cameras[j-1]=cards.cameras[j];
        cards.cameras[--cards.count]=SavedCamera{};break;
    }
    if(!persistSaved(cards))return;
    if(current){
        prefs.remove("address");prefs.remove("type");prefs.remove("identity");prefs.remove("idtype");
        savedAddress="";identityAddress="";secured=false;bonded=false;state=State::Offline;
        portENTER_CRITICAL(&mux);activeAddress[0]=0;portEXIT_CRITICAL(&mux);
    }
    log("Saved GoPro removed; other cameras and bonds untouched");
}
// Official Open GoPro: register statuses 10 (encoding), 70 (battery %),
// 35 (remaining video seconds). 0x53 includes initial values; 0x93 pushes changes.
void GoProCamera::clearStatus(){
    portENTER_CRITICAL(&mux);telemetry=Snapshot{};statusPending=false;portEXIT_CRITICAL(&mux);
}
String GoProCamera::activeCameraId(){
    char address[18];uint8_t type;
    portENTER_CRITICAL(&mux);memcpy(address,activeAddress,sizeof(address));type=activeType;portEXIT_CRITICAL(&mux);
    return address[0]?String(type)+":"+address:String("");
}
GoProCamera::Snapshot GoProCamera::snapshot(){
    Snapshot copy;
    portENTER_CRITICAL(&mux);copy=telemetry;const bool pending=statusPending;portEXIT_CRITICAL(&mux);
    copy.connected=linked;copy.controlReady=linked && secured && hardwareReady;
    const State current=state.load();
    copy.connecting=current==State::Connecting || current==State::Pairing ||
        current==State::Connected || (current==State::Failed && retryAllowed) || pending;
    copy.stateError=copy.stateError || (current==State::Failed && !retryAllowed);
    if(!copy.connected){copy.recording=Recording::Unknown;copy.batteryPercent=-1;copy.mediaKnown=false;}
    return copy;
}
void GoProCamera::startStatus(NimBLERemoteCharacteristic* query){
    // No writes to saved-camera state. Assembly is reset by the BLE disconnect callback.
    portENTER_CRITICAL(&mux);statusPending=true;statusDeadline=millis()+4000;portEXIT_CRITICAL(&mux);
    const uint8_t request[]={0x04,0x53,0x0A,0x46,0x23};
    log("Status registration requested: recording, battery, media");
    if(!query->writeValue(request,sizeof(request),true))statusFault("registration write failed");
}
void GoProCamera::statusFault(const char* reason,const uint8_t* data,size_t len){
    queryFragmentPending=false;
    portENTER_CRITICAL(&mux);
    telemetry.recording=Recording::Unknown;telemetry.batteryPercent=-1;telemetry.mediaKnown=false;
    telemetry.stateError=true;telemetry.statusRegistered=false;statusPending=false;
    const bool report=!statusFaultLogged || millis()-lastStatusFault>=10000;
    if(report){lastStatusFault=millis();statusFaultLogged=true;}
    portEXIT_CRITICAL(&mux);
    if(report){
        char prefix[25]{};
        for(size_t i=0;i<len && i<8;++i)snprintf(prefix+i*3,4,"%02X ",data[i]);
        log("Status error: %s len=%u %s",reason,(unsigned)len,prefix);
    }
}
void GoProCamera::queryResponse(const uint8_t* data,size_t len){
    if(!linked)return;
    if(!len){statusFault("empty packet");return;}
    size_t header=1;
    if(data[0]&0x80){
        if(!queryFragmentPending || !queryRemaining || (data[0]&15)!=querySequence){queryRemaining=0;statusFault("fragment sequence",data,len);return;}
        querySequence=(querySequence+1)&15;
    }else{
        const unsigned kind=(data[0]>>5)&3;
        queryFragmentPending=false;
        queryReceived=0;querySequence=0;queryRemaining=0;
        if(kind==0)queryRemaining=data[0]&31;
        else if(kind==1 && len>=2){header=2;queryRemaining=((data[0]&31)<<8)|data[1];}
        else if(kind==2 && len>=3){header=3;queryRemaining=(data[1]<<8)|data[2];}
        else{statusFault("packet header",data,len);return;}
        if(queryRemaining>sizeof(queryBuffer)){queryRemaining=0;statusFault("message too long",data,len);return;}
    }
    const size_t count=len-header;
    if(count>queryRemaining || queryReceived+count>sizeof(queryBuffer)){
        queryRemaining=0;statusFault("packet length",data,len);return;
    }
    memcpy(queryBuffer+queryReceived,data+header,count);queryReceived+=count;queryRemaining-=count;
    if(!queryRemaining){queryFragmentPending=false;decodeStatus(queryBuffer,queryReceived);}
    else{queryFragmentAt=millis();queryFragmentPending=true;}
}
void GoProCamera::decodeStatus(const uint8_t* data,size_t len){
    if(len<2){statusFault("short response",data,len);return;}
    if(data[0]==0x13){
        // One-shot active preset read; kept apart from the registered status stream.
        if(data[1]==0 && len>=8 && data[2]==97 && data[3]==4){
            const int32_t id=(int32_t)(((uint32_t)data[4]<<24)|((uint32_t)data[5]<<16)|((uint32_t)data[6]<<8)|data[7]);
            if(activePreset.exchange(id)!=id)log("Active preset ID: %ld",(long)id);
        }else log("Active preset read failed status=%u len=%u",data[1],(unsigned)len);
        return;
    }
    if(data[0]==0x12){
        // One-shot video mode read (setting values), also kept apart from the status stream.
        if(data[1]!=0){log("Video mode read failed status=%u",data[1]);return;}
        int16_t res=-1,fps=-1,lens=-1;
        for(size_t i=2;i+2<=len;){
            const uint8_t id=data[i],size=data[i+1];i+=2;
            if(!size||size>4||size>len-i)break;
            uint32_t v=0;for(uint8_t k=0;k<size;++k)v=(v<<8)|data[i+k];i+=size;
            if(id==2)res=(int16_t)v;else if(id==3)fps=(int16_t)v;else if(id==121)lens=(int16_t)v;
        }
        const bool changed=videoResolution.exchange(res)!=res|videoFps.exchange(fps)!=fps|videoLens.exchange(lens)!=lens;
        if(changed)log("Video mode: %s (res=%d fps=%d lens=%d)",videoModeText().c_str(),res,fps,lens);
        return;
    }
    if(data[0]!=0x53 && data[0]!=0x93){statusFault("unexpected query ID",data,len);return;}
    if(data[1]!=0){statusFault("query response status nonzero",data,len);return;}
    // Validate the complete TLV message before changing the visible state.
    Snapshot next;portENTER_CRITICAL(&mux);next=telemetry;portEXIT_CRITICAL(&mux);
    bool recSeen=false,batterySeen=false,mediaSeen=false;
    for(size_t i=2;i<len;){
        if(len-i<2){statusFault("truncated TLV",data,len);return;}
        const uint8_t id=data[i++],size=data[i++];
        if(size>len-i){statusFault("truncated value",data,len);return;}
        if(id==10){
            if(size!=1 || data[i]>1){statusFault("encoding value",data,len);return;}
            next.recording=data[i]?Recording::Recording:Recording::Standby;recSeen=true;
        }else if(id==70){
            if(size!=1){statusFault("battery length",data,len);return;}
            next.batteryPercent=data[i]<=100?data[i]:-1;batterySeen=true;
        }else if(id==35){
            if(size!=4){statusFault("media length",data,len);return;}
            next.remainingSeconds=((uint32_t)data[i]<<24)|((uint32_t)data[i+1]<<16)|((uint32_t)data[i+2]<<8)|data[i+3];
            next.mediaKnown=true;mediaSeen=true;
        }else{statusFault("unrequested status ID",data,len);return;}
        i+=size;
    }
    if(data[0]==0x53 && !(recSeen && batterySeen && mediaSeen)){
        statusFault("incomplete registration",data,len);return;
    }
    Snapshot previous;
    portENTER_CRITICAL(&mux);
    previous=telemetry;
    if(data[0]==0x53){next.statusRegistered=true;statusPending=false;}
    next.stateError=!next.statusRegistered;
    if(linked)telemetry=next;
    portEXIT_CRITICAL(&mux);
    if(data[0]==0x53)log("Recording/battery/media status registration established");
    if(recSeen && next.recording!=previous.recording)log("Recording state: %s",next.recording==Recording::Recording?"REC":"STBY");
    if(batterySeen && (next.batteryPercent!=previous.batteryPercent || data[0]==0x53))log("Battery: %d%% (-1=unknown)",next.batteryPercent);
    if(mediaSeen && (!previous.mediaKnown || next.remainingSeconds/60!=previous.remainingSeconds/60))
        log("Media remaining: %lu min",(unsigned long)(next.remainingSeconds/60));
}
void GoProCamera::response(const uint8_t* data,size_t len){
    if(!len)return;
    size_t header=1;
    if(data[0]&0x80){
        if(!responseRemaining || (data[0]&15)!=responseSequence){responseRemaining=0;return;}
        responseSequence=(responseSequence+1)&15;
    }else{
        const unsigned kind=(data[0]>>5)&3;
        if(kind==0)responseRemaining=data[0]&31;
        else if(kind==1 && len>=2){header=2;responseRemaining=((data[0]&31)<<8)|data[1];}
        else if(kind==2 && len>=3){header=3;responseRemaining=(data[1]<<8)|data[2];}
        else{responseRemaining=0;return;}
        responseReceived=0;responseSequence=0;
    }
    const size_t count=len-header;
    if(count>responseRemaining){responseRemaining=0;return;}
    for(size_t i=0;i<count && responseReceived+i<2;++i)responsePrefix[responseReceived+i]=data[header+i];
    responseReceived+=count;responseRemaining-=count;
    if(!responseRemaining && responseReceived>=2 && responsePrefix[0]==0x3C){
        log("Hardware Info response status=%u",responsePrefix[1]);
        if(responsePrefix[1]==0)hardwareReady=true;
    }
    if(!responseRemaining && responseReceived>=2 && responsePrefix[0]==0x01){
        // Response has command ID and status, but no on/off echo or transaction ID.
        // Keep this label generic so a delayed response cannot be mislabelled REC/STOP.
        log("Shutter response status=%u (0=success 1=error 2=invalid parameter)",responsePrefix[1]);
        ++shutterResponses;
    }
    if(!responseRemaining && responseReceived>=2 && responsePrefix[0]==0x40){
        presetStatus=responsePrefix[1];
        ++presetResponses;
    }
}
void GoProCamera::Callbacks::onConnect(NimBLEClient*){owner->linked=true;owner->log("BLE connected");}
void GoProCamera::Callbacks::onDisconnect(NimBLEClient*,int reason){
    owner->linked=false;owner->secured=false;owner->bonded=false;owner->hardwareReady=false;owner->activePreset=-1;owner->videoResolution=-1;owner->videoFps=-1;owner->videoLens=-1;
    owner->clearStatus();
    owner->queryRemaining=owner->queryReceived=0;owner->queryFragmentPending=false;
    if(owner->state!=State::Failed)owner->state=State::Offline;
    owner->log("Disconnected reason=%d (0x%X)",reason,reason);
    if(owner->retryAllowed){owner->retryAt=millis()+5000;owner->reconnect=true;}
}
void GoProCamera::Callbacks::onAuthenticationComplete(NimBLEConnInfo& info){
    owner->log("Authentication encrypted=%d bonded=%d",info.isEncrypted(),info.isBonded());
}
int GoProCamera::StoreCallbacks::onStoreStatus(struct ble_store_status_event*,void*){
    owner->log("Bond store full/error: refusing eviction of existing bonds");
    return BLE_HS_ESTORE_CAP;
}
void GoProCamera::log(const char* format,...){
    Entry entry{};va_list args;va_start(args,format);vsnprintf(entry.text,sizeof(entry.text),format,args);va_end(args);
    for(char* p=entry.text;*p;++p)if((uint8_t)*p<32)*p=' ';
    portENTER_CRITICAL(&mux);
    entry.ms=millis();entry.sequence=++totalLog;entries[nextLog]=entry;nextLog=(nextLog+1)%LOG_SIZE;
    if(logCount<LOG_SIZE)++logCount;
    portEXIT_CRITICAL(&mux);
}
String GoProCamera::connectionLog(){
    std::unique_ptr<Entry[]> copy(new(std::nothrow) Entry[LOG_SIZE]);
    if(!copy)return "Log unavailable: memory allocation failed";
    size_t count,next;uint32_t total;
    portENTER_CRITICAL(&mux);memcpy(copy.get(),entries,sizeof(entries));count=logCount;next=nextLog;total=totalLog;portEXIT_CRITICAL(&mux);
    String out="GOPRO CONNECTION TEST\nLatest 64 events since boot. Reboot clears log.\nOverwritten: "+String(total-count)+"\n";
    for(size_t i=0;i<count;++i){const Entry& e=copy[(next+LOG_SIZE-count+i)%LOG_SIZE];out+="#"+String(e.sequence)+" ["+String(e.ms)+" ms] "+e.text+"\n";}
    return out+"\n"+statusJson();
}
String GoProCamera::escape(const char* value){
    String out;
    for(const uint8_t* p=(const uint8_t*)value;*p;++p){
        if(*p=='"' || *p=='\\')out+='\\';
        if(*p<32){out+=' ';continue;}
        out+=(char)*p;
    }
    return out;
}
// Value tables from the Open GoPro spec (settings 2, 3 and 121).
static const char* goProResolutionName(int v){
    switch(v){case 1:return "4K";case 4:return "2.7K";case 6:return "2.7K 4:3";case 7:return "1440";case 9:return "1080";case 12:return "720";
    case 18:return "4K 4:3";case 21:return "5.6K";case 24:return "5K";case 25:return "5K 4:3";case 26:return "5.3K 8:7";case 27:return "5.3K 4:3";
    case 28:return "4K 8:7";case 31:return "8K";case 35:return "5.3K 21:9";case 36:return "4K 21:9";case 37:return "4K 1:1";case 38:return "900";
    case 39:return "4K SPH";case 100:return "5.3K";case 107:return "5.3K 8:7";case 108:return "4K 8:7";case 109:return "4K 9:16";
    case 110:return "1080 9:16";case 111:return "2.7K 4:3";case 112:return "4K 4:3";case 113:return "5.3K 4:3";default:return "";}
}
static int goProFps(int v){
    switch(v){case 0:return 240;case 1:return 120;case 2:return 100;case 3:return 90;case 5:return 60;case 6:return 50;case 8:return 30;
    case 9:return 25;case 10:return 24;case 13:return 200;case 15:return 400;case 16:return 360;case 17:return 300;default:return 0;}
}
static const char* goProLensName(int v){
    switch(v){case 0:return "WIDE";case 2:return "NARROW";case 3:return "SUPERVIEW";case 4:return "LINEAR";case 7:return "MAX SV";
    case 8:return "LINEAR HL";case 9:return "HYPERVIEW";case 10:return "LINEAR LOCK";case 11:return "MAX HV";case 12:return "ULTRA SV";
    case 13:return "ULTRA WIDE";case 14:return "ULTRA LINEAR";case 104:return "ULTRA HV";default:return "";}
}
String GoProCamera::videoModeText() const{
    String out=goProResolutionName(videoResolution.load());
    const int fps=goProFps(videoFps.load());
    if(fps){if(out.length())out+=' ';out+=String(fps);}
    const char* lens=goProLensName(videoLens.load());
    if(*lens){if(out.length())out+=' ';out+=lens;}
    return out;
}
String GoProCamera::statusJson(){
    static const char* names[]={"Offline","Scanning","Connecting","Pairing","Connected","Control ready","Connection failed"};
    String out="{\"status\":\""+String(names[(unsigned)state.load()])+"\",\"busy\":"+(working?String("true"):String("false"))+",\"connected\":"+(linked?String("true"):String("false"))+",\"bonded\":"+(bonded?String("true"):String("false"))+",\"encrypted\":"+(secured?String("true"):String("false"))+",\"preset\":"+String((long)activePreset.load())+",\"videoMode\":\""+videoModeText()+"\""+",\"cameras\":[";
    const auto values=snapshot();
    const auto cards=savedSnapshot();
    char active[18];uint8_t type;
    portENTER_CRITICAL(&mux);memcpy(active,activeAddress,sizeof(active));type=activeType;portEXIT_CRITICAL(&mux);
    const bool connected=linked.load(), encrypted=secured.load(), hasBond=bonded.load();
    Found copy[MAX_FOUND];size_t count;
    portENTER_CRITICAL(&mux);memcpy(copy,found,sizeof(found));count=foundCount;portEXIT_CRITICAL(&mux);
    for(size_t i=0;i<count;++i){
        bool known=false;
        for(size_t j=0;j<cards.count;++j)if(matches(cards.cameras[j],copy[i].address,copy[i].type))known=true;
        if(i)out+=',';
        out+="{\"name\":\""+escape(copy[i].name)+"\",\"address\":\""+String(copy[i].address)+"\",\"index\":"+String(i)+",\"saved\":"+(known?String("true"):String("false"))+"}";
    }
    out+="],\"savedCameras\":[";
    for(size_t i=0;i<cards.count;++i){
        const auto& card=cards.cameras[i];
        const bool activeCard=matches(card,active,type), online=activeCard && connected;
        if(i)out+=',';
        out+="{\"id\":\""+savedId(card)+"\",\"address\":\""+String(card.address)+"\",\"reportedName\":\""+escape(card.reportedName)+"\",\"friendlyName\":\""+escape(card.friendlyName)+"\",\"connected\":"+(online?String("true"):String("false"));
        out+=",\"bonded\":"+(online&&hasBond?String("true"):String("false"))+",\"encrypted\":"+(online&&encrypted?String("true"):String("false"))+",\"controlReady\":"+(online&&encrypted&&hardwareReady?String("true"):String("false"));
        out+=",\"recordingState\":\""+String(online?(values.recording==Recording::Recording?"recording":values.recording==Recording::Standby?"standby":"unknown"):"unknown")+"\",\"batteryPercent\":"+String(online?values.batteryPercent:-1)+",\"remainingSeconds\":"+(online&&values.mediaKnown?String((unsigned long)values.remainingSeconds):String("null"))+"}";
    }
    return out+"],\"statusRegistered\":"+String(values.statusRegistered?"true":"false")+",\"stateError\":"+String(values.stateError?"true":"false")+",\"savedListError\":"+(savedListError?String("true"):String("false"))+"}";
}
