<p align="center">
<img width="1536" height="1024" alt="Fusion_EDGE" src="https://github.com/user-attachments/assets/ac32ea6a-53a3-4aed-b5e3-69afc1bff3bf" />
</p>

# FusionEdge

FusionEdge is an ESP32-S3 internet radio firmware built on the yoRadio codebase. It uses LovyanGFX and LittleFS and adds a 480x320 touch-oriented interface, spectrum and waveform visualizations, optional Bluetooth audio input, SD and DLNA playback, WebUI control, encoders, weather information and theme support.

The project grew from the LovyanGFX/LittleFS-based [VTomRadio](https://github.com/VaraiTamas/VTomRadio/tree/main) variant. FusionEdge keeps the original yoRadio playback architecture while substantially extending the display, controls, audio visualization and source handling.

## Project status

FusionEdge is now feature-complete. Version 1.0.5 was the final planned feature
release; subsequent updates focus on confirmed bug fixes and compatibility
maintenance.

### Version 1.0.6

- Stabilized DLNA browser initialization, category loading, playlist import and
  switching between DLNA and WEB playlists
- Improved HTTPS stream reliability by coordinating CoverArt, DLNA and audio
  network operations and reducing internal-memory pressure
- Fetches the largest available Last.fm album image directly, with MusicBrainz
  and Cover Art Archive retained as fallbacks
- Gives stream changes priority over background cover downloads, preventing a
  new station name from appearing while the previous station keeps playing
- Improved stream-stall and Wi-Fi reconnection recovery while preserving the
  user's playback intent
- Fixed WebUI file-serving races that could crash `player.html` or leave the
  display locked after an interrupted request
- Fixed automatic backlight fading, including invalid legacy settings and a
  zero fade-step value
- Prevented the built-in LED and NeoPixel strip from competing for GPIO48
- Added an optional ESP32-WROVER-E N16R8 PlatformIO configuration template

### Version 1.0.5

- Optional current-track album art from Last.fm, MusicBrainz and Cover Art
  Archive for WEB, SD, DLNA and Bluetooth playback
- A more reliable asynchronous DLNA WebUI browser with configured root-folder
  support and paged directory listings
- Correct source icons and mode handling when switching between DLNA, WEB and
  Bluetooth playback
- Serialized, low-priority cover and DLNA network work to preserve audio
  playback memory and watchdog responsiveness

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

An additional classic ESP32-WROVER-E N16R8 template is available as
`platformio_esp32-wrover-n16r8.ini`. It requires matching classic-ESP32 pin
definitions in `myoptions.h`.

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

## DLNA browser

When `USE_DLNA` is enabled, set `dlnaHost` to the media server address and
`dlnaIDX` to the preferred root object ID in `myoptions.h`. The DLNA page in the
WebUI opens at that configured root, lists folders and tracks, and can build or
append entries to the playback list.

Directory browsing runs through a background worker instead of the web-server
callback. This keeps SOAP requests away from the AsyncTCP task and makes larger
or slower media-server libraries considerably more reliable. The WebUI and
firmware endpoints are version-dependent, so upload the v1.0.6 LittleFS data
along with the firmware when upgrading from an earlier release.

## Last.fm album art

Current-track album art can optionally replace the station/source icon in WEB,
SD, DLNA and Bluetooth modes. Create a Last.fm API account, then add the
following to `myoptions.h`:

```cpp
#define USE_LASTFM_COVER
#define LASTFM_API_KEY "your_lastfm_api_key"
```

The lookup runs in a low-priority background task and only starts when metadata
contains both an artist and a title in `Artist - Title` form. FusionEdge first
uses the largest album image returned by Last.fm `track.getInfo`. If Last.fm
does not provide a usable image, it uses the album MusicBrainz ID or searches
MusicBrainz for a matching release group before checking Cover Art Archive.
Images are kept in PSRAM and are not written to LittleFS. While a cover is
unavailable, downloading or invalid, the normal station or source icon remains
visible.

Bluetooth metadata receives an additional fallback for video services. If the
QCC artist field contains a channel name and the exact lookup fails, FusionEdge
cleans the video title and resolves it with Last.fm `track.search`. Local files
still use their exact embedded artist and title metadata first.

Cover lookup uses plain HTTP and keeps its task stack and JSON allocations in
PSRAM. This is intentional: opening another TLS session can consume the
contiguous internal RAM required by HTTPS audio streams on the ESP32-S3. Only
the Last.fm API key is sent; do not add a Last.fm shared secret to the firmware.

This integration uses metadata from Last.fm and MusicBrainz, and cover art from
the [Cover Art Archive](https://musicbrainz.org/doc/Cover_Art_Archive). Review the
[Last.fm API terms](https://www.last.fm/api/tos), retain the required Last.fm
credit in redistributed builds, and respect the rights of artists and labels
when using Cover Art Archive images.

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
