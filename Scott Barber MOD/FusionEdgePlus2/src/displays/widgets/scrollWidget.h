#pragma once

#include "../../core/options.h"
#include <LovyanGFX.hpp>
#if DSP_MODEL != DSP_DUMMY
#    include "widgetsconfig.h"
#    include "../display_select.h"
#    include "textWidget.h"
#    define CHARWIDTH  6
#    define CHARHEIGHT 8

class ScrollWidget : public TextWidget {
  public:
    ScrollWidget() {}
    ScrollWidget(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    ~ScrollWidget();
    using Widget::init;
    void init(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor);
    void loop();
    void setText(const char* txt);
    void setText(const char* txt, const char* format);
    void resetText(); // _oldtext cache törlése (short/long váltáskor)
    void setActive(bool act, bool clr);
    void setFontBySize(uint8_t size);
    static void releaseScrollOwner();
    void setColors(uint16_t fg, uint16_t bg) override;
    uint16_t textHeight() const { return _textheight; }

  private:
    enum ScrollState { SCROLL_WAIT_START, SCROLL_RUN };
    char*        _sep = nullptr;
    bool         _doscroll = false;
    uint8_t      _scrolldelta;
    uint16_t     _scrolltime;
    uint32_t     _scrolldelay;
    uint16_t     _startscrolldelay;
    LGFX_Sprite* _spr = nullptr;
#if DSP_MODEL == DSP_AXS15231B
    LGFX_Sprite* _stripSpr = nullptr;
    bool         _stripReady = false;
#endif
    int          _scrollOffset = 0;
    int          _fullWidth = 0;
    ScrollState  _scrollState = SCROLL_WAIT_START;
#if DSP_MODEL == DSP_AXS15231B
    uint32_t _diagWindowMs = 0;
    uint32_t _diagLastDrawMs = 0;
    uint32_t _diagSamples = 0;
    uint32_t _diagSumStepMs = 0;
    uint32_t _diagMaxStepMs = 0;
    uint32_t _diagMaxLateMs = 0;
    uint32_t _diagMissed = 0;
    uint32_t _diagBusySkips = 0;
    uint32_t _diagSumDrawUs = 0;
    uint32_t _diagMaxDrawUs = 0;
    uint32_t _diagStripFillUs = 0;
    uint32_t _diagStripCopyUs = 0;
    uint32_t _diagStripPushUs = 0;
    uint32_t _diagDirectBlits = 0;
#endif

    void _setTextParams();
    void _setTextParams(LGFX_Sprite* sprite);
#if DSP_MODEL == DSP_AXS15231B
    void _deleteStrip();
    bool _buildStrip();
    void _drawStrip();
#endif
    void _draw();
    void _clear();
    void _reset();
};
#endif
