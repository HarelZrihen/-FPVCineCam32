#pragma once
#include <Arduino.h>

struct CameraState {
    bool connected = false;
    bool paired = false;
    bool ready = false;
    bool recording = false;
    int batteryPercent = -1;
    int iso = -1;
    int whiteBalance = -1;
    float fps = 0.0f;
    String timecode = "--:--:--:--";
    String mediaRemaining = "--";
    String videoMode = "";  // e.g. "4K 60", empty when the camera does not report it
    int activeMediaSlot = 0;
    String mediaSlotRemaining[3] = {"--", "--", "--"};
    String incomingSubscription = "none";
    uint32_t incomingPackets = 0;
    String lastIncoming = "";
    String model = "";
    String protocolVersion = "";
    String status = "OFFLINE";
    String lastCommand = "";
    String lastWrite = "";
    bool controlReady = false;
};

class ICameraBackend {
public:
    virtual ~ICameraBackend() = default;
    virtual void begin() = 0;
    virtual void loop() = 0;
    virtual bool startScan(String& jsonOut) = 0;
    virtual bool connectTo(const String& address, uint8_t addressType) = 0;
    virtual void disconnect() = 0;
    virtual bool setRecording(bool on) = 0;
    virtual bool toggleRecording() = 0;
    virtual void forgetPairing() = 0;
    virtual bool submitPasskey(uint32_t pin) = 0;
    virtual bool waitingForPasskey() const = 0;
    virtual const CameraState& state() const = 0;
    // Optional preset switching (switch-driven, like the GoPro presets). The ID meaning is
    // backend-specific; backends without presets keep these defaults.
    virtual bool requestPreset(int32_t) { return false; }
    virtual int32_t currentPreset() const { return -1; }
    virtual bool presetBusy() const { return false; }
};
