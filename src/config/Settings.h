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
    struct PresetRange { int channel; int low; int high; int32_t id; char name[13]; }; // channel 1-based; name shown on OSD
    static constexpr int MAX_PRESETS = 6;
    int presetCount = 0;
    PresetRange presets[MAX_PRESETS] = {};
    // OSD-safe name: uppercase letters, digits, space and - . / : only, up to 12 characters.
    static void setPresetName(PresetRange& p, const String& value) {
        size_t n = 0;
        for (size_t i = 0; i < value.length() && n < sizeof(p.name) - 1; ++i) {
            const char c = toupper((unsigned char)value[i]);
            if (isalnum((unsigned char)c) || strchr(" -./:", c)) p.name[n++] = c;
        }
        p.name[n] = 0;
    }
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
