#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "../config/Settings.h"
#include "../camera/BlackmagicCamera.h"
#include "../camera/GoProCamera.h"
#include "../camera/DjiActionCamera.h"
#include "../msp/MspClient.h"

class WebUi {
public:
    WebUi(AppSettings& settings, SettingsStore& store, BlackmagicCamera& camera, MspClient& msp, GoProCamera* goPro, ICameraBackend* djiCamera)
      : s(settings), prefs(store), cam(camera), mspClient(msp), server(80), savedCamera(settings.selectedCamera), gp(goPro), dji(djiCamera) {}
    void begin(const String& apName);
    void loop();
    bool active() const { return running; }
    void stopWifi();
private:
    AppSettings& s; SettingsStore& prefs; BlackmagicCamera& cam; MspClient& mspClient;
    WebServer server; DNSServer dnsServer; bool running=false;
    bool stopRequested=false; uint32_t stopAtMs=0;
    String savedCamera;
    bool restartRequested=false; uint32_t restartAtMs=0;
    GoProCamera* gp;
    ICameraBackend* dji;
    bool goProAvailable();
    bool djiAvailable();
    bool cameraAvailable();
    void routes();
    String statusJson();
    static const char PAGE[] PROGMEM;
};
