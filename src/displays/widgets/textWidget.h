#pragma once

#include "../../core/options.h"
#include <LovyanGFX.hpp>
#if DSP_MODEL != DSP_DUMMY
#    include "widgetsconfig.h"
#include "widget.h"
#    define CHARWIDTH  6
#    define CHARHEIGHT 8

class TextWidget : public Widget {
  public:
    TextWidget() {}
    TextWidget(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor, uint16_t bgcolor) { init(wconf, buffsize, uppercase, fgcolor, bgcolor); }
    ~TextWidget();
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor, uint16_t bgcolor);
    void setText(const char* txt);
    void setText(int val, const char* format);
    void setText(const char* txt, const char* format);
    bool uppercase() { return _uppercase; }

  protected:
    char*    _text = nullptr;
    char*    _oldtext = nullptr;
    bool     _uppercase = false;
    uint16_t _buffsize = 0, _textwidth = 0, _oldtextwidth = 0, _oldleft = 0, _textheight = 0;
    uint8_t  _charWidth = 0;

  protected:
    void     _draw();
    uint16_t _realLeft(bool w_fb = false);
    void     _charSize(uint8_t textsize, uint8_t& width, uint16_t& height);
};

#endif
