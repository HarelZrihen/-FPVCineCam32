#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct AppSettings {
    String selectedCamera = "blackmagic";
    int uartRxPin = 6;
    int uartTxPin = 7;
    uint32_t uartBaud = 115200;
    int recordChannel = 11;       // Betaflight channel number, 1-based
    int recordThreshold = 1500;
    bool recordActiveHigh = true;
    // GoPro preset switches: like Betaflight modes, a preset loads while its channel is inside [low, high].
    struct PresetRange { int channel; int low; int high; int32_t id; }; // channel 1-based
    static constexpr int MAX_PRESETS = 6;
    int presetCount = 0;
    PresetRange presets[MAX_PRESETS] = {};
    uint8_t osdSlot = 0;          // Custom Message 0..3
    String cameraAddress = "";
    uint8_t cameraAddressType = 0;
    bool autoConnect = true;
    bool wifiAutoOff = false;
};

class SettingsStore {
public:
    void begin();
    AppSettings load();
    void save(const AppSettings& s);
    void clearCamera();
    bool selectCamera(const String& value);
private:
    Preferences prefs;
};
