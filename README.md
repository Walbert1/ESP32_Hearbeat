# Arduino Heartbeat

This project is a small ESP32-based radar display built around an HLK-LD2450 millimeter-wave presence sensor and a 4-inch TFT LCD. It collects movement data from the radar module, decodes the target frames, and renders a live radar-style visualization on the screen.

## What I use

### Hardware
- ESP32 Dev Module
  - Runs the firmware and handles UART + display communication.
- HLK-LD2450 mmWave radar sensor
  - Sends wireless-style target tracking data over UART.
  - Used to detect moving people/objects and read their X/Y position and speed.
- Hosyond 4.0" TFT display with ST7796S controller
  - 480x320 SPI display used as the main visualization panel.
  - Controlled by TFT_eSPI for drawing the radar grid and target dots.

### Software
- PlatformIO
  - Project build system and firmware upload workflow.
- Arduino framework
  - Runs on the ESP32.
- TFT_eSPI
  - High-performance display library configured for the ST7796S panel.
- Unity (PlatformIO test framework)
  - Used for on-device validation and diagnostics for the display and radar.

## Project purpose

The system reads radar target data, then plots the targets on a fixed radar map with a circular grid. The display updates in real time so you can see movement, direction, and approximate target position on the screen.

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

- `src/main.cpp` — main firmware for the ESP32 radar display
- `test/test_lcd/test_main.cpp` — LCD sanity checks and visual display tests
- `test/test_radar/test_main.cpp` — radar validation and waveform/baud diagnostics
- `platformio.ini` — PlatformIO board, framework, library, and TFT driver settings

## Build and run

From the project root:

```bash
pio run
pio run -t upload
```

If you want to run the tests locally through PlatformIO:

```bash
pio test
```

## Notes

- The display driver is configured for a specific ST7796S panel and uses TFT_eSPI custom setup flags.
- The radar module is configured to multi-target mode so it can report multiple moving objects.
- The firmware reads UART frames directly from the LD2450 and decodes the target positions before rendering them.

If you want, I can also add a more detailed hardware list, a wiring diagram, or a deeper component-by-component breakdown for the ESP32, radar sensor, and display.
