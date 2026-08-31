# Guition JC3248W535 / AXS15231B configuration

This directory contains a tested `myoptions.h` example for the Guition
JC3248W535 board with its integrated 480x320 AXS15231B QSPI display and touch
controller.

The final AXS15231B driver stabilization and long-duration hardware testing
were completed by Tibor Botfai.

## How to use it

1. Back up your current project-level `myoptions.h`.
2. Copy the supplied `myoptions.h` to the project root.
3. Review the language, encoder, audio DAC, SD card and optional feature
   settings before building.
4. Run a clean build, then compile and upload the firmware.
5. Upload the LittleFS data or configure Wi-Fi through the access point and use
   the Web Board uploader.

## Included hardware configuration

- AXS15231B QSPI display and integrated touch controller
- Guition JC3248W535 display, touch and backlight pins
- PCM5102A I2S audio output
- One rotary encoder
- SD card on a separate FSPI bus
- 16 MB flash / 8 MB PSRAM ESP32-S3 target

`USE_BUILTIN_LED` must remain disabled for this board. Check every pin against
your exact board revision before connecting external hardware.

The AXS15231B display stability fix is already included in the FusionEdge source
code. No additional driver files need to be copied when using this release.

## Display stability changes

The integrated AXS15231B driver uses a dedicated, coalescing background flush
task. The tested stability update:

- disables DMA for the physical QSPI frame transfer;
- waits for each 12,800-pixel staging block before reusing its buffer;
- limits physical full-frame transfers to one every 50 ms;
- disables the extra LovyanGFX automatic framebuffer display path;
- restores the complete CASET scan window before every frame transfer;
- leaves the general FusionEdge drawing and spectrum code unchanged.

These changes are compiled only for `DSP_AXS15231B`; SPI displays such as the
ILI9488 and ST7796 are not affected.

The AXS QSPI bus runs at 40 MHz for writes and 16 MHz for reads. Other supported
display types keep their existing 27 MHz write and 12 MHz read settings.

## Tested environment

- Guition JC3248W535
- ESP32-S3 with 16 MB flash and 8 MB OPI PSRAM
- Integrated AXS15231B display and touch controller
- Pioarduino `platform-espressif32` 55.03.37
- Arduino ESP32 framework 3.3.7
- LovyanGFX 1.2.25
- Spectrum visualization enabled during an overnight stability test
