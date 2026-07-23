#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY

#include "../../core/config.h"
#include "../display_select.h"
#include "eqWidget.h"

static constexpr uint16_t EQW = 50;
static constexpr uint16_t EQH = 25;
static constexpr uint8_t  EQR = 4;

void EqWidget::init(WidgetConfig wconf, uint16_t bgcolor) {
    Widget::init(wconf, 0, bgcolor);
    _bgcolor = bgcolor;
}

bool EqWidget::toggle() {
    _eqActive = !_eqActive;
    if (_active && !_locked) _drawIcon(_eqActive);
    return _eqActive;
}

void EqWidget::forceClose() {
    if (!_eqActive) return;
    _eqActive = false;
    if (_active && !_locked) _drawIcon(false);
}

void EqWidget::_drawIcon(bool active) {
    uint16_t x = _config.left;
    uint16_t y = _config.top;

    // mode 1 (inaktív): bg háttér, status_active keret + rajz
    // mode 2 (aktív):   status_active háttér, bg keret + rajz
    uint16_t bg  = active ? config.theme.status_active : _bgcolor;
    uint16_t fg  = active ? _bgcolor                   : config.theme.status_active;
    uint16_t brd = active ? _bgcolor                   : config.theme.status_active;

    dsp.fillRoundRect(x, y, EQW, EQH, EQR, bg);
    dsp.drawRoundRect(x, y, EQW, EQH, EQR, brd);

    // Sliders ikon — gap a bordertől: LX=6, RX=44 → 38px vonal
    const uint8_t LX  = 7;
    const uint8_t RX  = EQW - 7;     // 43
    const uint8_t LW  = RX - LX;     // 36px
    const uint8_t KR  = 3;

    const uint8_t rowY[3]  = { 7, 13, 19 };
    const uint8_t knobX[3] = { LX + 9, LX + 23, LX + 15 };

    for (uint8_t i = 0; i < 3; i++) {
        uint16_t ly = y + rowY[i];
        uint16_t kx = x + knobX[i];

        dsp.fillRect(x + LX, ly, LW, 1, fg);
        dsp.fillCircle(kx, ly, KR, bg);
        dsp.drawCircle(kx, ly, KR, fg);
    }
}

void EqWidget::_draw() {
    if (!_active) return;
    _drawIcon(_eqActive);
}

void EqWidget::_clear() {
    dsp.fillRect(_config.left, _config.top, EQW, EQH, _bgcolor);
}

#endif
