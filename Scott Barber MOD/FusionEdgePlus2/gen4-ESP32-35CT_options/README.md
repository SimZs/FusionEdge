# 4D Systems gen4-ESP32-35CT configuration

This directory contains a FusionEdge `myoptions.h` example for the 4D Systems
gen4-ESP32-35CT board with its integrated 320x480 ILI9488 display, FocalTech
capacitive touch controller and microSD slot.

FusionEdge support for this board was developed by Révész Tamás.

## Installation

1. Back up the `myoptions.h` file in the project root.
2. Copy `gen4-ESP32-35CT_options/myoptions.h` to the project root.
3. Review the language, audio DAC and optional control settings.
4. Run a clean build, then upload the firmware and LittleFS data.

No display driver files need to be replaced. The same FusionEdge source tree
supports both regular ILI9488 modules and the gen4-ESP32-35CT; the selected
`myoptions.h` supplies the board-specific configuration.

## Integrated hardware configuration

- ILI9488 SPI display: SCK 14, MOSI 13, DC 21, reset 7
- FocalTech touch controller: SDA 10, SCL 9, reset 11, interrupt 8, address 0x38
- Backlight: GPIO 4
- microSD: SCK 42, MISO 41, MOSI 2, CS 1
- ESP32-S3 with 16 MB flash and 8 MB OPI PSRAM

The display uses SPI2 with a 20 MHz write clock. The microSD slot uses the
separate SPI3 (`HSPI`) host, and the touch bus runs at 400 kHz. The example
leaves Bluetooth, infrared control and rotary encoders disabled because their
pins depend on the external hardware connected to the board.

Check the pin assignment against the exact board revision before connecting an
external DAC or controls. The example I2S pins are GPIO 38, 39 and 40.
