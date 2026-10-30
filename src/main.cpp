#include <Arduino.h>
#include <WiFi.h>
#include "config/Settings.h"
#include "msp/MspClient.h"
#include "camera/BlackmagicCamera.h"
#include "camera/GoProCamera.h"
#include "camera/DjiActionCamera.h"
#include "web/WebUi.h"

HardwareSerial FcSerial(1);
SettingsStore settingsStore;
AppSettings settings;
MspClient msp(FcSerial);
BlackmagicCamera camera;
GoProCamera* gopro = nullptr;
DjiActionCamera* dji = nullptr;
WebUi* web = nullptr;

static constexpr int ESP_RX_PIN = 6;
static constexpr int ESP_TX_PIN = 7;
static constexpr uint32_t MSP_BAUD = 115200;

static uint32_t lastRcRequest = 0, lastApiRequest = 0, lastOsdUpdate = 0, lastRecordAttempt = 0;
static bool recordMapInitialized = false;
static bool lastAppliedRecordState = false;
static bool recordStopPending = false;
static uint32_t recordOffSince = 0;
static bool lastControlReady = false;
static String lastGoProRcTarget;
static uint32_t wifiStartedAt = 0;
static constexpr uint32_t WIFI_SETUP_WINDOW_MS = 90000;
static uint32_t lastDiagLog = 0;
static bool lastDiagCamConnected = false;
static bool lastDiagControlReady = false;
static wifi_mode_t lastDiagWifiMode = WIFI_MODE_NULL;

static void diagLog(const char* msg) {
    Serial.printf("[%8lu ms] %s\n", (unsigned long)millis(), msg);
}

static void diagLogState(const char* reason) {
    const CameraState& c = camera.state();
    const wifi_mode_t mode = WiFi.getMode();
    Serial.printf("[%8lu ms] STATE %-12s wifiMode=%d apIP=%s stations=%u camConnected=%d controlReady=%d camStatus=\"%s\" heap=%u\n",
                  (unsigned long)millis(), reason, (int)mode,
                  WiFi.softAPIP().toString().c_str(),
                  (unsigned)WiFi.softAPgetStationNum(),
                  c.connected ? 1 : 0, c.controlReady ? 1 : 0,
                  c.status.c_str(), (unsigned)ESP.getFreeHeap());
}

static bool recordSwitchState(bool& valid) {
    const int idx = settings.recordChannel - 1;
    valid = idx >= 0 && (size_t)idx < msp.rcCount() && msp.rcFresh();
    if (!valid) return false;
    const uint16_t value = msp.rcValue((size_t)idx);
    return settings.recordActiveHigh ? value > settings.recordThreshold : value < settings.recordThreshold;
}

static String osdStatusText() {
    const CameraState& c = camera.state();
    if (!c.connected) return "CAM OFFLINE";
    if (camera.waitingForPasskey()) return "CAM ENTER PIN";
    if (c.recording) return "REC";
    if (c.controlReady || c.ready || c.paired) return "STBY";
    return "CAM WAIT";
}

static String osdMediaText() {
    const CameraState& c = camera.state();
    if (!c.connected) return "MEDIA --";
    if (c.mediaRemaining.length() && c.mediaRemaining != "--") return "MEDIA " + c.mediaRemaining;
    return "MEDIA --";
}

// Presentation only: GoPro packets are decoded in GoProCamera, never here.
static String goProStatusText(const GoProCamera::Snapshot& c) {
    if(c.stateError) return "CAM ERROR";
    if(c.connecting) return "CAM CONNECT";
    if(!c.connected) return "CAM OFF";
    if(!c.controlReady || c.recording==GoProCamera::Recording::Unknown) return "CAM ERROR";
    return c.recording==GoProCamera::Recording::Recording ? "CAM REC" : "CAM READY";
}
static String goProTimeText(uint32_t seconds) {
    char text[24];
    snprintf(text,sizeof(text),"%luH:%02lu",(unsigned long)(seconds/3600),(unsigned long)((seconds%3600)/60));
    return String(text);
}
static String goProMediaText(const GoProCamera::Snapshot& c) {
    const String battery=c.batteryPercent>=0?String(c.batteryPercent)+"%":String("--");
    const String media=c.mediaKnown?goProTimeText(c.remainingSeconds):String("--");
    return "BAT "+battery+" SD "+media;
}

static String djiStatusText(const CameraState& c) {
    if(!c.connected) return "CAM OFF";
    if(!c.controlReady) return "CAM CONNECT";
    return c.recording ? "CAM REC" : "CAM READY";
}
static String djiMediaText(const CameraState& c) {
    const String battery=c.batteryPercent>=0?String(c.batteryPercent)+"%":String("--");
    const String media=(c.mediaRemaining.length() && c.mediaRemaining!="--")?c.mediaRemaining:String("--");
    return "BAT "+battery+" SD "+media;
}

void setup() {
    Serial.begin(115200);
    delay(250);
    diagLog("BOOT: FPVCineCam32 v0.12.0");
    Serial.printf("[%8lu ms] resetReason=%d freeHeap=%u\n", (unsigned long)millis(), (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
    diagLog("SETTINGS: begin");
    settingsStore.begin();
    settings = settingsStore.load();
    Serial.printf("[%8lu ms] SETTINGS: autoConnect=%d savedCamera=%s\n", (unsigned long)millis(), settings.autoConnect ? 1 : 0, settings.cameraAddress.c_str());

    // ESP32-C3 SuperMini hardware profile. Keep these fixed so wiring is predictable.
    diagLog("MSP: begin UART6/7 @115200");
    msp.begin(ESP_RX_PIN, ESP_TX_PIN, MSP_BAUD);

    // Bring the setup AP up before starting BLE. Wi-Fi and BLE share the C3's
    // 2.4 GHz radio, so giving SoftAP a clean head start makes setup discovery
    // more predictable without changing the proven Blackmagic BLE control path.
    uint64_t mac = ESP.getEfuseMac();
    char ap[32]; snprintf(ap,sizeof(ap),"FPVCineCam32-%04X",(uint16_t)(mac&0xffff));
    if (settings.selectedCamera == "gopro") gopro = new GoProCamera();
    if (settings.selectedCamera == "dji") dji = new DjiActionCamera();
    web = new WebUi(settings,settingsStore,camera,msp,gopro,dji);
    Serial.printf("[%8lu ms] WIFI: starting SoftAP %s\n", (unsigned long)millis(), ap);
    web->begin(ap);
    wifiStartedAt = millis();
    diagLogState("after AP");
    delay(750);
    diagLogState("AP +750ms");

    if (settings.selectedCamera == "blackmagic") {
    diagLog("BLE: camera.begin");
    camera.begin();
    diagLog("BLE: initialized");
    camera.setSavedTarget(settings.cameraAddress, settings.cameraAddressType);

    if (settings.autoConnect && settings.cameraAddress.length()) {
        diagLog("BLE: queue saved camera reconnect");
        camera.connectTo(settings.cameraAddress,settings.cameraAddressType);
    } else {
        diagLog("BLE: no saved auto-connect target");
    }
    }
    if (gopro) gopro->begin(settings.cameraAddress);
    if (dji) dji->begin();
    msp.requestApiVersion();
    diagLogState("setup done");
}

void loop() {
    msp.loop();
    if (settings.selectedCamera == "blackmagic") camera.loop();
    if (gopro) gopro->loop();
    if (dji) dji->loop();
    if(web) web->loop();

    const uint32_t now=millis();

    // Diagnostic-only logging. No control behavior is changed from v0.10.
    const CameraState& diagCam = camera.state();
    const wifi_mode_t diagMode = WiFi.getMode();
    if (diagCam.connected != lastDiagCamConnected || diagCam.controlReady != lastDiagControlReady || diagMode != lastDiagWifiMode) {
        lastDiagCamConnected = diagCam.connected;
        lastDiagControlReady = diagCam.controlReady;
        lastDiagWifiMode = diagMode;
        diagLogState("state change");
    }
    if (now - lastDiagLog >= 5000) {
        lastDiagLog = now;
        diagLogState("periodic");
    }
    if(now-lastRcRequest>=100){ lastRcRequest=now; msp.requestRc(); }
    if(now-lastApiRequest>=5000){ lastApiRequest=now; msp.requestApiVersion(); }

    bool mappingValid = false;
    const bool switchOn = recordSwitchState(mappingValid);
    // Hold REC for the stop delay after the switch turns off. Turning it back on within the
    // delay just cancels the pending STOP, so the current clip continues instead of a new one.
    if (switchOn) recordStopPending = false;
    else if (mappingValid && !recordStopPending && lastAppliedRecordState) { recordStopPending = true; recordOffSince = now; }
    const bool holdingRec = recordStopPending && now - recordOffSince < (uint32_t)settings.recordStopDelaySec * 1000UL;
    const bool desiredRecordState = switchOn || holdingRec;
    const CameraState& c = camera.state();

    // When the camera control link comes back, re-apply the physical switch position once.
    // This avoids losing a REC/STOP state across camera reconnect/authentication.
    const bool useGoPro = settings.selectedCamera == "gopro" && gopro;
    const bool useDji = settings.selectedCamera == "dji" && dji;
    const bool controlReady = useGoPro ? gopro->snapshot().controlReady : (useDji ? dji->state().controlReady : c.controlReady);
    const String goProTarget = useGoPro ? gopro->activeCameraId() : String("");
    if (controlReady && (!lastControlReady || (useGoPro && goProTarget != lastGoProRcTarget))) recordMapInitialized = false;
    lastControlReady = controlReady;
    lastGoProRcTarget = goProTarget;

    if ((settings.selectedCamera == "blackmagic" || useGoPro || useDji) && mappingValid && controlReady) {
        const bool changed = !recordMapInitialized || desiredRecordState != lastAppliedRecordState;
        if (changed && now-lastRecordAttempt >= 300) {
            lastRecordAttempt = now;
            bool accepted=false;
            if(useGoPro) accepted=gopro->setShutter(goProTarget, desiredRecordState);
            else if(useDji) accepted=dji->setRecording(desiredRecordState);
            else accepted=camera.setRecording(desiredRecordState);
            if (accepted) {
                if (useGoPro) diagLog(desiredRecordState ? "RC -> GoPro REC" : "RC -> GoPro STOP");
                if (useDji) diagLog(desiredRecordState ? "RC -> DJI REC" : "RC -> DJI STOP");
                lastAppliedRecordState = desiredRecordState;
                recordMapInitialized = true;
            }
        }
    }

    if(now-lastOsdUpdate>=500){
        lastOsdUpdate=now;
        if(gopro){
            const auto state=gopro->snapshot();
            // Configurator Custom Message 1 and 2 use internal slots 0 and 1.
            msp.setCustomText(0,goProStatusText(state));
            msp.setCustomText(1,goProMediaText(state));
        }else if(dji){
            const CameraState& state=dji->state();
            // Use the same two operator-facing OSD slots as GoPro.
            msp.setCustomText(0,djiStatusText(state));
            msp.setCustomText(1,djiMediaText(state));
        }else{
        msp.setCustomText(settings.osdSlot, osdStatusText());
        // The next Custom Message slot carries decoded Pocket 4K remaining
        // record duration from category 9 / parameter 2 telemetry.
        if (settings.osdSlot < 3) msp.setCustomText(settings.osdSlot + 1, osdMediaText());
        }
    }

    // Setup Wi-Fi is temporary. If nobody joins the AP within 90 seconds,
    // shut Wi-Fi down and leave BLE + MSP + OSD running. If a phone/laptop is
    // connected, keep setup alive until it disconnects or the user presses
    // "Disable Wi-Fi now" in the configurator. Wi-Fi returns on every reboot.
    if (web && web->active() && (now - wifiStartedAt >= WIFI_SETUP_WINDOW_MS) && WiFi.softAPgetStationNum() == 0) {
        diagLog("WIFI: 90s idle timeout -> stop");
        web->stopWifi();
    }

    delay(2);
}
