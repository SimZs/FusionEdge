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

## Weather units

Weather values use metric units by default: Celsius, hPa and km/h. To display
Fahrenheit, inHg and mph, select `LANGUAGE EN` and define `IMPERIALUNIT` in
`myoptions.h`. A build-time check prevents this option from being combined
with locale files that currently contain metric-only weather format strings.

The `IMPERIALUNIT` weather-unit option was reintroduced by Adam Navrowski.

## Cassette screensaver

Define `USE_CASSETTE_SCREENSAVER` in `myoptions.h` to enable the animated
vintage cassette. In the WebUI, enable the **While playing** screensaver, set
its timeout in seconds, and leave its **Blank screen** option disabled. During
playback the cassette shows the current artist and track title; without stream
metadata it falls back to the station name and a `PLAYING` label. The lower
label displays the available codec, bitrate, sample-rate and bit-depth
information. The normal idle screensaver remains the large clock, while
**Blank screen** continues to switch the display off.

The cassette idea and original animation prototype were contributed by Adam
Navrowski. The FusionEdge implementation redraws only the two reel areas during
animation to keep display traffic low. The supplied background is stored at
`/images/screensaver/retro_audio_cassette.png`; `CASSETTE_PNG_PATH` can point
to a replacement image with the same 456 x 291 pixel layout. Blue/pink and
red/cream alternatives are supplied as `retro_audio_cassette_blue.png` and
`retro_audio_cassette_red.png` in the same directory.
`CASSETTE_FRAME_MS` can optionally set the reel refresh interval; the default
is 100 ms. If the PNG or its PSRAM-backed sprite cannot be loaded, FusionEdge
falls back to the code-drawn cassette.

## Display note

Some ST7796 panels require the **Invert display** WebUI option for correct colors. This is a panel/controller variation and is currently a known hardware-specific setting.

When an ST7796, XPT2046 and SD card share the same SPI bus, some modules may show display artifacts during SD playback. Other tested display/touch combinations do not exhibit this behavior.

For the Guition JC3248W535 with its integrated AXS15231B QSPI display and touch
controller, use the tested configuration in
[AXS15231B_options](AXS15231B_options/README.md). The final AXS15231B driver
stabilization and hardware testing were completed by Tibor Botfai.

For the 4D Systems gen4-ESP32-35CT with its integrated ILI9488 display,
FocalTech capacitive touch controller and microSD slot, use the configuration
in [gen4-ESP32-35CT_options](gen4-ESP32-35CT_options/README.md). It uses the
same ILI9488 driver as regular SPI modules, with board-specific pins and bus
settings supplied through `myoptions.h`.

To add logos for your own stations, see the
[custom station icon guide](data/images/stations/README.md).

## Project history and credits

- [yoRadio](https://github.com/e2002/yoradio) by e2002 and contributors
- [VTomRadio](https://github.com/VaraiTamas/VTomRadio/tree/main) LovyanGFX/LittleFS yoRadio variant by VTom
- [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) by schreibfaul1
- Final AXS15231B driver stabilization and hardware testing by Tibor Botfai
- 4D Systems gen4-ESP32-35CT board support by Révész Tamás
- Reintroduction of `IMPERIALUNIT` weather-unit support by Adam Navrowski
- Cassette screensaver concept and original prototype by Adam Navrowski
- FusionEdge development by SimZs and contributors

Bundled and external components retain their own copyrights and licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

FusionEdge is distributed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
