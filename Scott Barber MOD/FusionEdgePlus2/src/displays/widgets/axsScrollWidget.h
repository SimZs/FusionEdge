#pragma once

#include "../../core/options.h"

#if DSP_MODEL == DSP_AXS15231B

#include <LovyanGFX.hpp>
#include "textWidget.h"
#include "widgetsconfig.h"

class AXSScrollWidget : public TextWidget {
  public:
    AXSScrollWidget() {}
    AXSScrollWidget(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    ~AXSScrollWidget();
    using Widget::init;
    void init(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    void loop() override;
    void setText(const char* txt);
    void setText(const char* txt, const char* format);
    void setFontBySize(uint8_t size);
    void setColors(uint16_t fg, uint16_t bg) override;
    uint16_t textHeight() const { return _textheight; }

  private:
    enum ScrollState { SCROLL_WAIT_START, SCROLL_RUN };

    char*        _sep = nullptr;
    bool         _doscroll = false;
    uint8_t      _scrolldelta = 1;
    uint16_t     _scrolltime = 16;
    uint16_t     _startscrolldelay = 0;
    uint32_t     _scrolldelay = 0;
    uint32_t     _lastStep = 0;
    LGFX_Sprite* _spr = nullptr;
    LGFX_Sprite* _stripSpr = nullptr;
    bool         _stripReady = false;
    int          _scrollOffset = 0;
    int          _fullWidth = 0;
    ScrollState  _scrollState = SCROLL_WAIT_START;

    void _setTextParams();
    void _setTextParams(LGFX_Sprite* sprite);
    void _deleteStrip();
    bool _buildStrip();
    void _drawStatic();
    void _drawStrip();
    void _drawFallback();
    void _draw() override;
    void _clear() override;
    void _reset() override;
};

#endif
