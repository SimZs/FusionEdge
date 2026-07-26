<p align="center">
<img width="1536" height="1024" alt="Fusion_EDGE" src="https://github.com/user-attachments/assets/ac32ea6a-53a3-4aed-b5e3-69afc1bff3bf" />
</p>

# FusionEdge

FusionEdge is an ESP32-S3 internet radio firmware built on the yoRadio codebase. It uses LovyanGFX and LittleFS and adds a 480x320 touch-oriented interface, spectrum and waveform visualizations, optional Bluetooth audio input, SD and DLNA playback, WebUI control, encoders, weather information and theme support.

The project grew from the LovyanGFX/LittleFS-based [VTomRadio](https://github.com/VaraiTamas/VTomRadio/tree/main) variant. FusionEdge keeps the original yoRadio playback architecture while substantially extending the display, controls, audio visualization and source handling.

## Main target

- ESP32-S3 with PSRAM
- 480x320 ILI9486, ILI9488, ST7796 or AXS15231B display
- LovyanGFX display driver
- XPT2046 resistive or supported I2C capacitive touch controller
- External I2S DAC or amplifier
- Optional QCC5124EL Bluetooth module using UART AT commands and an I2S audio bridge
- Optional SD card, DLNA, MQTT, IR receiver, LED strip and RTC

Hardware pins and optional features are selected in `myoptions.h`. Bluetooth support is compiled only when `USE_BLUETOOTH` is defined.

## Build

The supplied `platformio.ini` targets an ESP32-S3 DevKitC-1 N16R8 board using the pioarduino Arduino core.

1. Install Visual Studio Code, PlatformIO, or PlatformIO Core.
2. Review `myoptions.h` and `mytheme.h` for your hardware.
3. Copy `data/data/wifi.example.csv` to `data/data/wifi.csv` and enter your
   Wi-Fi credentials, or configure Wi-Fi through the radio's access point.
4. Build and upload the firmware.
5. Upload the `data` directory as a LittleFS filesystem image.

`data/data/wifi.csv` is intentionally ignored by Git because it contains local
network credentials.

Typical PlatformIO commands:

```text
pio run
pio run --target upload
pio run --target uploadfs
pio device monitor
```

The serial monitor speed is `460800` baud.

## Bluetooth

The tested Bluetooth path uses a QCC5124EL module:

- UART AT control on the pins configured by `BT_UART_TX` and `BT_UART_RX`
- I2S input on `BT_I2S_BCK`, `BT_I2S_LRCK` and `BT_I2S_DATA`
- No MCLK connection is required by the tested module configuration
- Metadata, touch/encoder/WebUI mode switching and spectrum visualization are supported

The working bridge parameters are kept in `src/core/bluetooth_config.h`.

## Display note

Some ST7796 panels require the **Invert display** WebUI option for correct colors. This is a panel/controller variation and is currently a known hardware-specific setting.

When an ST7796, XPT2046 and SD card share the same SPI bus, some modules may show display artifacts during SD playback. Other tested display/touch combinations do not exhibit this behavior.

For the Guition JC3248W535 with its integrated AXS15231B QSPI display and touch
controller, use the tested configuration in
[AXS15231B_options](AXS15231B_options/README.md). The final AXS15231B driver
stabilization and hardware testing were completed by Tibor Botfai.

To add logos for your own stations, see the
[custom station icon guide](data/images/stations/README.md).

## Project history and credits

- [yoRadio](https://github.com/e2002/yoradio) by e2002 and contributors
- [VTomRadio](https://github.com/VaraiTamas/VTomRadio/tree/main) LovyanGFX/LittleFS yoRadio variant by VTom
- [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) by schreibfaul1
- Final AXS15231B driver stabilization and hardware testing by Tibor Botfai
- FusionEdge development by SimZs and contributors

Bundled and external components retain their own copyrights and licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

FusionEdge is distributed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
