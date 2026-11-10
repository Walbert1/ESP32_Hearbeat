# ESP32 Heartbeat

This project is an ESP32-based radar display built around an HLK-LD2450 millimeter-wave presence sensor and a 4-inch TFT LCD. It reads motion data from the radar sensor, decodes the target frames, and shows a live heartbeat-style radar map on the display.

## What I use

### Hardware
- ESP32 Dev Module
  - The main controller for the project.
  - Handles UART communication with the radar sensor and SPI communication with the display.
- HLK-LD2450 mmWave radar sensor
  - Detects moving people or objects.
  - Reports target position and speed over serial UART.
- Hosyond 4.0" TFT display with ST7796S controller
  - 480x320 SPI display used for the live radar visualization.
  - Controlled with TFT_eSPI.

### Software
- PlatformIO
  - Build system and firmware upload workflow.
- Arduino framework
  - Used to program the ESP32.
- TFT_eSPI
  - Display library configured for the ST7796S panel.
- Unity (PlatformIO test framework)
  - Used for hardware checks and diagnostics.

## Project purpose

The ESP32 reads radar target data and renders those targets on a fixed radar-style map with circular grid rings. The display updates in real time so movement, direction, and target position are visible on screen.

## Hardware wiring used in this project

The project currently assumes the following wiring:

- HLK-LD2450 TX -> ESP32 GPIO16 (RX2)
- HLK-LD2450 RX -> ESP32 GPIO17 (TX2)
- HLK-LD2450 5V -> ESP32 VIN/5V
- HLK-LD2450 GND -> ESP32 GND

The display is configured for the ST7796S panel with these ESP32 SPI pins:

- CS = 15
- DC = 2
- RST = 4
- MOSI = 23
- MISO = 19
- SCK = 18

These settings are defined in the build flags in `platformio.ini`.

## Project layout

- `src/main.cpp` — main ESP32 firmware for the radar display
- `test/test_lcd/test_main.cpp` — LCD verification and visual display checks
- `test/test_radar/test_main.cpp` — radar diagnostics and validation
- `platformio.ini` — PlatformIO configuration for the ESP32, framework, and display settings

## Build and run

From the project root:

```bash
pio run
pio run -t upload
```

To run the tests:

```bash
pio test
```

## Notes

- The display driver is configured specifically for the ST7796S TFT panel.
- The radar module is configured for multi-target tracking.
- The ESP32 firmware reads UART frames from the LD2450 and decodes target X/Y coordinates before drawing them.

If you want, I can also add a more detailed hardware list, a wiring diagram, or a component-by-component breakdown for the ESP32, radar sensor, and display.
