#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <atomic>

// Open GoPro connection and manual shutter test controls. Independent of Blackmagic/ICameraBackend.
class GoProCamera {
public:
    enum class Recording { Unknown, Standby, Recording };
    struct Snapshot {
        Recording recording=Recording::Unknown;
        int batteryPercent=-1;
        uint32_t remainingSeconds=0;
        bool mediaKnown=false, connected=false, controlReady=false;
        bool connecting=false, stateError=false, statusRegistered=false;
    };
    Snapshot snapshot();
    String activeCameraId();
    GoProCamera();
    void begin(const String& blackmagicAddress);
    void loop();
    bool scan();
    bool connectDiscovered(unsigned index);
    bool forget(const String& id);
    bool connectSaved(const String& id);
    bool renameSaved(const String& id, const String& name);
    bool setShutter(const String& id, bool on);
    bool loadPreset(const String& id, int32_t preset);
    bool readPreset();
    int32_t currentPreset() const { return activePreset.load(); }
    bool busy() const { return working.load(); }
    String statusJson();
    String connectionLog();
private:
    enum class State { Offline, Scanning, Connecting, Pairing, Connected, Ready, Failed };
    enum class Job { Scan, Connect, Forget, Rec, Stop, Preset, ReadPreset };
    struct Found { char name[64]; char address[18]; uint8_t type; };
    struct SavedCamera {
        char address[18]{};
        uint8_t type=0;
        char advertisedAddress[18]{};
        uint8_t advertisedType=0;
        char reportedName[64]{};
        char friendlyName[49]{};
    };
    static constexpr size_t MAX_SAVED=8;
    struct SavedList {
        uint32_t version=1;
        uint32_t count=0;
        SavedCamera cameras[MAX_SAVED]{};
    } savedList;
    SavedCamera removal{};
    char activeAddress[18]{};
    uint8_t activeType=0;
    std::atomic<bool> savedListAvailable{true}, savedListError{false};
    struct Entry { uint32_t ms, sequence; char text[120]; };
    static constexpr size_t LOG_SIZE=64, MAX_FOUND=16;
    Entry entries[LOG_SIZE]{};
    Found found[MAX_FOUND]{};
    size_t nextLog=0, logCount=0, foundCount=0;
    uint32_t totalLog=0;
    portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
    std::atomic<State> state{State::Offline};
    std::atomic<bool> working{false}, linked{false}, secured{false}, bonded{false};
    std::atomic<bool> reconnect{false}, retryAllowed{false}, hardwareReady{false};
    std::atomic<uint32_t> retryAt{0};
    Job job=Job::Scan;
    Preferences prefs;
    bool storageReady=false;
    NimBLEClient* client=nullptr;
    String savedAddress, identityAddress, protectedAddress;
    uint8_t savedType=0, identityType=0;
    Found requested{};
    // Command-response packet assembly, accessed only by the BLE notification callback.
    size_t responseRemaining=0, responseReceived=0;
    uint8_t responsePrefix[2]{}, responseSequence=0;
    bool launch(Job value);
    static void task(void* context);
    void loadSaved();
    SavedList savedSnapshot();
    bool persistSaved(const SavedList& value);
    bool findSaved(const String& id, SavedCamera& value);
    void rememberPaired(const String& address, uint8_t type, const Found& discovered);
    bool prepareConnect();
    static bool matches(const SavedCamera& value, const String& address, uint8_t type);
    static String savedId(const SavedCamera& value);
    void runScan();
    void runConnect();
    void runForget();
    void runShutter(bool on);
    std::atomic<uint32_t> shutterResponses{0}, presetResponses{0};
    std::atomic<uint8_t> presetStatus{0xFF};
    std::atomic<int32_t> activePreset{-1};
    int32_t requestedPreset=-1;
    bool controlUsable();
    void runPreset();
    void runReadPreset();
    void fail(const char* reason, bool retry=false);
    void log(const char* format, ...);
    void response(const uint8_t* data, size_t len);
    void startStatus(NimBLERemoteCharacteristic* query);
    void queryResponse(const uint8_t* data, size_t len);
    void decodeStatus(const uint8_t* data, size_t len);
    void statusFault(const char* reason, const uint8_t* data=nullptr, size_t len=0);
    void clearStatus();
    Snapshot telemetry;
    bool statusPending=false;
    uint32_t statusDeadline=0, lastStatusFault=0;
    bool statusFaultLogged=false;
    uint8_t queryBuffer[64]{}, querySequence=0;
    size_t queryRemaining=0, queryReceived=0;
    std::atomic<bool> queryFragmentPending{false};
    std::atomic<uint32_t> queryFragmentAt{0};
    static String escape(const char* value);
    class Callbacks : public NimBLEClientCallbacks {
    public:
        explicit Callbacks(GoProCamera* owner):owner(owner){}
        void onConnect(NimBLEClient*) override;
        void onDisconnect(NimBLEClient*, int reason) override;
        void onAuthenticationComplete(NimBLEConnInfo&) override;
    private: GoProCamera* owner;
    } callbacks;
    class StoreCallbacks : public NimBLEDeviceCallbacks {
    public:
        explicit StoreCallbacks(GoProCamera* owner):owner(owner){}
        int onStoreStatus(struct ble_store_status_event*, void*) override;
    private: GoProCamera* owner;
    } storeCallbacks;
};
