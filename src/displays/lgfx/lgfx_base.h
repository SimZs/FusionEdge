#pragma once

#ifndef LGFX_USE_QSPI
#define LGFX_USE_QSPI
#endif

#include <LovyanGFX.hpp>

#include "../../core/options.h"

#ifndef LGFX_LCD_SPI_WRITE_FREQ
#if DSP_MODEL == DSP_AXS15231B
#define LGFX_LCD_SPI_WRITE_FREQ 40000000
#else
#define LGFX_LCD_SPI_WRITE_FREQ 27000000
#endif
#endif

#ifndef LGFX_LCD_SPI_READ_FREQ
#if DSP_MODEL == DSP_AXS15231B
#define LGFX_LCD_SPI_READ_FREQ 16000000
#else
#define LGFX_LCD_SPI_READ_FREQ 12000000
#endif
#endif

#if TS_MODEL == TS_MODEL_AXS15231B
#include "touch_axs15231b.h"
#endif

template <typename TPanel>
class LGFX_Base : public lgfx::LGFX_Device {
  TPanel _panel;
  lgfx::Bus_SPI _bus;

#if TS_MODEL == TS_MODEL_XPT2046
  lgfx::Touch_XPT2046 _touch;
#elif TS_MODEL == TS_MODEL_GT911
  lgfx::Touch_GT911 _touch;
#elif TS_MODEL == TS_MODEL_FT6X36
  lgfx::Touch_FT5x06 _touch;
#elif TS_MODEL == TS_MODEL_AXS15231B
  lgfx::Touch_AXS15231B _touch;
#endif

  const int16_t _panelWidth;
  const int16_t _panelHeight;
  const uint8_t _rotationOffset;

  template <typename TCfg>
  void applySpiHost(TCfg& cfg) {
    cfg.spi_host = static_cast<decltype(cfg.spi_host)>(LGFX_LCD_SPI_HOST);
  }

  void configureBus() {
    auto cfg = _bus.config();
    applySpiHost(cfg);
#if DSP_MODEL == DSP_AXS15231B
    cfg.pin_sclk = TFT_SCK;
    cfg.pin_mosi = -1;
    cfg.pin_miso = -1;
    cfg.pin_dc = -1;
    cfg.pin_io0 = TFT_D0;
    cfg.pin_io1 = TFT_D1;
    cfg.pin_io2 = TFT_D2;
    cfg.pin_io3 = TFT_D3;
    cfg.spi_3wire = false;
#else
    cfg.pin_sclk = 12;
    cfg.pin_mosi = 11;
    cfg.pin_miso = 13;
    cfg.use_lock = true;  // SPI bus mutex - fehér képernyő megelőzése (v0.1.2 fix)
    cfg.pin_dc = TFT_DC;
#endif
    cfg.freq_write = LGFX_LCD_SPI_WRITE_FREQ;
    cfg.freq_read = LGFX_LCD_SPI_READ_FREQ;
    _bus.config(cfg);
    _panel.setBus(&_bus);
  }

  void configurePanel() {
    auto cfg = _panel.config();
    cfg.pin_cs = TFT_CS;
    cfg.pin_rst = TFT_RST;
    cfg.panel_width = _panelWidth;
    cfg.panel_height = _panelHeight;
    cfg.offset_rotation = _rotationOffset;
    cfg.invert = true;
#if DSP_MODEL == DSP_AXS15231B
    cfg.memory_width = _panelWidth;
    cfg.memory_height = _panelHeight;
    cfg.invert = false;
#endif
    _panel.config(cfg);
  }

#if TS_MODEL != TS_MODEL_UNDEFINED
  void configureTouch() {
    auto cfg = _touch.config();

#if TS_MODEL == TS_MODEL_XPT2046
    cfg.pin_miso = 13;
    cfg.pin_cs = TS_CS;
    cfg.bus_shared = true;
    applySpiHost(cfg);
    cfg.freq = 2500000;
    cfg.offset_rotation = _rotationOffset;
#elif TS_MODEL == TS_MODEL_GT911 || TS_MODEL == TS_MODEL_FT6X36
    cfg.pin_sda = TS_SDA;
    cfg.pin_scl = TS_SCL;
    cfg.i2c_port = TS_I2C_PORT;
    cfg.i2c_addr = TS_I2C_ADDR;
    cfg.pin_int = (TS_INT == 255) ? -1 : TS_INT;
    cfg.pin_rst = (TS_RST == 255) ? -1 : TS_RST;
    cfg.x_min = 0;
    cfg.x_max = _panelWidth - 1;
    cfg.y_min = 0;
    cfg.y_max = _panelHeight - 1;
    cfg.offset_rotation = _rotationOffset;
    cfg.bus_shared = true;  // Wire is initialized in config.cpp on the same I2C port
#elif TS_MODEL == TS_MODEL_AXS15231B
    cfg.pin_sda = TS_SDA;
    cfg.pin_scl = TS_SCL;
    cfg.i2c_port = 1;
    cfg.i2c_addr = 0x3B;
    cfg.pin_int = TS_INT;
    cfg.pin_rst = TS_RST;
    cfg.x_min = 0;
    cfg.x_max = _panelWidth - 1;
    cfg.y_min = 0;
    cfg.y_max = _panelHeight - 1;
    cfg.offset_rotation = _rotationOffset;
    cfg.bus_shared = false;
#endif

    _touch.config(cfg);
    _panel.setTouch(&_touch);
  }
#endif

  public:
  LGFX_Base(int16_t panelWidth, int16_t panelHeight, uint8_t rotationOffset)
      : _panelWidth(panelWidth), _panelHeight(panelHeight), _rotationOffset(rotationOffset) {
    configureBus();
    configurePanel();
#if TS_MODEL != TS_MODEL_UNDEFINED
    configureTouch();
#endif
    setPanel(&_panel);
  }

#if DSP_MODEL == DSP_AXS15231B
  bool isFlushBusy() const {
    return _panel.isFlushBusy();
  }

  bool isFrameBusy() const {
    return _panel.isFrameBusy();
  }

  bool tryBeginFrameAccess() {
    return _panel.tryBeginFrameAccess();
  }

  void endFrameAccess() {
    _panel.endFrameAccess();
  }

  bool blitFrameBlock(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    return _panel.blitFrameBlock(x, y, w, h, pixels);
  }

  bool blitFrameBlockDeferred(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    return _panel.blitFrameBlockDeferred(x, y, w, h, pixels);
  }

  bool fillFrameBlockDeferred(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    return _panel.fillFrameBlockDeferred(x, y, w, h, color);
  }
#endif
};
