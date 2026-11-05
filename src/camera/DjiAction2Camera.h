#pragma once
#include "CameraBackend.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

// DJI Osmo Action 2 and Osmo Nano speak legacy DUML (0x55 frames) over BLE, not the R SDK
// (0xAA frames) used by DjiActionCamera for Action 4 and newer.
class DjiAction2Camera : public ICameraBackend {
public:
    explicit DjiAction2Camera(bool osmoNano=false);
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
private:
    CameraState camState;
    Preferences prefs;
    NimBLEClient* client=nullptr;
    NimBLERemoteCharacteristic* pairChar=nullptr;
    NimBLERemoteCharacteristic* writeChar=nullptr;
    String savedAddress,requestedAddress;
    uint8_t savedAddressType=0,requestedAddressType=0;
    bool reconnectWanted=false;
    volatile bool connectRequested=false,connectTaskRunning=false,pinPending=false,approvalAckPending=false,sessionReady=false;
    volatile uint16_t approvalSeq=0;
    volatile uint32_t lastRxMs=0;
    uint32_t nextReconnectMs=0,pinAtMs=0,authStartMs=0,lastPollMs=0,pollCycle=0;
    uint16_t seq=1;
    uint8_t rxBuf[512]{}; size_t rxLen=0;
    // Osmo Nano: the camera pushes its state instead of answering polls.
    const bool nano;
    bool nanoInitPending=false; uint32_t nanoInitAtMs=0;
    uint8_t workMode=0xFF;  // 0 photo, 1 video, anything else = asleep/standby/playback
    uint32_t paramSubId=0x6A00;
    void sendNanoInit();
    void subscribeParam(const char* name);
    void handleNanoState(const uint8_t* p,size_t n);
    void handleParam(const uint8_t* p,size_t n);
    const char* modelName() const { return nano?"DJI Osmo Nano":"DJI Osmo Action 2"; }
    class ClientCallbacks:public NimBLEClientCallbacks{public:explicit ClientCallbacks(DjiAction2Camera* x):o(x){}void onConnect(NimBLEClient*)override;void onDisconnect(NimBLEClient*,int reason)override;private:DjiAction2Camera* o;} callbacks;
    static DjiAction2Camera* instance;
    static void notifyCb(NimBLERemoteCharacteristic*,uint8_t*,size_t,bool);
    static void connectTaskThunk(void*);
    void performConnect(const String&,uint8_t);
    bool sendFrame(uint8_t receiver,uint8_t flags,uint8_t set,uint8_t id,const uint8_t* payload,size_t payloadLen,int32_t fixedSeq=-1);
    void sendPairingPin();
    void handleNotify(const uint8_t*,size_t);
    void dropRx(size_t n);
    void handleFrame(const uint8_t*,size_t);
    void log(const char* fmt,...);
    static uint8_t crc8(const uint8_t*,size_t);
    static uint16_t crc16(const uint8_t*,size_t);
};
