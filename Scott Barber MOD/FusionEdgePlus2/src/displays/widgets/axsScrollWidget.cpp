#include "../../core/options.h"

#if DSP_MODEL == DSP_AXS15231B

#include "../../core/config.h"
#include "../../core/fonts.h"
#include "../display_select.h"
#include "axsScrollWidget.h"

#ifndef AXS_SMOOTH_SCROLL
#define AXS_SMOOTH_SCROLL 1
#endif

#ifndef AXS_SCROLL_SINGLE_OWNER
#define AXS_SCROLL_SINGLE_OWNER AXS_SMOOTH_SCROLL
#endif

#ifndef AXS_SCROLL_MIN_STEP_MS
#define AXS_SCROLL_MIN_STEP_MS 50
#endif

#ifndef AXS_SCROLL_MAX_DELTA_PX
#define AXS_SCROLL_MAX_DELTA_PX 1
#endif

#if AXS_SCROLL_SINGLE_OWNER
static AXSScrollWidget* axsScrollOwner = nullptr;
#endif

AXSScrollWidget::AXSScrollWidget(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor) {
    init(separator, conf, fgcolor, bgcolor);
}

AXSScrollWidget::~AXSScrollWidget() {
#if AXS_SCROLL_SINGLE_OWNER
    if (axsScrollOwner == this) {
        axsScrollOwner = nullptr;
    }
#endif
    _deleteStrip();
    if (_spr) {
        _spr->deleteSprite();
        delete _spr;
    }
    if (_sep) free(_sep);
}

void AXSScrollWidget::init(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor) {
    TextWidget::init(conf.widget, conf.buffsize, conf.uppercase, fgcolor, bgcolor);

    _sep = (char*)malloc(4);
    if (_sep) snprintf(_sep, 4, " %.*s ", 1, separator);

    _startscrolldelay = conf.startscrolldelay;
    _scrolldelta = conf.scrolldelta;
    _scrolltime = conf.scrolltime;

#if AXS_SMOOTH_SCROLL
    if (_scrolltime < AXS_SCROLL_MIN_STEP_MS) {
        _scrolltime = AXS_SCROLL_MIN_STEP_MS;
    }
    if (_scrolldelta > AXS_SCROLL_MAX_DELTA_PX) {
        _scrolldelta = AXS_SCROLL_MAX_DELTA_PX;
    }
#else
    if (_scrolltime > 30 && _scrolldelta > 1) {
        _scrolldelta = (_scrolldelta + 1) / 2;
        _scrolltime = (_scrolltime + 1) / 2;
    }
#endif
    _width = conf.width;

    _spr = new LGFX_Sprite(&dsp);
    _spr->setColorDepth(16);
    _spr->setPsram(true);

    _setTextParams();
    _textheight = _spr->fontHeight();

    if (!_spr->createSprite(_width, _textheight)) {
        _spr->setPsram(false);
        _spr->createSprite(_width, _textheight);
    }

    _reset();
    _doscroll = false;
}

void AXSScrollWidget::_setTextParams() {
    _setTextParams(_spr);
}

void AXSScrollWidget::_setTextParams(LGFX_Sprite* sprite) {
    if (!sprite || _config.textsize == 0) return;

    sprite->setTextColor(_fgcolor, _bgcolor);
    sprite->setTextWrap(false);
    sprite->unloadFont();

    if (_config.textsize == 20 && font_vlw_20) {
        sprite->loadFont(font_vlw_20);
        sprite->setTextSize(1);
    } else if (_config.textsize == 22 && font_vlw_22) {
        sprite->loadFont(font_vlw_22);
        sprite->setTextSize(1);
    } else if (_config.textsize == 24 && font_vlw_24) {
        sprite->loadFont(font_vlw_24);
        sprite->setTextSize(1);
    } else if (_config.textsize == 26 && font_vlw_26) {
        sprite->loadFont(font_vlw_26);
        sprite->setTextSize(1);
    } else if (_config.textsize == 36 && font_vlw_36) {
        sprite->loadFont(font_vlw_36);
        sprite->setTextSize(1);
    } else {
        sprite->setFont(nullptr);
        sprite->setTextSize(2);
    }
}

void AXSScrollWidget::_deleteStrip() {
    if (_stripSpr) {
        _stripSpr->deleteSprite();
        delete _stripSpr;
        _stripSpr = nullptr;
    }
    _stripReady = false;
}

bool AXSScrollWidget::_buildStrip() {
    _deleteStrip();

    if (!_spr || !_doscroll || !_sep || _fullWidth <= 0 || _textheight == 0) {
        return false;
    }

    _stripSpr = new LGFX_Sprite(&dsp);
    if (!_stripSpr) {
        return false;
    }

    _stripSpr->setColorDepth(16);
    _stripSpr->setPsram(true);
    _setTextParams(_stripSpr);

    if (!_stripSpr->createSprite(_fullWidth, _textheight)) {
        _stripSpr->setPsram(false);
        if (!_stripSpr->createSprite(_fullWidth, _textheight)) {
            _deleteStrip();
            return false;
        }
    }

    _stripSpr->fillSprite(_bgcolor);
    _stripSpr->setCursor(0, 0);
    _stripSpr->print(_text);
    _stripSpr->print(_sep);
    _stripReady = true;
    return true;
}

void AXSScrollWidget::setFontBySize(uint8_t size) {
    if (!_spr) return;

    _config.textsize = size;
    _setTextParams();

    uint16_t newHeight = _spr->fontHeight();
    if (newHeight != _textheight) {
        _textheight = newHeight;
        _spr->deleteSprite();
        if (!_spr->createSprite(_width, _textheight)) {
            _spr->setPsram(false);
            _spr->createSprite(_width, _textheight);
        }
    }

    _textwidth = _spr->textWidth(_text);
    int sepW = _spr->textWidth(_sep);
    _fullWidth = _textwidth + sepW;
    _doscroll = (_textwidth > _width);
    _buildStrip();
    _reset();

    if (_active) {
        _draw();
    }
}

void AXSScrollWidget::setText(const char* txt) {
    if (!txt || !_text || !_oldtext || !_spr || !_sep || _buffsize == 0) return;

    strlcpy(_text, txt, _buffsize);
    if (strcmp(_oldtext, _text) == 0) return;

    _setTextParams();
    _textwidth = _spr->textWidth(_text);
    int sepW = _spr->textWidth(_sep);
    _fullWidth = _textwidth + sepW;
    _doscroll = (_textwidth > _width);
    _buildStrip();
    _reset();

    if (_active) {
        _draw();
    }

    strlcpy(_oldtext, _text, _buffsize);
}

void AXSScrollWidget::setText(const char* txt, const char* format) {
    if (!txt || !format || _buffsize == 0) return;

    char buf[_buffsize];
    int  n = snprintf(buf, sizeof(buf), format, txt);
    if (n < 0) return;

    buf[sizeof(buf) - 1] = '\0';
    setText(buf);
}

void AXSScrollWidget::loop() {
    if (_locked || !_active || !_doscroll) {
#if AXS_SCROLL_SINGLE_OWNER
        if (axsScrollOwner == this) {
            axsScrollOwner = nullptr;
        }
#endif
        return;
    }

    uint32_t now = millis();
    if (_scrollState == SCROLL_WAIT_START) {
        if (now - _scrolldelay >= _startscrolldelay) {
#if AXS_SCROLL_SINGLE_OWNER
            if (axsScrollOwner && axsScrollOwner != this) {
                return;
            }
            axsScrollOwner = this;
#endif
            _scrollState = SCROLL_RUN;
            _lastStep = now;
        }
        return;
    }

#if AXS_SCROLL_SINGLE_OWNER
    if (axsScrollOwner && axsScrollOwner != this) {
        return;
    }
    axsScrollOwner = this;
#endif

    if (now - _lastStep >= _scrolltime) {
        _scrollOffset += _scrolldelta;
        if (_scrollOffset >= _fullWidth) {
            _reset();
            _drawStatic();
#if AXS_SCROLL_SINGLE_OWNER
            if (axsScrollOwner == this) {
                axsScrollOwner = nullptr;
            }
#endif
            return;
        }

        _lastStep += _scrolltime;
        if (now - _lastStep >= _scrolltime) {
            _lastStep = now;
        }
        _draw();
    }
}

void AXSScrollWidget::setColors(uint16_t fg, uint16_t bg) {
    _fgcolor = fg;
    _bgcolor = bg;
    _setTextParams();
    _buildStrip();
    _draw();
}

void AXSScrollWidget::_clear() {
    if (_spr && _spr->width() > 0) {
        _spr->fillSprite(_bgcolor);
        _spr->pushSprite(_config.left, _config.top);
    } else {
        dsp.fillRect(_config.left, _config.top, _width, _textheight, _bgcolor);
    }
}

void AXSScrollWidget::_drawStatic() {
    if (!_spr || _spr->width() <= 0) return;

    _spr->fillSprite(_bgcolor);
    int16_t startX = 0;

    if (!_doscroll) {
        if (_config.align == WA_CENTER) {
            startX = (_width - _textwidth) / 2;
        } else if (_config.align == WA_RIGHT) {
            startX = _width - _textwidth;
        }
    }

    _spr->setCursor(startX, 0);
    _spr->print(_text);
    _spr->pushSprite(_config.left, _config.top);
}

void AXSScrollWidget::_drawStrip() {
    _spr->fillSprite(_bgcolor);
    _stripSpr->pushSprite(_spr, -_scrollOffset, 0);
    _stripSpr->pushSprite(_spr, _fullWidth - _scrollOffset, 0);
    _spr->pushSprite(_config.left, _config.top);
}

void AXSScrollWidget::_drawFallback() {
    _spr->fillSprite(_bgcolor);
    _spr->setCursor(-_scrollOffset, 0);
    _spr->print(_text);
    _spr->print(_sep);

    if (_scrollOffset > 0) {
        _spr->setCursor(_fullWidth - _scrollOffset, 0);
        _spr->print(_text);
        _spr->print(_sep);
    }

    _spr->pushSprite(_config.left, _config.top);
}

void AXSScrollWidget::_draw() {
    if (!_active || _locked || !_spr || _spr->width() <= 0) return;

    if (!_doscroll || _scrollState == SCROLL_WAIT_START) {
        _drawStatic();
        return;
    }

    if (_stripReady && _stripSpr) {
        _drawStrip();
    } else {
        _drawFallback();
    }
}

void AXSScrollWidget::_reset() {
#if AXS_SCROLL_SINGLE_OWNER
    if (axsScrollOwner == this) {
        axsScrollOwner = nullptr;
    }
#endif
    _scrolldelay = millis();
    _lastStep = _scrolldelay;
    _scrollOffset = 0;
    _scrollState = SCROLL_WAIT_START;
    if (_spr && _spr->width() > 0) {
        _spr->fillSprite(_bgcolor);
    }
}

#endif
