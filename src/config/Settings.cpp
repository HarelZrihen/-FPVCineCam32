#include "Settings.h"

void SettingsStore::begin() { prefs.begin("fpvcinecam32", false); }

AppSettings SettingsStore::load() {
    AppSettings s;
    s.selectedCamera = prefs.getString("camsystem", "blackmagic");
    if (s.selectedCamera != "gopro" && s.selectedCamera != "dji" && s.selectedCamera != "dji2") s.selectedCamera = "blackmagic";
    s.uartRxPin = prefs.getInt("rx", s.uartRxPin);
    s.uartTxPin = prefs.getInt("tx", s.uartTxPin);
    s.uartBaud = prefs.getUInt("baud", s.uartBaud);
    s.recordChannel = prefs.getInt("recch", s.recordChannel);
    s.recordThreshold = prefs.getInt("recthr", s.recordThreshold);
    s.recordActiveHigh = prefs.getBool("rechigh", s.recordActiveHigh);
    s.recordStopDelaySec = prefs.getInt("recdelay", s.recordStopDelaySec);
    s.presetCount = constrain(prefs.getInt("pcount", 0), 0, AppSettings::MAX_PRESETS);
    for (int i = 0; i < s.presetCount; ++i) {
        auto& p = s.presets[i];
        const String n(i);
        p.channel = prefs.getInt(("pch" + n).c_str(), p.channel);
        p.low = prefs.getInt(("pmin" + n).c_str(), p.low);
        p.high = prefs.getInt(("pmax" + n).c_str(), p.high);
        p.id = prefs.getInt(("pid" + n).c_str(), p.id);
        AppSettings::setPresetName(p, prefs.getString(("pnm" + n).c_str(), ""));
    }
    s.osdSlot = prefs.getUChar("osdslot", s.osdSlot);
    s.cameraAddress = prefs.getString("camaddr", "");
    s.cameraAddressType = prefs.getUChar("camtype", 0);
    s.autoConnect = prefs.getBool("autoconn", true);
    s.wifiAutoOff = prefs.getBool("wifioff", false);
    return s;
}

void SettingsStore::save(const AppSettings& s) {
    prefs.putInt("rx", s.uartRxPin);
    prefs.putInt("tx", s.uartTxPin);
    prefs.putUInt("baud", s.uartBaud);
    prefs.putInt("recch", s.recordChannel);
    prefs.putInt("recthr", s.recordThreshold);
    prefs.putBool("rechigh", s.recordActiveHigh);
    prefs.putInt("recdelay", s.recordStopDelaySec);
    prefs.putInt("pcount", s.presetCount);
    for (int i = 0; i < s.presetCount; ++i) {
        const auto& p = s.presets[i];
        const String n(i);
        prefs.putInt(("pch" + n).c_str(), p.channel);
        prefs.putInt(("pmin" + n).c_str(), p.low);
        prefs.putInt(("pmax" + n).c_str(), p.high);
        prefs.putInt(("pid" + n).c_str(), p.id);
        prefs.putString(("pnm" + n).c_str(), p.name);
    }
    prefs.putUChar("osdslot", s.osdSlot);
    prefs.putString("camaddr", s.cameraAddress);
    prefs.putUChar("camtype", s.cameraAddressType);
    prefs.putBool("autoconn", s.autoConnect);
    prefs.putBool("wifioff", s.wifiAutoOff);
}

void SettingsStore::clearCamera() {
    prefs.remove("camaddr");
    prefs.remove("camtype");
}

bool SettingsStore::selectCamera(const String& value) {
    if (value != "blackmagic" && value != "gopro" && value != "dji" && value != "dji2") return false;
    return prefs.putString("camsystem", value) > 0;
}
