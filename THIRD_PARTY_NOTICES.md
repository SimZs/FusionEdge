# Third-Party Notices

FusionEdge is derived from and includes code from several open-source projects. Copyright notices in the individual source files remain in effect.

## Core projects

| Component | Upstream | License |
| --- | --- | --- |
| yoRadio | https://github.com/e2002/yoradio | GNU GPL v3.0 |
| ESP32-audioI2S | https://github.com/schreibfaul1/ESP32-audioI2S | GNU GPL v3.0 |
| ESPAsyncWebServer / AsyncTCP | https://github.com/ESP32Async/ESPAsyncWebServer | GNU LGPL v3.0 |
| IRremoteESP8266 | https://github.com/crankyoldgit/IRremoteESP8266 | GNU LGPL v2.1 |
| async-mqtt-client | https://github.com/marvinroger/async-mqtt-client | MIT |
| OneButton | https://github.com/mathertel/OneButton | BSD-style license |
| ai-esp32-rotary-encoder-derived code | https://github.com/igorantolic/ai-esp32-rotary-encoder | GNU GPL v2.0 |

## Audio codecs

The bundled ESP32-audioI2S tree contains codec implementations with their own notices, including FAAD2, Helix MP3, FLAC, Opus and Vorbis code. Those notices are preserved in `src/audioI2S`. In particular, the FAAD2 sources require the following acknowledgement:

> Code from FAAD2 is copyright (c) Nero AG, www.nero.com

## PlatformIO dependencies

The build downloads additional libraries declared in `platformio.ini`, including LovyanGFX, RTClib, Adafruit BusIO and Adafruit NeoPixel. These dependencies are not part of this repository and remain under their respective upstream licenses.

## License texts

The FusionEdge license is provided in the repository root. License copies for the bundled third-party components are stored in the `licenses` directory. Source-level copyright and attribution notices bundled with this project must not be removed.
