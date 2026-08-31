#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY
#include "../../core/config.h"
#include "../display_select.h"
#include "bufferWidget.h"

BufferWidget::BufferWidget(yoDisplay* dsp, const VolumeWidgetConfig& conf) : _dsp(dsp), _spr(dsp) {
    _conf   = conf;
    _filled = -1;

    Widget::init({_conf.left, _conf.top, 0, WA_LEFT},
                 0,
                 config.theme.buff_bg);

    _spr.setPsram(true);
    _spr.setColorDepth(16);
    _spr.createSprite(_conf.width, _conf.height);
}

void BufferWidget::clear() {
    dsp.fillRect(_conf.left, _conf.top, _conf.width, _conf.height, config.theme.background);
}

void BufferWidget::setValue(uint32_t val, uint32_t maxVal) {
    if (!_active) return;
    if (maxVal == 0) maxVal = 1;

    int8_t filled = (int8_t)(((uint32_t)_conf.segments * val) / maxVal);
    if (filled > _conf.segments) filled = _conf.segments;
    if (filled == _filled) return;

    _filled = filled;
    _draw();
}

void BufferWidget::_draw() {
    if (!_active) return;

    _spr.fillSprite(config.theme.buff_bg);

    // Háttér
    if (_conf.radius > 0)
        _spr.fillRoundRect(0, 0, _conf.width, _conf.height, _conf.radius, config.theme.buff_bg);
    else
        _spr.fillRect(0, 0, _conf.width, _conf.height, config.theme.buff_bg);

    // Keret – 1px border, nincs nagy belső margó
    if (_conf.border > 0) {
        for (uint8_t i = 0; i < _conf.border; i++) {
            if (_conf.radius > 0)
                _spr.drawRoundRect(i, i, _conf.width - i*2, _conf.height - i*2, _conf.radius, config.theme.buff_border);
            else
                _spr.drawRect(i, i, _conf.width - i*2, _conf.height - i*2, config.theme.buff_border);
        }
    }

    // Tartalom: szorosan a border mellé, 2px padding
    int pad = _conf.border + 2;
    int innerW = _conf.width - pad * 2;
    int innerH = _conf.height - pad * 2;

    int segmentsW     = (_conf.segments * _conf.segWidth) + ((_conf.segments - 1) * _conf.segGap);
    int totalContentW = _conf.iconW + 3 + segmentsW;
    int offsetX       = pad + (innerW - totalContentW) / 2;
    int offsetY       = pad;

    _drawIcon(offsetX, offsetY, innerH);
    _drawSegments(offsetX + _conf.iconW + 3, offsetY, innerH);

    _spr.pushSprite(_conf.left, _conf.top);
}

// ── IKON: henger-stack (3 réteg, adatbázis/buffer stílus) ──────────────────
// Fusion-féle implementáció: minden réteg felső+alsó ellipszis ív + oldalsó vonalak
void BufferWidget::_drawIcon(int startX, int startY, int areaH) {
    uint16_t c  = config.theme.buff_icon;
    int      y  = startY + (areaH - _conf.iconH) / 2;

    int cx     = startX + _conf.iconW / 2;
    int rx     = _conf.iconW / 2;
    int ry     = max(1, (int)(_conf.iconH / 10));
    int layers = 3;
    int layerH = (_conf.iconH - ry) / layers;

    for (int lv = 0; lv < layers; lv++) {
        int topY = y + lv * layerH;
        int botY = topY + layerH;

        // Oldalsó vonalak
        _spr.drawLine(cx - rx, topY + ry, cx - rx, botY + ry, c);
        _spr.drawLine(cx + rx, topY + ry, cx + rx, botY + ry, c);

        // Felső ív (felső félellipszis)
        for (int dx = -rx; dx <= rx; dx++) {
            float t  = (float)dx / rx;
            int   ey = (int)(ry - ry * sqrtf(1.0f - t * t));
            _spr.drawPixel(cx + dx, topY + ey, c);
        }

        // Alsó ív (teljes ellipszis – látszik az "alap")
        for (int dx = -rx; dx <= rx; dx++) {
            float t  = (float)dx / rx;
            int   ey = (int)(ry * sqrtf(1.0f - t * t));
            _spr.drawPixel(cx + dx, botY + ry + ey, c);
            _spr.drawPixel(cx + dx, botY + ry - ey, c);
        }
    }
}

// ── SZEGMENSEK ──────────────────────────────────────────────────────────────
void BufferWidget::_drawSegments(int startX, int startY, int areaH) {
    int y = startY + (areaH - _conf.segHeight) / 2;

    for (uint8_t i = 0; i < _conf.segments; i++) {
        int      x   = startX + i * (_conf.segWidth + _conf.segGap);
        uint16_t col = (i < _filled) ? config.theme.buff : config.theme.buff_inactive;
        _spr.fillRect(x, y, _conf.segWidth, _conf.segHeight, col);
    }
}

#endif // DSP_MODEL != DSP_DUMMY
