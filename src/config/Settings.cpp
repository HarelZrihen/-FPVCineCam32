#include "Settings.h"

void SettingsStore::begin() { prefs.begin("fpvcinecam32", false); }

AppSettings SettingsStore::load() {
    AppSettings s;
    s.selectedCamera = prefs.getString("camsystem", "blackmagic");
    if (s.selectedCamera != "gopro" && s.selectedCamera != "dji") s.selectedCamera = "blackmagic";
    s.uartRxPin = prefs.getInt("rx", s.uartRxPin);
    s.uartTxPin = prefs.getInt("tx", s.uartTxPin);
    s.uartBaud = prefs.getUInt("baud", s.uartBaud);
    s.recordChannel = prefs.getInt("recch", s.recordChannel);
    s.recordThreshold = prefs.getInt("recthr", s.recordThreshold);
    s.recordActiveHigh = prefs.getBool("rechigh", s.recordActiveHigh);
    s.recordStopDelaySec = prefs.getInt("recdelay", s.recordStopDelaySec);
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
    if (value != "blackmagic" && value != "gopro" && value != "dji") return false;
    return prefs.putString("camsystem", value) > 0;
}
