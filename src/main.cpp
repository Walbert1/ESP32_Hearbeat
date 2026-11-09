#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>

// HLK-LD2450 wiring:
//   LD2450 TX  -> ESP32 GPIO16 (RX2)
//   LD2450 RX  -> ESP32 GPIO17 (TX2)
//   LD2450 5V  -> ESP32 VIN/5V
//   LD2450 GND -> ESP32 GND
#define RADAR_SERIAL Serial2
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17
#define RADAR_BAUD 256000

// LD2450 frame layout: AA FF 03 00 | target1(8) target2(8) target3(8) | 55 CC
static const uint8_t kFrameHeader[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t kFrameFooter[2] = {0x55, 0xCC};
static const size_t kFrameLength = 30;

struct RadarTarget {
    int16_t x_mm;
    int16_t y_mm;
    int16_t speed_cms;
    uint16_t distance_res;
    bool present;
};

// X/Y/speed are sign+magnitude, not offset-binary: bit15 is a sign flag
// (1 = positive), and the low 15 bits are the magnitude on their own --
// confirmed against the official manual's worked example, where a raw value
// with bit15 clear (782) decodes to -782, not 782 - 0x8000 (-31986).
static int16_t decodeSigned(uint8_t low, uint8_t high) {
    uint16_t raw = (uint16_t)low | ((uint16_t)high << 8);
    int16_t magnitude = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? magnitude : (int16_t)(-magnitude);
}

// Speed uses the same encoding, except a raw 0x0000 means "no reliable
// speed estimate yet" rather than offset-decoding to a bogus -32768.
static int16_t decodeSpeed(uint8_t low, uint8_t high) {
    if (low == 0 && high == 0) {
        return 0;
    }
    return decodeSigned(low, high);
}

// Looks for one full frame within timeoutMs. Returns true if a frame with a
// valid header and footer was read into targets[3].
static bool readRadarFrame(RadarTarget targets[3], uint32_t timeoutMs) {
    uint8_t frame[kFrameLength];
    uint32_t start = millis();

    size_t matched = 0;
    while (matched < 4 && (millis() - start) < timeoutMs) {
        if (RADAR_SERIAL.available()) {
            uint8_t b = (uint8_t)RADAR_SERIAL.read();
            if (b == kFrameHeader[matched]) {
                matched++;
            } else {
                matched = (b == kFrameHeader[0]) ? 1 : 0;
            }
        }
    }
    if (matched != 4) {
        return false;
    }
    memcpy(frame, kFrameHeader, 4);

    size_t idx = 4;
    while (idx < kFrameLength && (millis() - start) < timeoutMs) {
        if (RADAR_SERIAL.available()) {
            frame[idx++] = (uint8_t)RADAR_SERIAL.read();
        }
    }
    if (idx != kFrameLength) {
        return false;
    }

    if (frame[28] != kFrameFooter[0] || frame[29] != kFrameFooter[1]) {
        return false;
    }

    for (int t = 0; t < 3; t++) {
        const uint8_t *d = &frame[4 + t * 8];
        targets[t].x_mm = decodeSigned(d[0], d[1]);
        targets[t].y_mm = decodeSigned(d[2], d[3]);
        targets[t].speed_cms = decodeSpeed(d[4], d[5]);
        targets[t].distance_res = (uint16_t)d[6] | ((uint16_t)d[7] << 8);
        targets[t].present = !(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0);
    }

    return true;
}

static TFT_eSPI tft;

static const int32_t kScreenWidth = 480;
static const int32_t kScreenHeight = 320;
static const int32_t kOriginX = kScreenWidth / 2;
static const int32_t kOriginY = kScreenHeight - 10;
static const int32_t kMaxRangeMm = 6000;
static const float kPxPerMm = (float)(kScreenHeight - 20) / (float)kMaxRangeMm;

static uint16_t colorGrid;
static uint16_t colorTarget;

static void drawRadarGrid() {
    for (int32_t rangeMm = 1500; rangeMm <= kMaxRangeMm; rangeMm += 1500) {
        int32_t radiusPx = (int32_t)(rangeMm * kPxPerMm);
        tft.drawCircle(kOriginX, kOriginY, radiusPx, colorGrid);
    }

    static const float kAnglesDeg[] = {-60, -30, 0, 30, 60};
    for (float angleDeg : kAnglesDeg) {
        float angleRad = angleDeg * (float)M_PI / 180.0f;
        int32_t endX = kOriginX + (int32_t)(kMaxRangeMm * kPxPerMm * sinf(angleRad));
        int32_t endY = kOriginY - (int32_t)(kMaxRangeMm * kPxPerMm * cosf(angleRad));
        tft.drawLine(kOriginX, kOriginY, endX, endY, colorGrid);
    }

    tft.fillCircle(kOriginX, kOriginY, 4, colorGrid);
}

static void drawRadarBackground() {
    tft.fillScreen(TFT_BLACK);
    drawRadarGrid();
}

static void mapTargetToScreen(int16_t x_mm, int16_t y_mm, int32_t *screenX, int32_t *screenY) {
    *screenX = kOriginX + (int32_t)(x_mm * kPxPerMm);
    *screenY = kOriginY - (int32_t)(y_mm * kPxPerMm);
}

static const int32_t kTargetRadiusPx = 6;

static bool prevVisible[3] = {false, false, false};
static int32_t prevScreenX[3];
static int32_t prevScreenY[3];

// LD2450 config-command framing (distinct from the AA FF 03 00 data-report
// framing above): FD FC FB FA header, 04 03 02 01 footer. Sequence below
// enters config mode, forces multi-target tracking on, then exits config
// mode. Without this, some units ship defaulted to single-target tracking.
static const uint8_t kCmdEnableConfig[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
static const uint8_t kCmdMultiTargetMode[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x90, 0x00, 0x04, 0x03, 0x02, 0x01};
static const uint8_t kCmdEndConfig[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};

static void sendRadarCommand(const uint8_t *cmd, size_t len, const char *label) {
    while (RADAR_SERIAL.available()) {
        RADAR_SERIAL.read();
    }

    RADAR_SERIAL.write(cmd, len);
    RADAR_SERIAL.flush();

    Serial.printf("%s response: ", label);
    uint32_t start = millis();
    bool gotByte = false;
    while (millis() - start < 200) {
        if (RADAR_SERIAL.available()) {
            Serial.printf("%02X ", (uint8_t)RADAR_SERIAL.read());
            gotByte = true;
        }
    }
    Serial.println(gotByte ? "" : "(no response -- command may not have been received)");
}

static void enableMultiTargetTracking() {
    sendRadarCommand(kCmdEnableConfig, sizeof(kCmdEnableConfig), "EnableConfig");
    sendRadarCommand(kCmdMultiTargetMode, sizeof(kCmdMultiTargetMode), "MultiTargetMode");
    sendRadarCommand(kCmdEndConfig, sizeof(kCmdEndConfig), "EndConfig");
}

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);
    colorGrid = tft.color565(0, 170, 60);
    colorTarget = TFT_GREEN;

    drawRadarBackground();

    RADAR_SERIAL.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
    delay(1000);
    enableMultiTargetTracking();
}

void loop() {
    RadarTarget targets[3];
    if (!readRadarFrame(targets, 150)) {
        return;
    }

    // Fast-forward through any additional frames already buffered (e.g. if
    // rendering took longer than the radar's ~100ms report interval) so we
    // always act on the freshest data instead of drifting further behind.
    while (RADAR_SERIAL.available() >= (int)kFrameLength) {
        RadarTarget newerTargets[3];
        if (!readRadarFrame(newerTargets, 5)) {
            break;
        }
        memcpy(targets, newerTargets, sizeof(targets));
    }

    bool visible[3];
    int32_t screenX[3] = {0, 0, 0};
    int32_t screenY[3] = {0, 0, 0};
    bool anyErased = false;

    for (int t = 0; t < 3; t++) {
        visible[t] = targets[t].present;
        if (visible[t]) {
            mapTargetToScreen(targets[t].x_mm, targets[t].y_mm, &screenX[t], &screenY[t]);
            visible[t] = screenX[t] >= 0 && screenX[t] < kScreenWidth && screenY[t] >= 0 && screenY[t] < kScreenHeight;
        }

        if (prevVisible[t]) {
            tft.fillCircle(prevScreenX[t], prevScreenY[t], kTargetRadiusPx, TFT_BLACK);
            anyErased = true;
        }
    }

    // Erasing a dot also blacks out any grid ring/line underneath it, so
    // restore the (cheap, outline-only) grid before drawing new dot
    // positions on top -- otherwise moving dots leave a trail of gaps.
    if (anyErased) {
        drawRadarGrid();
    }

    for (int t = 0; t < 3; t++) {
        if (visible[t]) {
            tft.fillCircle(screenX[t], screenY[t], kTargetRadiusPx, colorTarget);
        }
        prevVisible[t] = visible[t];
        prevScreenX[t] = screenX[t];
        prevScreenY[t] = screenY[t];
    }
}
