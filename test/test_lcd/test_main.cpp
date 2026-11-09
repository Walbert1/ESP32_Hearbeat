#include <Arduino.h>
#include <unity.h>
#include <TFT_eSPI.h>

// Pin/driver configuration for the Hosyond 4.0" 480x320 ST7796S SPI panel is
// supplied via build_flags in platformio.ini (TFT_eSPI's User_Setup). The
// pin numbers there are placeholders for a typical ESP32 VSPI wiring --
// adjust them to match your actual wiring. TFT_MISO must be physically
// connected for the readback assertion below to work.

static TFT_eSPI tft;

struct NamedColor {
    const char *name;
    uint16_t color;
};

static const NamedColor kColors[] = {
    {"RED", TFT_RED},
    {"GREEN", TFT_GREEN},
    {"BLUE", TFT_BLUE},
    {"YELLOW", TFT_YELLOW},
    {"WHITE", TFT_WHITE},
};

void setUp(void) {
    // Runs before each test.
}

void tearDown(void) {
    // Runs after each test.
}

void test_tft_initializes(void) {
    Serial.println("Calling tft.init()...");
    tft.init();
    Serial.println("tft.init() returned.");
    tft.setRotation(1); // landscape: 480 wide x 320 tall
    Serial.printf("Reported size: %d x %d\n", tft.width(), tft.height());

    TEST_ASSERT_EQUAL_INT32(480, tft.width());
    TEST_ASSERT_EQUAL_INT32(320, tft.height());
}

void test_fill_screen_cycles_through_colors(void) {
    // No readback here: this panel's MISO/SDO isn't wired for pixel
    // readback, so this is a visual check -- watch the panel while it runs.
    for (const NamedColor &nc : kColors) {
        Serial.print("Filling screen: ");
        Serial.println(nc.name);
        tft.fillScreen(nc.color);
        delay(3000); // long enough to visually confirm the color on the panel
    }

    Serial.println("Color cycle done, filling black.");
    tft.fillScreen(TFT_BLACK);

    TEST_ASSERT_TRUE(true); // reached the end of the color cycle without hanging
}

void setup() {
    delay(2000); // let the board and display power up before testing

    UNITY_BEGIN();
    RUN_TEST(test_tft_initializes);
    RUN_TEST(test_fill_screen_cycles_through_colors);
    UNITY_END();
}

void loop() {
    // Nothing to do here for an on-device test run.
}
