#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY
#include "../../core/config.h"
#include "../display_select.h"
#include "widgets.h"
#include "../../core/fonts.h"

PlayModeWidget::~PlayModeWidget() {
    if (_spr) {
        _spr->deleteSprite();
        delete _spr;
        _spr = nullptr;
    }
}

void PlayModeWidget::init(BitrateBoxConfig boxconf, uint16_t fgcolor, uint16_t bgcolor) {
    _box  = boxconf;
    _mode = 0;
    _num  = 0;
    WidgetConfig wconf = { boxconf.left, boxconf.top, boxconf.textsize, boxconf.align };
    Widget::init(wconf, fgcolor, bgcolor);

    if (_spr) { _spr->deleteSprite(); delete _spr; _spr = nullptr; }
    _spr = new LGFX_Sprite(&dsp);
    _spr->setColorDepth(16);
    _spr->setPsram(true);
}

void PlayModeWidget::_ensureSprite() {
    uint16_t w = _box.dimension * 2;
    uint16_t h = _box.dimension / 2;
    if (_spr->width() != w || _spr->height() != h) {
        _spr->deleteSprite();
        _spr->createSprite(w, h);
    }
}

void PlayModeWidget::setMode(uint8_t playMode) {
    _mode = playMode;
    _draw();
}

void PlayModeWidget::setState(uint8_t playMode, uint16_t num) {
    _mode = playMode;
    _num = num;
    _draw();
}

void PlayModeWidget::setNum(uint16_t num) {
    _num = num;
    _draw();
}

void PlayModeWidget::invalidate() {
    _draw();
}

void PlayModeWidget::_draw() {
    if (!_active || !_spr) return;
    _ensureSprite();

    // Theme színek – runtime változáskor azonnal érvényes
    _fgcolor = config.theme.pmode;
    _bgcolor = config.theme.background;

    const uint16_t fg  = _fgcolor;
    const uint16_t bg  = _bgcolor;
    const uint16_t halfW = _box.dimension;
    const uint16_t boxH  = _box.dimension / 2;
    const uint16_t totalW = halfW * 2;
    const uint8_t  r   = _box.radius;
    const uint8_t  brd = _box.border;

    _spr->fillSprite(bg);

    // 1. Teljes widget kitöltve fg-vel
    if (r > 0) {
        _spr->fillRoundRect(0, 0, totalW, boxH, r, fg);
    } else {
        _spr->fillRect(0, 0, totalW, boxH, fg);
    }

    // 2. Jobb fele belseje visszatörölve (üres doboz) – ellentétes a BitrateWidget-tel
    _spr->fillRect(halfW + brd, brd, halfW - brd * 2, boxH - brd * 2, bg);

    // 3. Külső keret
    for (uint8_t i = 0; i < brd; i++) {
        uint8_t ri = (r > i) ? (r - i) : 0;
        _spr->drawRoundRect(i, i, totalW - i*2, boxH - i*2, ri, fg);
    }

    // 4. Középső elválasztó
    for (uint8_t i = 0; i < brd; i++) {
        _spr->drawFastVLine(halfW + i, 0, boxH, fg);
    }

    // Font betöltés
    if (uint8_t* fnt = vlwBySize(_box.textsize)) {
        _spr->loadFont(fnt);
    }
    _spr->setTextWrap(false);
    _spr->setTextDatum(middle_center);

    // BAL doboz: playmode (kitöltött → invertált szín)
    const bool btMode = _mode == DPS_BLUETOOTH;
    const char* modeStr = (_mode == DPS_SDCARD) ? "SD"
                        : (_mode == DPS_DLNA) ? "DLNA"
                        : btMode ? "BT" : "WEB";
    _spr->setTextColor(bg, fg);
    _spr->drawString(modeStr, halfW / 2, boxH / 2);

    // JOBB doboz: állomásszám (üres → normál szín)
    char numBuf[8];
    if (!btMode && _num > 0) {
        snprintf(numBuf, sizeof(numBuf), "%u", _num);
    } else {
        snprintf(numBuf, sizeof(numBuf), "-");
    }
    _spr->setTextColor(fg, bg);
    _spr->drawString(numBuf, halfW + halfW / 2, boxH / 2);

    _spr->unloadFont();
    _spr->pushSprite(_box.left, _box.top);
}

void PlayModeWidget::_clear() {
    dsp.fillRect(_box.left, _box.top,
                 _box.dimension * 2, _box.dimension / 2,
                 config.theme.background);
}

#endif // DSP_MODEL != DSP_DUMMY
