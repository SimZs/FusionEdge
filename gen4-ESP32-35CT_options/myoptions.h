/* FusionEdge hardware configuration for the 4D Systems gen4-ESP32-35CT. */
#pragma once

#ifndef ARDUINO_ESP32S3_DEV
#define ARDUINO_ESP32S3_DEV
#endif

#define LANGUAGE HU
#define NAMEDAYS_FILE HU
#define CLOCK_TTS_LANGUAGE "hu"

#define USE_BUILTIN_LED false

/* Integrated 3.5-inch ILI9488 display. */
#define DSP_MODEL DSP_ILI9488
#define LGFX_LCD_SPI_HOST 1
#define LGFX_LCD_SPI_WRITE_FREQ 20000000
#define LGFX_LCD_SPI_READ_FREQ 16000000
#define LGFX_PANEL_INVERT false
#define LGFX_ROTATION 1

#define TFT_MISO -1
#define TFT_MOSI 13
#define TFT_SCK 14
#define TFT_CS 3
#define TFT_DC 21
#define TFT_RST 7

#define BRIGHTNESS_PIN 4

/* Integrated FocalTech capacitive touch controller. */
#define TS_MODEL TS_MODEL_FT6X36
#define TS_SDA 10
#define TS_SCL 9
#define TS_RST 11
#define TS_INT 8
#define TS_I2C_PORT 0
#define TS_I2C_ADDR 0x38
#define LGFX_TOUCH_BUS_SHARED false
#define LGFX_TOUCH_I2C_FREQ 400000

/* Integrated microSD slot. */
#define SDC_CS 1
#define SD_SPI_HOST HSPI
#define SD_SPIPINS 42, 41, 2
#define SDSPISPEED 4000000

/* External I2S DAC example. Review these pins before connecting hardware. */
#define I2S_DOUT 38
#define I2S_BCLK 39
#define I2S_LRC 40

#define EXT_WEATHER true

/* Optional controls and features. Enable only after assigning free GPIO pins. */
// #define ENC_BTNR 15
// #define ENC_BTNL 16
// #define ENC_BTNB 17
// #define ENC_INTERNALPULLUP true
// #define IR_PIN 18
// #define USE_BLUETOOTH
