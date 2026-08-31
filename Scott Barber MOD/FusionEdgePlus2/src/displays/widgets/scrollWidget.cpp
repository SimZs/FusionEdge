#include "../../core/config.h"
#include "../display_select.h"
#include "widgets.h"
#include "../../core/fonts.h"

static ScrollWidget* scrollOwner = nullptr;

void ScrollWidget::releaseScrollOwner() {
    scrollOwner = nullptr;
}

#if DSP_MODEL == DSP_AXS15231B
#ifndef AXS_SCROLL_MIN_STEP_MS
#define AXS_SCROLL_MIN_STEP_MS 50
#endif
#ifndef AXS_SCROLL_MAX_STEP_MS
#define AXS_SCROLL_MAX_STEP_MS 52
#endif
#ifndef AXS_SCROLL_MAX_DELTA_PX
#define AXS_SCROLL_MAX_DELTA_PX 2
#endif
#ifndef AXS_SCROLL_DIAG
#define AXS_SCROLL_DIAG 0
#endif
#ifndef AXS_SCROLL_DIAG_TOP
#define AXS_SCROLL_DIAG_TOP 113
#endif
#ifndef AXS_SCROLL_DIAG_INTERVAL_MS
#define AXS_SCROLL_DIAG_INTERVAL_MS 1000UL
#endif
#ifndef AXS_SCROLL_DIRECT_BLIT_AFTER_MS
#define AXS_SCROLL_DIRECT_BLIT_AFTER_MS 8000UL
#endif
#endif

ScrollWidget::ScrollWidget(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor) {
    init(separator, conf, fgcolor, bgcolor);
}

ScrollWidget::~ScrollWidget() {
    if (scrollOwner == this) {
        scrollOwner = nullptr;
    }
    if (_spr) {
        _spr->deleteSprite();
        delete _spr;
    }
#if DSP_MODEL == DSP_AXS15231B
    _deleteStrip();
#endif
    if (_sep) free(_sep);
}

void ScrollWidget::init(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor) {

    TextWidget::init(conf.widget, conf.buffsize, conf.uppercase, fgcolor, bgcolor);

    _sep = (char*)malloc(4);
    if (_sep) snprintf(_sep, 4, " %.*s ", 1, separator);

    _startscrolldelay = conf.startscrolldelay;
    _scrolldelta = conf.scrolldelta;
    _scrolltime = conf.scrolltime;
#if DSP_MODEL == DSP_AXS15231B
    if (_scrolltime < AXS_SCROLL_MIN_STEP_MS) {
        _scrolltime = AXS_SCROLL_MIN_STEP_MS;
    }
    if (_scrolltime > AXS_SCROLL_MAX_STEP_MS) {
        _scrolltime = AXS_SCROLL_MAX_STEP_MS;
    }
    if (_scrolldelta > AXS_SCROLL_MAX_DELTA_PX) {
        _scrolldelta = AXS_SCROLL_MAX_DELTA_PX;
    }
#endif
    _width = conf.width;

    _spr = new LGFX_Sprite(&dsp);
    _spr->setColorDepth(16);
    _spr->setPsram(true);

    // 1. ELŐBB beállítjuk a paramétereket (font betöltése), hogy tudjuk a magasságot
    _setTextParams();

    // 2. Lekérjük a tényleges font magasságot (Roboto36 esetén ez ~40-42 px lesz)
    _textheight = _spr->fontHeight();

    // 3. CSAK MOST hozzuk létre a Sprite-ot a pontos mérettel
    if (!_spr->createSprite(_width, _textheight)) {
        // Ha PSRAM-ba nem sikerült, megpróbáljuk belső RAM-ba
        _spr->setPsram(false);
        _spr->createSprite(_width, _textheight);
    }

    _scrollOffset = 0;
    _doscroll = false;
}

void ScrollWidget::_setTextParams() {
    _setTextParams(_spr);
}

void ScrollWidget::_setTextParams(LGFX_Sprite* sprite) {
    if (!sprite || _config.textsize == 0) return;
    sprite->setTextColor(_fgcolor, _bgcolor);
    sprite->setTextWrap(false);
    sprite->unloadFont();
    uint8_t* vlw = vlwBySize(_config.textsize);
    if (vlw) {
        sprite->loadFont(vlw);
        sprite->setTextSize(1);
    } else {
        sprite->setFont(nullptr);
        sprite->setTextSize(2);
    }
}

#if DSP_MODEL == DSP_AXS15231B
void ScrollWidget::_deleteStrip() {
    if (_stripSpr) {
        _stripSpr->deleteSprite();
        delete _stripSpr;
        _stripSpr = nullptr;
    }
    _stripReady = false;
}

bool ScrollWidget::_buildStrip() {
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
        _deleteStrip();
        return false;
    }

    _stripSpr->fillSprite(_bgcolor);
    _stripSpr->setCursor(0, 0);
    _stripSpr->print(_text);
    _stripSpr->print(_sep);
    _stripReady = true;
    return true;
}
#endif

void ScrollWidget::setFontBySize(uint8_t size) {
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
#if DSP_MODEL == DSP_AXS15231B
    _buildStrip();
#endif
    _scrollOffset = 0;
    _scrollState = SCROLL_WAIT_START;
    _scrolldelay = millis();
    if (_active) { _draw(); }
}

void ScrollWidget::resetText() {
    // _oldtext cache törlése: a következő setText() akkor is frissít,
    // ha a string byte-szinten egyezne az előzővel (pl. short→long váltáskor
    // levágott string egyezés esetén)
    if (_oldtext) _oldtext[0] = '\0';
}

void ScrollWidget::setText(const char* txt) {
    if (!txt || !_text || !_oldtext || !_spr || !_sep || _buffsize == 0) return;
    strlcpy(_text, txt, _buffsize);
    if (strcmp(_oldtext, _text) == 0) return;
    _setTextParams();
    _textwidth = _spr->textWidth(_text);
    int sepW = _spr->textWidth(_sep);
    _fullWidth = _textwidth + sepW;
    _doscroll = (_textwidth > _width);
#if DSP_MODEL == DSP_AXS15231B
    _buildStrip();
#endif
    _scrollOffset = 0;
    _scrollState = SCROLL_WAIT_START;
    _scrolldelay = millis();
    if (_active) { _draw(); }
    strlcpy(_oldtext, _text, _buffsize);
}

void ScrollWidget::setText(const char* txt, const char* format) {
    if (!txt || !format || _buffsize == 0) return;
    char buf[_buffsize];
    int  n = snprintf(buf, sizeof(buf), format, txt);
    if (n < 0) return;
    buf[sizeof(buf) - 1] = '\0';
    setText(buf);
}

void ScrollWidget::loop() {
#if DSP_MODEL == DSP_AXS15231B
    if (_scrolltime < AXS_SCROLL_MIN_STEP_MS) {
        _scrolltime = AXS_SCROLL_MIN_STEP_MS;
    }
    if (_scrolltime > AXS_SCROLL_MAX_STEP_MS) {
        _scrolltime = AXS_SCROLL_MAX_STEP_MS;
    }
    if (_scrolldelta > AXS_SCROLL_MAX_DELTA_PX) {
        _scrolldelta = AXS_SCROLL_MAX_DELTA_PX;
    }
#endif

    if (_locked || !_active || !_doscroll) {
        if (scrollOwner == this) {
            scrollOwner = nullptr;
        }
        return;
    }

    if (scrollOwner && scrollOwner != this) {
        return;
    }

    uint32_t now = millis();
    uint32_t waitTime = (_scrollState == SCROLL_WAIT_START) ? _startscrolldelay : _scrolltime;

    if (now - _scrolldelay >= waitTime) {
#if DSP_MODEL == DSP_AXS15231B
        if (dsp.isFlushBusy() || dsp.isFrameBusy()) {
#if AXS_SCROLL_DIAG
            if (_scrollState == SCROLL_RUN && _config.top == AXS_SCROLL_DIAG_TOP) {
                _diagBusySkips++;
            }
#endif
            return;
        }
#endif
#if DSP_MODEL == DSP_AXS15231B && AXS_SCROLL_DIAG
        const bool     wasRunning = (_scrollState == SCROLL_RUN);
        const uint32_t lateMs = (now - _scrolldelay > waitTime) ? (now - _scrolldelay - waitTime) : 0;
        const uint32_t drawStartUs = micros();
#endif
        scrollOwner = this;
        _draw();
#if DSP_MODEL == DSP_AXS15231B && AXS_SCROLL_DIAG
        const uint32_t drawUs = micros() - drawStartUs;
        if (wasRunning && _config.top == AXS_SCROLL_DIAG_TOP) {
            if (_diagWindowMs == 0) {
                _diagWindowMs = now;
            }
            if (_diagLastDrawMs != 0) {
                uint32_t stepMs = now - _diagLastDrawMs;
                _diagSamples++;
                _diagSumStepMs += stepMs;
                if (stepMs > _diagMaxStepMs) _diagMaxStepMs = stepMs;
                if (stepMs > waitTime + 10) _diagMissed++;
            }
            _diagLastDrawMs = now;
            if (lateMs > _diagMaxLateMs) _diagMaxLateMs = lateMs;
            _diagSumDrawUs += drawUs;
            if (drawUs > _diagMaxDrawUs) _diagMaxDrawUs = drawUs;

            if (now - _diagWindowMs >= AXS_SCROLL_DIAG_INTERVAL_MS) {
                Serial.printf(
                    "AXS_SCROLL top=%u step=%u delta=%u samples=%lu avg_ms=%lu max_ms=%lu max_late=%lu draw_avg_us=%lu draw_max_us=%lu missed=%lu busy_skips=%lu strip=%u direct=%lu fill_us=%lu copy_us=%lu push_us=%lu owner=%u\n",
                    _config.top,
                    _scrolltime,
                    _scrolldelta,
                    (unsigned long)_diagSamples,
                    (unsigned long)(_diagSamples ? _diagSumStepMs / _diagSamples : 0),
                    (unsigned long)_diagMaxStepMs,
                    (unsigned long)_diagMaxLateMs,
                    (unsigned long)(_diagSamples ? _diagSumDrawUs / _diagSamples : 0),
                    (unsigned long)_diagMaxDrawUs,
                    (unsigned long)_diagMissed,
                    (unsigned long)_diagBusySkips,
                    (_stripReady && _stripSpr) ? 1 : 0,
                    (unsigned long)_diagDirectBlits,
                    (unsigned long)(_diagSamples ? _diagStripFillUs / _diagSamples : 0),
                    (unsigned long)(_diagSamples ? _diagStripCopyUs / _diagSamples : 0),
                    (unsigned long)(_diagSamples ? _diagStripPushUs / _diagSamples : 0),
                    scrollOwner == this ? 1 : 0
                );
                _diagWindowMs = now;
                _diagSamples = 0;
                _diagSumStepMs = 0;
                _diagMaxStepMs = 0;
                _diagMaxLateMs = 0;
                _diagMissed = 0;
                _diagBusySkips = 0;
                _diagSumDrawUs = 0;
                _diagMaxDrawUs = 0;
                _diagStripFillUs = 0;
                _diagStripCopyUs = 0;
                _diagStripPushUs = 0;
                _diagDirectBlits = 0;
            }
        }
#endif
        // Itt frissítjük az időbélyeget, miután a rajzolás megtörtént
        _scrolldelay = now;
    }
}

void ScrollWidget::setColors(uint16_t fg, uint16_t bg) {
    _fgcolor = fg;
    _bgcolor = bg;
    _setTextParams();
#if DSP_MODEL == DSP_AXS15231B
    _buildStrip();
#endif
    _draw();
}

// --- TISZTA TÖRLÉS ---
void ScrollWidget::_clear() {
    // Ellenőrizzük, hogy a Sprite létezik-e és van-e mérete
    if (_spr && _spr->width() > 0) {
        _spr->fillSprite(_bgcolor);
        _spr->pushSprite(_config.left, _config.top);
    } else {
        dsp.fillRect(_config.left, _config.top, _width, _textheight, _bgcolor);
    }
}

#if DSP_MODEL == DSP_AXS15231B
void ScrollWidget::_drawStrip() {
    uint32_t t0 = micros();
    _spr->fillSprite(_bgcolor);
    uint32_t t1 = micros();
    _stripSpr->pushSprite(_spr, -_scrollOffset, 0);
    _stripSpr->pushSprite(_spr, _fullWidth - _scrollOffset, 0);
    uint32_t t2 = micros();
    uint16_t* pixels = static_cast<uint16_t*>(_spr->getBuffer());
    bool usedDirectBlit = false;
    if (millis() >= AXS_SCROLL_DIRECT_BLIT_AFTER_MS && pixels) {
        usedDirectBlit = dsp.blitFrameBlock(_config.left, _config.top, _spr->width(), _spr->height(), pixels);
    }
    if (!usedDirectBlit) {
        _spr->pushSprite(_config.left, _config.top);
    }
    uint32_t t3 = micros();
    _diagStripFillUs += t1 - t0;
    _diagStripCopyUs += t2 - t1;
    _diagStripPushUs += t3 - t2;
    if (usedDirectBlit) {
        _diagDirectBlits++;
    }
}
#endif

void ScrollWidget::_draw() {
    if (!_active || _locked || !_spr || _spr->width() <= 0) return;
    //  --- 1. ESET: NINCS GÖRGETÉS (Rövid szöveg) ---
    if (!_doscroll) {
        _spr->fillSprite(_bgcolor);
        int16_t startX = 0;

        if (_config.align == WA_CENTER) {
            startX = (_width - _textwidth) / 2;
        } else if (_config.align == WA_RIGHT) {
            startX = _width - _textwidth;
        }

        _spr->setCursor(startX, 0);
        _spr->print(_text);
        _spr->pushSprite(_config.left, _config.top);
        return;
    }

    // --- 2. ESET: VÁRAKOZÁS A GÖRGETÉS ELŐTT ---
    if (_scrollState == SCROLL_WAIT_START) {
        // Ha még 0 az offset, rajzoljuk ki az elejét álló helyzetben
        if (_scrollOffset == 0) {
            _spr->fillSprite(_bgcolor);
            _spr->setCursor(0, 0);
            _spr->print(_text);
            _spr->pushSprite(_config.left, _config.top);
        }

        // Időellenőrzés: Csak akkor lépünk tovább, ha letelt a várakozás
        if (millis() - _scrolldelay >= _startscrolldelay) {
            _scrollState = SCROLL_RUN;
            _scrolldelay = millis(); // Reseteljük az időzítőt a folyamatos gördüléshez
        }
        return;
    }

    // --- 3. ESET: AKTÍV GÖRGETÉS (SCROLL_RUN) ---
    if (_scrollState == SCROLL_RUN) {
        // Offset növelése
        _scrollOffset += _scrolldelta;

        // Ha végére értünk a teljes hossznak (szöveg + separator)
        if (_scrollOffset >= _fullWidth) {
            _scrollOffset = 0;
            _scrollState = SCROLL_WAIT_START;
            _scrolldelay = millis(); // Itt indul a 2 másodperces várakozás az elején
            if (scrollOwner == this) {
                scrollOwner = nullptr;
            }

            // Kirajzoljuk az alapállapotot, hogy ne maradjon üres vagy félkész a kép
            _spr->fillSprite(_bgcolor);
            _spr->setCursor(0, 0);
            _spr->print(_text);
            _spr->pushSprite(_config.left, _config.top);
            return;
        }

        // Rajzolás folyamata
        _spr->fillSprite(_bgcolor);

        // Első példány kirajzolása (balra csúszik ki)
#if DSP_MODEL == DSP_AXS15231B
        if (_stripReady && _stripSpr) {
            _drawStrip();
            _scrolldelay = millis();
            return;
        }
#endif
        _spr->setCursor(-_scrollOffset, 0);
        _spr->print(_text);
        _spr->print(_sep);

        // Második példány kirajzolása (jobbról úszik be)
        // Csak akkor rajzoljuk, ha az első már elkezdett kimenni a képből
        if (_scrollOffset > 0) {
            _spr->setCursor(_fullWidth - _scrollOffset, 0);
            _spr->print(_text);
            _spr->print(_sep);
        }

        _spr->pushSprite(_config.left, _config.top);

        // Frissítjük az utolsó futás idejét a loop() számára
        _scrolldelay = millis();
    }
}
void ScrollWidget::setActive(bool act, bool clr) {
    if (!act && scrollOwner == this) {
        scrollOwner = nullptr;
    }
    // 1. Alaphelyzetbe állítjuk a belső változókat, mielőtt bármi történne
    if (act) {
        _scrollOffset = 0;
        _scrollState = SCROLL_WAIT_START;
        _scrolldelay = millis();
        if (_spr && _spr->width() > 0) {
            _spr->fillSprite(_bgcolor); // Megelőzzük a szellemképet
        }
    }

    // 2. Meghívjuk az ősosztály metódusát.
    Widget::setActive(act, clr);
}

void ScrollWidget::_reset() {
    if (scrollOwner == this) {
        scrollOwner = nullptr;
    }
    _scrolldelay = millis();
    _scrollOffset = 0;
    _scrollState = SCROLL_WAIT_START;
    if (_spr && _spr->width() > 0) _spr->fillSprite(_bgcolor);
}
