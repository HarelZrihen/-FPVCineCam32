#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <functional>

class MspClient {
public:
    using RcCallback = std::function<void(const uint16_t*, size_t)>;

    explicit MspClient(HardwareSerial& serial) : port(serial) {}
    void begin(int rxPin, int txPin, uint32_t baud);
    void loop();
    void requestRc();
    void requestApiVersion();
    void requestStatus();
    bool setCustomText(uint8_t slot, const String& text);

    bool connected() const { return lastValidFrameMs && (millis() - lastValidFrameMs < 2500); }
    bool rcFresh() const { return lastRcFrameMs && (millis() - lastRcFrameMs < 500); }
    // Betaflight's real armed state (MSP_STATUS flight mode bit 0 = ARM), not the arm switch.
    bool armedFresh() const { return lastStatusFrameMs && (millis() - lastStatusFrameMs < 1000); }
    bool armed() const { return armedFlag; }
    uint8_t apiMajor() const { return apiMaj; }
    uint8_t apiMinor() const { return apiMin; }
    void onRc(RcCallback cb) { rcCallback = cb; }

    size_t rcCount() const { return cachedRcCount; }
    uint16_t rcValue(size_t zeroBasedIndex) const {
        return zeroBasedIndex < cachedRcCount ? cachedRc[zeroBasedIndex] : 0;
    }
    uint32_t responses() const { return rcResponses; }
    uint32_t timeouts() const { return rcTimeouts; }
    uint32_t invalidFrames() const { return badFrames; }
    uint32_t lastResponseMs() const { return rcLastResponseMs; }

private:
    HardwareSerial& port;
    RcCallback rcCallback;
    uint32_t lastValidFrameMs = 0;
    uint32_t lastRcFrameMs = 0;
    uint32_t lastStatusFrameMs = 0;
    bool armedFlag = false;
    uint8_t apiMaj = 0, apiMin = 0;

    uint16_t cachedRc[18]{};
    size_t cachedRcCount = 0;
    bool rcRequestPending = false;
    uint32_t rcRequestSentMs = 0;
    uint32_t rcResponses = 0;
    uint32_t rcTimeouts = 0;
    uint32_t badFrames = 0;
    uint32_t rcLastResponseMs = 0;

    enum class ParseState { IDLE, V1_DIR, V1_SIZE, V1_CMD, V1_PAYLOAD, V1_CSUM,
                            V2_DIR, V2_FLAGS, V2_CMD1, V2_CMD2, V2_SIZE1, V2_SIZE2, V2_PAYLOAD, V2_CRC };
    ParseState state = ParseState::IDLE;
    uint8_t proto = 0, dir = 0, flags = 0, cmd8 = 0, checksum = 0, crc = 0;
    uint16_t cmd16 = 0, expected = 0, index = 0;
    uint8_t payload[128]{};

    void resetParser();
    void parseByte(uint8_t b);
    void handleFrame(uint16_t cmd, const uint8_t* data, uint16_t len);
    void sendV1(uint8_t cmd, const uint8_t* data = nullptr, uint8_t len = 0);
    void sendV2(uint16_t cmd, const uint8_t* data, uint16_t len);
    static uint8_t crc8DvbS2(uint8_t crc, uint8_t a);
};
