# ESP32-WROVER-E N16R8 (Experimental)

This directory contains an experimental FusionEdge build configuration for a
classic ESP32-WROVER-E module with 16 MB flash and 8 MB PSRAM. The primary and
fully supported FusionEdge target remains the ESP32-S3 N16R8.

## Installation

1. Back up the project-root `platformio.ini` and `myoptions.h` files.
2. Copy this directory's `platformio.ini` and `myoptions.h` to the project root.
3. Review every GPIO assignment in `myoptions.h` for your own board and wiring.
4. Build and upload the firmware, then upload the normal `data` directory as
   the LittleFS filesystem.
5. Use a serial monitor speed of `460800` baud.

The included pinout was tested with an SPI ILI9488 display, an external I2S DAC
and two encoders. Touch, SD, Bluetooth, DLNA and Last.fm CoverArt are disabled
in the example and require additional board-specific GPIO planning and testing.

## Important limitations

The classic ESP32 has substantially less usable internal DRAM for this workload
than the ESP32-S3. FusionEdge reserves internal memory for Wi-Fi, AsyncTCP,
LittleFS/VFS locks and audio DMA by moving medium-sized general allocations to
PSRAM on this target. This prevents the WebUI file-serving crash observed with
the default allocator, but it cannot remove the hardware limits.

Although an N16R8 module contains 8 MB PSRAM, the classic ESP32 normally exposes
only about 4 MB through the directly mapped heap used by Arduino applications.
The remaining capacity does not compensate for the limited internal DRAM needed
by DMA, TLS and system locks.

- The WebUI can become very slow while audio and visualization are active.
- HTTPS radio streams may fail because a TLS handshake needs a large contiguous
  internal-memory block.
- Enabling CoverArt, DLNA, Bluetooth or several visual features together is not
  recommended and has not been validated on this target.
- High display refresh rates increase contention with the Wi-Fi and AsyncTCP
  tasks. Disabling the cassette screensaver and selecting a lighter spectrum
  mode can help, but ESP32-S3 remains the recommended solution.

This profile is provided for experimentation and basic compatibility. It does
not promise feature or performance parity with the ESP32-S3 build.
