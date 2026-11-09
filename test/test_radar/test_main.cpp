#include <Arduino.h>
#include <unity.h>

// HLK-LD2450 wiring (confirmed):
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

// Speed uses the same offset-binary encoding, except the module reports a
// raw 0x0000 (not 0x8000, which is what "positive zero" would offset-decode
// to) when it has no reliable speed estimate yet -- treat that as 0 cm/s
// instead of letting it offset-decode to a nonsensical -32768.
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

void setUp(void) {
    // Runs before each test.
}

void tearDown(void) {
    // Runs after each test.
}

static const uint32_t kCandidateBauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800};

void test_radar_baud_sweep(void) {
    // Tries several common baud rates and reports how many bytes show up
    // at each one. Any nonzero count means real UART activity is reaching
    // this pin -- a baud rate problem, not a wiring/power problem. All
    // zero across every rate points back to wiring/power/pin mix-up.
    Serial.println("Baud rate sweep -- 1.5s per rate...");
    for (size_t i = 0; i < sizeof(kCandidateBauds) / sizeof(kCandidateBauds[0]); i++) {
        uint32_t baud = kCandidateBauds[i];
        RADAR_SERIAL.end();
        delay(50);
        RADAR_SERIAL.begin(baud, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
        delay(100); // let the UART peripheral settle at the new rate

        uint32_t start = millis();
        size_t byteCount = 0;
        uint8_t preview[16];
        size_t previewLen = 0;
        while (millis() - start < 1500) {
            if (RADAR_SERIAL.available()) {
                uint8_t b = (uint8_t)RADAR_SERIAL.read();
                if (previewLen < sizeof(preview)) {
                    preview[previewLen++] = b;
                }
                byteCount++;
            }
        }

        Serial.printf("  %7lu baud: %4u bytes", (unsigned long)baud, (unsigned)byteCount);
        if (previewLen > 0) {
            Serial.print("  first bytes: ");
            for (size_t j = 0; j < previewLen; j++) {
                Serial.printf("%02X ", preview[j]);
            }
        }
        Serial.println();
    }

    // Restore the documented operating baud rate for the rest of the suite.
    RADAR_SERIAL.end();
    delay(50);
    RADAR_SERIAL.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);

    TEST_ASSERT_TRUE(true); // diagnostic only -- read the printed table above
}

void test_radar_raw_byte_dump(void) {
    // Bypasses frame parsing entirely: just prints whatever raw bytes show
    // up on RADAR_SERIAL for a few seconds. If nothing prints here at all,
    // that's a wiring/power/TX-RX-swap problem, not a baud rate or
    // framing problem.
    Serial.println("Raw byte dump for 3 seconds...");
    uint32_t start = millis();
    size_t byteCount = 0;
    while (millis() - start < 3000) {
        if (RADAR_SERIAL.available()) {
            uint8_t b = (uint8_t)RADAR_SERIAL.read();
            Serial.printf("%02X ", b);
            byteCount++;
            if (byteCount % 16 == 0) {
                Serial.println();
            }
        }
    }
    Serial.println();
    Serial.printf("Total bytes received: %u\n", (unsigned)byteCount);

    TEST_ASSERT_TRUE(true); // diagnostic only -- read the printed byte count/dump above
}

void test_radar_sends_valid_frames(void) {
    RadarTarget targets[3];
    bool gotFrame = false;

    // Frames stream continuously (~10Hz typical); a few attempts is plenty.
    for (int attempt = 0; attempt < 5 && !gotFrame; attempt++) {
        gotFrame = readRadarFrame(targets, 2000);
    }

    TEST_ASSERT_TRUE_MESSAGE(gotFrame, "No valid LD2450 frame received -- check wiring/baud rate");
}

void test_radar_prints_live_targets(void) {
    RadarTarget targets[3];

    Serial.println("Reading 10 radar frames -- move in front of the sensor now...");
    for (int i = 0; i < 10; i++) {
        if (readRadarFrame(targets, 2000)) {
            bool anyPresent = false;
            for (int t = 0; t < 3; t++) {
                if (targets[t].present) {
                    anyPresent = true;
                    Serial.printf("  Target %d: x=%dmm y=%dmm speed=%dcm/s\n",
                                  t + 1, targets[t].x_mm, targets[t].y_mm, targets[t].speed_cms);
                }
            }
            if (!anyPresent) {
                Serial.println("  (frame OK, no targets detected)");
            }
        } else {
            Serial.println("  No frame received this round.");
        }
    }

    TEST_ASSERT_TRUE(true); // this test is for visual/Serial confirmation, not a hard pass/fail
}

void setup() {
    Serial.begin(115200);
    RADAR_SERIAL.begin(RADAR_BAUD, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);
    delay(500);

    UNITY_BEGIN();
    RUN_TEST(test_radar_baud_sweep);
    RUN_TEST(test_radar_raw_byte_dump);
    RUN_TEST(test_radar_sends_valid_frames);
    RUN_TEST(test_radar_prints_live_targets);
    UNITY_END();
}

void loop() {
    // Nothing to do here for an on-device test run.
}
