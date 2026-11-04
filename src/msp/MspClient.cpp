#include "MspClient.h"

static constexpr uint8_t MSP_API_VERSION = 1;
static constexpr uint8_t MSP_STATUS = 101;
static constexpr uint8_t MSP_RC = 105;
static constexpr uint16_t MSP2_SET_TEXT = 0x3007;
static constexpr uint8_t MSP2TEXT_CUSTOM_MSG_0 = 7;
static constexpr uint32_t RC_TIMEOUT_MS = 120;

void MspClient::begin(int rxPin, int txPin, uint32_t baud) {
    port.begin(baud, SERIAL_8N1, rxPin, txPin);
    resetParser();
}

void MspClient::loop() {
    while (port.available()) parseByte((uint8_t)port.read());
    if (rcRequestPending && millis() - rcRequestSentMs > RC_TIMEOUT_MS) {
        rcRequestPending = false;
        rcTimeouts++;
    }
}

void MspClient::requestRc() {
    // Keep one RC request in flight. This gives meaningful timeout/latency stats
    // and avoids stacking requests if the FC UART is disconnected.
    if (rcRequestPending) return;
    rcRequestPending = true;
    rcRequestSentMs = millis();
    sendV1(MSP_RC);
}

void MspClient::requestApiVersion() { sendV1(MSP_API_VERSION); }
void MspClient::requestStatus() { sendV1(MSP_STATUS); }

bool MspClient::setCustomText(uint8_t slot, const String& textIn) {
    if (slot > 3) return false;
    String text = textIn;
    if (text.length() > 31) text.remove(31);
    uint8_t buf[34];
    buf[0] = MSP2TEXT_CUSTOM_MSG_0 + slot;
    buf[1] = (uint8_t)text.length();
    memcpy(&buf[2], text.c_str(), text.length());
    sendV2(MSP2_SET_TEXT, buf, text.length() + 2);
    return true;
}

void MspClient::sendV1(uint8_t cmd, const uint8_t* data, uint8_t len) {
    uint8_t csum = len ^ cmd;
    port.write('$'); port.write('M'); port.write('<');
    port.write(len); port.write(cmd);
    for (uint8_t i=0;i<len;i++) { port.write(data[i]); csum ^= data[i]; }
    port.write(csum);
}

void MspClient::sendV2(uint16_t cmd, const uint8_t* data, uint16_t len) {
    port.write('$'); port.write('X'); port.write('<');
    uint8_t hdr[5] = {0, (uint8_t)(cmd & 0xff), (uint8_t)(cmd >> 8), (uint8_t)(len & 0xff), (uint8_t)(len >> 8)};
    uint8_t c = 0;
    for (uint8_t b : hdr) { port.write(b); c = crc8DvbS2(c, b); }
    for (uint16_t i=0;i<len;i++) { port.write(data[i]); c = crc8DvbS2(c, data[i]); }
    port.write(c);
}

uint8_t MspClient::crc8DvbS2(uint8_t c, uint8_t a) {
    c ^= a;
    for (int i=0;i<8;i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0xD5) : (uint8_t)(c << 1);
    return c;
}

void MspClient::resetParser() {
    state = ParseState::IDLE; expected = index = 0; checksum = crc = 0; cmd16 = 0;
}

void MspClient::parseByte(uint8_t b) {
    switch (state) {
    case ParseState::IDLE:
        if (b == '$') state = ParseState::V1_DIR;
        break;
    case ParseState::V1_DIR:
        if (b == 'M') { proto = 1; state = ParseState::V1_SIZE; }
        else if (b == 'X') { proto = 2; state = ParseState::V2_DIR; }
        else { badFrames++; resetParser(); }
        break;
    case ParseState::V1_SIZE: // direction follows $M
        dir = b;
        if (dir != '>' && dir != '!') { badFrames++; resetParser(); break; }
        state = ParseState::V1_CMD; // then payload size
        break;
    case ParseState::V1_CMD:
        expected = b; checksum = b; state = ParseState::V1_PAYLOAD; index = 0;
        cmd8 = 0xff;
        if (expected > sizeof(payload)) { badFrames++; resetParser(); }
        break;
    case ParseState::V1_PAYLOAD:
        if (cmd8 == 0xff) {
            cmd8 = b; checksum ^= b;
            if (expected == 0) state = ParseState::V1_CSUM;
        } else if (index < expected) {
            payload[index++] = b; checksum ^= b;
            if (index >= expected) state = ParseState::V1_CSUM;
        }
        break;
    case ParseState::V1_CSUM:
        if (b == checksum && dir == '>') handleFrame(cmd8, payload, expected);
        else if (dir != '!') badFrames++;
        resetParser();
        break;
    case ParseState::V2_DIR:
        dir = b;
        if (dir != '>' && dir != '!') { badFrames++; resetParser(); break; }
        state = ParseState::V2_FLAGS;
        break;
    case ParseState::V2_FLAGS:
        flags = b; crc = crc8DvbS2(0, b); state = ParseState::V2_CMD1; break;
    case ParseState::V2_CMD1:
        cmd16 = b; crc = crc8DvbS2(crc,b); state = ParseState::V2_CMD2; break;
    case ParseState::V2_CMD2:
        cmd16 |= ((uint16_t)b << 8); crc = crc8DvbS2(crc,b); state = ParseState::V2_SIZE1; break;
    case ParseState::V2_SIZE1:
        expected = b; crc = crc8DvbS2(crc,b); state = ParseState::V2_SIZE2; break;
    case ParseState::V2_SIZE2:
        expected |= ((uint16_t)b << 8); crc = crc8DvbS2(crc,b); index = 0;
        if (expected > sizeof(payload)) { badFrames++; resetParser(); break; }
        state = expected ? ParseState::V2_PAYLOAD : ParseState::V2_CRC; break;
    case ParseState::V2_PAYLOAD:
        payload[index++] = b; crc = crc8DvbS2(crc,b);
        if (index >= expected) state = ParseState::V2_CRC;
        break;
    case ParseState::V2_CRC:
        if (b == crc && dir == '>') handleFrame(cmd16,payload,expected);
        else if (dir != '!') badFrames++;
        resetParser(); break;
    }
}

void MspClient::handleFrame(uint16_t cmd, const uint8_t* data, uint16_t len) {
    lastValidFrameMs = millis();
    if (cmd == MSP_API_VERSION && len >= 3) {
        apiMaj = data[1]; apiMin = data[2];
    } else if (cmd == MSP_STATUS && len >= 10) {
        // Bytes 6..9 are the flight mode flags; bit 0 is BOXARM, set only while actually armed.
        armedFlag = data[6] & 1;
        lastStatusFrameMs = millis();
    } else if (cmd == MSP_RC && len >= 2) {
        const size_t count = len / 2;
        cachedRcCount = min(count, (size_t)18);
        for (size_t i=0;i<cachedRcCount;i++) cachedRc[i] = data[i*2] | ((uint16_t)data[i*2+1] << 8);

        lastRcFrameMs = millis();
        rcResponses++;
        if (rcRequestPending) {
            rcLastResponseMs = millis() - rcRequestSentMs;
            rcRequestPending = false;
        }
        if (rcCallback) rcCallback(cachedRc, cachedRcCount);
    }
}
