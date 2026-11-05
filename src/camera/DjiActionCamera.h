#pragma once
#include "CameraBackend.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

class DjiActionCamera : public ICameraBackend {
public:
    DjiActionCamera();
    const char* systemKey() const { return "dji"; }
    const char* systemName() const { return "DJI Osmo"; }
    bool isExperimental() const { return true; }
    void begin() override;
    void loop() override;
    bool startScan(String& jsonOut) override;
    bool connectTo(const String& address,uint8_t type) override;
    void disconnect() override;
    bool setRecording(bool on) override;
    bool toggleRecording() override { return setRecording(!camState.recording); }
    void forgetPairing() override;
    bool submitPasskey(uint32_t) override { return false; }
    bool waitingForPasskey() const override { return false; }
    const CameraState& state() const override { return camState; }
    // Presets are the camera's user modes: 0 = General, 1..5 = Custom modes C1..C5.
    bool requestPreset(int32_t userMode) override;
    int32_t currentPreset() const override { return userMode; }
    bool presetBusy() const override { return targetUserMode>=0; }
    void setSavedTarget(const String& address,uint8_t type) { savedAddress=address; savedAddressType=type; }
    String connectionLog() const { return logText; }
private:
    CameraState camState;
    Preferences prefs;
    NimBLEClient* client=nullptr;
    NimBLERemoteCharacteristic* notifyChar=nullptr;
    NimBLERemoteCharacteristic* writeChar=nullptr;
    String savedAddress,requestedAddress,logText;
    uint8_t savedAddressType=0,requestedAddressType=0;
    bool approvedBefore=false,reconnectWanted=false;
    volatile bool connectRequested=false,connectTaskRunning=false,pendingConnectionResponse=false;
    uint32_t nextReconnectMs=0,cameraDeviceId=0,remainingSeconds=0;
    uint16_t seq=0,pendingConnectionSeq=0;
    uint8_t rxBuf[512]{}; size_t rxLen=0;
    class ClientCallbacks:public NimBLEClientCallbacks{public:explicit ClientCallbacks(DjiActionCamera* x):o(x){}void onConnect(NimBLEClient*)override;void onDisconnect(NimBLEClient*,int reason)override;private:DjiActionCamera* o;} callbacks;
    static DjiActionCamera* instance;
    static void notifyCb(NimBLERemoteCharacteristic*,uint8_t*,size_t,bool);
    static void connectTaskThunk(void*);
    void performConnect(const String&,uint8_t);
    bool sendFrame(uint8_t set,uint8_t id,uint8_t type,const uint8_t* payload,size_t payloadLen,uint16_t fixedSeq=0);
    void sendConnectionRequest();
    void sendConnectionResponse(uint16_t incomingSeq);
    void subscribeStatus();
    void handleNotify(const uint8_t*,size_t);
    void parseFrames();
    void handleFrame(const uint8_t*,size_t);
    void setModel(uint32_t id);
    void updateVideoMode(uint8_t mode,uint8_t res,uint8_t fps,uint8_t eis);
    uint8_t lastCameraMode=0xFF;
    // Custom-mode switching by stepping the quick-switch (QS) key until the status reports it.
    int8_t userMode=-1,targetUserMode=-1;
    uint8_t nextModeFlag=0xFF,qsPresses=0,startCameraMode=0xFF;int8_t startUserMode=-1;
    uint32_t lastQsMs=0,lastStatusMs=0;
    void stepPreset(uint32_t now);
    void cancelPreset(const char* why);
    void log(const char* fmt,...);
    static uint16_t crc16(const uint8_t*,size_t);
    static uint32_t crc32(const uint8_t*,size_t);
    static uint16_t read16(const uint8_t*);
    static uint32_t read32(const uint8_t*);
};
