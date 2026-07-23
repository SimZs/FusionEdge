#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY

#include "../../core/config.h"
#include "../display_select.h"
#include "../../core/fonts.h"
#include "statusWidget.h"

// Egy jelző mérete (Fusion: W=50, H=26, gap=4)
static constexpr uint16_t SW = 50;
static constexpr uint16_t SH = 25;
static constexpr uint16_t SGAP = 8;
static constexpr uint8_t  SR = 4;   // sarok lekerekítés

void StatusWidget::init(WidgetConfig wconf, uint16_t bgcolor) {
    Widget::init(wconf, 0, bgcolor);
    _bgcolor = bgcolor;
}

void StatusWidget::setFade(bool enabled) {
    if (_fade == enabled) return;
    _fade = enabled;
    if (_active && !_locked && !config.isScreensaver) _drawDot(0, _fade ? 2 : 0, "FADE");
}

void StatusWidget::setTts(bool enabled, bool onlyNoStream) {
    if (_tts == enabled && _ttsExtra == onlyNoStream) return;
    _tts      = enabled;
    _ttsExtra = onlyNoStream;
    uint8_t mode = !_tts ? 0 : (_ttsExtra ? 2 : 1);
    if (_active && !_locked && !config.isScreensaver) _drawDot(1, mode, "TTS");
}

void StatusWidget::setRgb(bool enabled, bool duringScreensaver) {
    if (_rgb == enabled && _rgbExtra == duringScreensaver) return;
    _rgb      = enabled;
    _rgbExtra = duringScreensaver;
    uint8_t mode = !_rgb ? 0 : (_rgbExtra ? 2 : 1);
    if (_active && !_locked && !config.isScreensaver) _drawDot(2, mode, "RGB");
}

void StatusWidget::_drawDot(uint8_t idx, uint8_t mode, const char* label) {
    uint16_t x = _config.left + idx * (SW + SGAP);
    uint16_t y = _config.top;

    uint16_t bg, fg, brd;
    switch (mode) {
        case 2:  // Enabled + másik kapcsoló is aktív → kitöltött (eredeti active)
            bg  = config.theme.status_active;
            fg  = _bgcolor;
            brd = config.theme.status_active;
            break;
        case 1:  // Csak Enabled → üres box, foreground keret és szöveg
            bg  = _bgcolor;
            fg  = config.theme.status_active;
            brd = config.theme.status_active;
            break;
        default: // Disabled → üres box, inactive szín
            bg  = _bgcolor;
            fg  = config.theme.status_inactive;
            brd = config.theme.status_inactive;
            break;
    }

    // Háttér
    dsp.fillRoundRect(x, y, SW, SH, SR, bg);
    // Keret
    dsp.drawRoundRect(x, y, SW, SH, SR, brd);

    // Felirat
    uint8_t* fnt = vlwBySize(_config.textsize);
    if (fnt) { dsp.loadFont(fnt); } else { dsp.setTextSize(1); }
    dsp.setTextColor(fg, bg);
    dsp.setTextDatum(middle_center);
    dsp.drawString(label, x + SW / 2, y + SH / 2);
    if (fnt) dsp.unloadFont();
}

void StatusWidget::_draw() {
    if (!_active) return;
    _drawDot(0, _fade ? 2 : 0, "FADE");
    _drawDot(1, !_tts ? 0 : (_ttsExtra ? 2 : 1), "TTS");
    _drawDot(2, !_rgb ? 0 : (_rgbExtra ? 2 : 1), "RGB");
}

void StatusWidget::_clear() {
    uint16_t totalW = SW * 3 + SGAP * 2;
    dsp.fillRect(_config.left, _config.top, totalW, SH, _bgcolor);
}

#endif // DSP_MODEL != DSP_DUMMY
