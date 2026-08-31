#include "../../core/config.h"
#include "../display_select.h"
#include "../../core/network.h"
#include "../tools/language.h"
#include "clockWidget.h"
#include "../../core/fonts.h"

#ifndef CLOCK_WIDGET_SEC_DEBUG
#define CLOCK_WIDGET_SEC_DEBUG 0
#endif

// Slightly smaller clock digits on 320x240 panels (~85% of native vlw size).
// Other displays keep drawing the clock font at its native size.
#if DSP_MODEL == DSP_ILI9341 || DSP_MODEL == DSP_ST7789
static constexpr float CLOCK_TEXT_SCALE = 0.70f;
#else
static constexpr float CLOCK_TEXT_SCALE = 1.0f;
#endif

void ClockWidget::init(WidgetConfig clockConf, uint16_t fgcolor, uint16_t bgcolor) {

    _clockConf = clockConf;

    Widget::init(_clockConf, fgcolor, bgcolor);

    _secTopSpace = 10;
    _space = 4;

    _calcSize();
    _begin();
    // _getTimeBounds() a _syncLayoutIfNeeded()-ben hívódik, amikor a sprite már létezik
}

void ClockWidget::_begin() {
    if (!_spr) { _spr = new LGFX_Sprite(&dsp); }
    _spr->setColorDepth(16);
    _spr->setPsram(true);
    if (_spr) {
        _spr->createSprite(_clockwidth, _clockheight);
        _spr->fillSprite(config.theme.background);
        _spr->setTextDatum(lgfx::top_left);
    }
}

void ClockWidget::_calcSize() {

    if (!_spr) { _spr = new LGFX_Sprite(&dsp); }
    _spr->setColorDepth(16);

    auto measureClockStyle = [this](uint8_t* mainFont, uint8_t* secFont, uint16_t& wTime, uint16_t& hTime, uint16_t& wSec, uint16_t& hSec) {
        // Ha a font nincs betöltve, 0-t adunk vissza – a max() nem veszi figyelembe
        if (!mainFont) { wTime = 0; hTime = 0; wSec = 0; hSec = 0; return; }
        _spr->loadFont(mainFont);
        _spr->setTextSize(CLOCK_TEXT_SCALE);
        wTime = _spr->textWidth("88:88");
        hTime = _spr->fontHeight();

        if (secFont) {
            _spr->loadFont(secFont);
            _spr->setTextSize(CLOCK_TEXT_SCALE);
        } else {
            _spr->unloadFont();
            _spr->setFont(nullptr);
            _spr->setTextSize(3);
        }
        wSec = _spr->textWidth("88");
        hSec = _spr->fontHeight();
    };

    uint8_t* digiMain = nullptr;
    uint8_t* digiSec = nullptr;
    uint8_t* calMain = nullptr;
    uint8_t* calSec = nullptr;
    uint8_t* androidMain = nullptr;
    uint8_t* androidSec = nullptr;
    uint8_t* oldtMain = nullptr;
    uint8_t* oldtSec = nullptr;
    uint8_t* laraMain = nullptr;
    uint8_t* laraSec = nullptr;
    uint8_t* decMain = nullptr;
    uint8_t* decSec = nullptr;
    uint8_t* squaMain = nullptr;
    uint8_t* squaSec = nullptr;
    getClockFontStylePointers(CLOCKFONT_STYLE_DIGI7, &digiMain, &digiSec);
    getClockFontStylePointers(CLOCKFONT_STYLE_CALIBRI, &calMain, &calSec);
    getClockFontStylePointers(CLOCKFONT_STYLE_ANDROIDCLOCK, &androidMain, &androidSec);
    getClockFontStylePointers(CLOCKFONT_STYLE_OLDTIMER, &oldtMain, &oldtSec);
    getClockFontStylePointers(CLOCKFONT_STYLE_LARADOT, &laraMain, &laraSec);
    getClockFontStylePointers(CLOCKFONT_STYLE_DECODERR, &decMain, &decSec);
    getClockFontStylePointers(CLOCKFONT_STYLE_SQUAREFONT, &squaMain, &squaSec);
 
    uint16_t wTimeDigi = 0, hTimeDigi = 0, wSecDigi = 0, hSecDigi = 0;
    uint16_t wTimeCalibri = 0, hTimeCalibri = 0, wSecCalibri = 0, hSecCalibri = 0;
    uint16_t wTimeAndroid = 0, hTimeAndroid = 0, wSecAndroid = 0, hSecAndroid = 0;
    uint16_t wTimeOldtimer = 0, hTimeOldtimer = 0, wSecOldtimer = 0, hSecOldtimer = 0;
    uint16_t wTimeLaradot = 0, hTimeLaradot = 0, wSecLaradot = 0, hSecLaradot = 0;
    uint16_t wTimeDecoderr = 0, hTimeDecoderr = 0, wSecDecoderr = 0, hSecDecoderr = 0;
    uint16_t wTimeSquarefont = 0, hTimeSquarefont = 0, wSecSquarefont = 0, hSecSquarefont = 0;
    measureClockStyle(digiMain, digiSec, wTimeDigi, hTimeDigi, wSecDigi, hSecDigi);
    measureClockStyle(calMain, calSec, wTimeCalibri, hTimeCalibri, wSecCalibri, hSecCalibri);
    measureClockStyle(androidMain, androidSec, wTimeAndroid, hTimeAndroid, wSecAndroid, hSecAndroid);
    measureClockStyle(oldtMain, oldtSec, wTimeOldtimer, hTimeOldtimer, wSecOldtimer, hSecOldtimer);
    measureClockStyle(laraMain, laraSec, wTimeLaradot, hTimeLaradot, wSecLaradot, hSecLaradot);
    measureClockStyle(decMain, decSec, wTimeDecoderr, hTimeDecoderr, wSecDecoderr, hSecDecoderr);
    measureClockStyle(squaMain, squaSec, wTimeSquarefont, hTimeSquarefont, wSecSquarefont, hSecSquarefont);

    uint16_t w_time = max<uint16_t>({wTimeDigi, wTimeCalibri, wTimeAndroid, wTimeOldtimer, wTimeLaradot, wTimeDecoderr, wTimeSquarefont});
    uint16_t h_time = max<uint16_t>({hTimeDigi, hTimeCalibri, hTimeAndroid, hTimeOldtimer, hTimeLaradot, hTimeDecoderr, hTimeSquarefont});
    uint16_t w_sec  = max<uint16_t>({wSecDigi, wSecCalibri, wSecAndroid, wSecOldtimer, wSecLaradot, wSecDecoderr, wSecSquarefont});
    _secHeight      = max<uint16_t>({hSecDigi, hSecCalibri, hSecOldtimer, hSecLaradot, hSecDecoderr, hSecSquarefont});

    uint16_t w_right = w_sec;

    if (config.store.clockAmPmStyle) {
        if (font_vlw_18) {
            _spr->loadFont(font_vlw_18);
            _spr->setTextSize(1);
        } else {
            _spr->unloadFont();
            _spr->setFont(nullptr);
            _spr->setTextSize(1);
        }

        uint16_t w_ampm = _spr->textWidth("PM");
        if (w_ampm > w_right) { w_right = w_ampm; }
    }

    // --- layout ---
    _clockwidth = w_time + _space + w_right;
    _clockheight = h_time;
}

bool ClockWidget::_syncLayoutIfNeeded(bool forceRedraw) {
    const bool fontOrModeChanged = (_lastMainFont != font_vlw_clock) || (_lastSecFont != font_vlw_clock_sec) || (_lastClockFontStyle != config.store.clockFontStyle) ||
                                   (_lastClockAmPmStyle != config.store.clockAmPmStyle);
    const bool missingSprite = !_spr || !_spr->getBuffer();

    if (!forceRedraw && !fontOrModeChanged && !missingSprite) {
        return false;
    }

    _calcSize();
    _getTimeBounds();

    if (!_spr) {
        _spr = new LGFX_Sprite(&dsp);
    }
    _spr->setColorDepth(16);
    _spr->setPsram(true);

    const bool sizeMismatch = !_spr->getBuffer() || _spr->width() != _clockwidth || _spr->height() != _clockheight;
    if (sizeMismatch) {
        if (_spr->getBuffer()) { _spr->deleteSprite(); }
        _spr->createSprite(_clockwidth, _clockheight);
    }

    _spr->fillSprite(config.theme.background);
    _spr->setTextDatum(lgfx::top_left);

    _lastMainFont = font_vlw_clock;
    _lastSecFont = font_vlw_clock_sec;
    _lastClockFontStyle = config.store.clockFontStyle;
    _lastClockAmPmStyle = config.store.clockAmPmStyle;
    _lastRenderedSecond = -1;

    return true;
}

void ClockWidget::_captureTimeSnapshot() {
    network_get_timeinfo_snapshot(&_drawTimeinfo);
}

bool ClockWidget::_getTime() {
    if (config.store.clockAmPmStyle) {
        strftime(_timebuffer, sizeof(_timebuffer), "%I:%M", &_drawTimeinfo);
        if (_timebuffer[0] == '0') {
            _timebuffer[0] = ' '; // Ha az eslő számjegy 0 kicseréli szóközre (azonos karakterszélesség szükséges)
        }
    } else {
        strftime(_timebuffer, sizeof(_timebuffer), "%H:%M", &_drawTimeinfo);
    }
    const bool hasValidTime = _drawTimeinfo.tm_year > 100;
    const bool timeChanged = (_lastRenderedHour != _drawTimeinfo.tm_hour) || (_lastRenderedMinute != _drawTimeinfo.tm_min);
    bool       ret = (hasValidTime && (_drawTimeinfo.tm_sec == 0 || timeChanged)) || _forceflag != _drawTimeinfo.tm_year;
    _forceflag = _drawTimeinfo.tm_year;
    return ret;
}

uint16_t ClockWidget::_top() {
    if (_spr && _spr->getBuffer()) return 0; // A Sprite tetejére rajzolunk
    return clockConf.top;                    // Ha nincs Sprite, marad az eredeti
}

uint16_t ClockWidget::_left() {
    if (_spr && _spr->getBuffer()) return 0;
    return _clockConf.left;
}

void ClockWidget::_getTimeBounds() {
    if (config.isScreensaver) {
        _clockleft = _config.left;
        _dotsleft = 0;
        return;
    }

    switch (_config.align) {
        case WA_LEFT: _clockleft = _clockConf.left; break;
        case WA_RIGHT: _clockleft = _clockConf.left; break; // left = az óra bal élének fix X koordinátája
        default: _clockleft = (dsp.width() / 2 - _clockwidth / 2) + _clockConf.left; break;
    }
    // ❗ FONT-FÜGGŐ MÉRÉS TILOS ITT
    _dotsleft = 0;
}

#if DSP_MODEL == DSP_SSD1322
void ClockWidget::_drawShortDateSSD1322() {
    if (config.isScreensaver) { return; }
    // ⬅️ DÁTUM ELŐÁLLÍTÁSA KÖZÖS HELYEN
    _formatDate(); // _tmp -t tölti fel!
    WidgetConfig dc;
    memcpy_P(&dc, &dateConf, sizeof(WidgetConfig));
    // ===== FIX: 5x7 FONT MÉRETEK =====
    constexpr uint8_t  TS = 1;
    constexpr uint16_t H = CHARHEIGHT * TS;
    uint16_t           dateWidgetWidth = dsp.width() - dc.left;
    dsp.fillRect(dc.left, dc.top, dateWidgetWidth, H, config.theme.background);
    dsp.setFont(nullptr);
    dsp.setTextSize(TS);
    dsp.setTextColor(config.theme.date, config.theme.background); // 0x8410
    // ===== SZÉLESSÉG SZÁMÍTÁS (5x7!) =====
    uint16_t w = strlen(_tmp) * CHARWIDTH * TS;
    uint16_t x;
    switch (dc.align) {
        case WA_CENTER: x = dsp.width() - w - (dateWidgetWidth - w) / 2; break;
        case WA_RIGHT: x = dsp.width() - w; break;
        default: x = dc.left; break;
    }
    // ===== RAJZOLÁS =====
    dsp.setCursor(x, dc.top);
    dsp.print(_tmp);
}
#endif

void ClockWidget::_printClock(bool redraw) {
    if (!_spr || !_spr->getBuffer()) return;

    auto applyMainClockFont = [this]() {
        if (font_vlw_clock) {
            _spr->loadFont(font_vlw_clock);
            _spr->setTextSize(CLOCK_TEXT_SCALE);
        } else {
            _spr->unloadFont();
            _spr->setFont(nullptr);
            _spr->setTextSize(4);
        }
    };

    auto applySecClockFont = [this]() {
        if (font_vlw_clock_sec) {
            _spr->loadFont(font_vlw_clock_sec);
            _spr->setTextSize(CLOCK_TEXT_SCALE);
        } else {
            _spr->unloadFont();
            _spr->setFont(nullptr);
            _spr->setTextSize(3);
        }
    };

    auto applyAmPmFont = [this]() {
        if (font_vlw_18) {
            _spr->loadFont(font_vlw_18);
            _spr->setTextSize(1);
        } else {
            _spr->unloadFont();
            _spr->setFont(nullptr);
            _spr->setTextSize(1);
        }
    };

    uint16_t rightBlockWidth = 0;

    // ------------------------------------------------------------
    // 1) Fő idő (óra:perc)
    // ------------------------------------------------------------
    if (redraw) {
        _clearClock();
        _getTimeBounds();
        applyMainClockFont();
        _timewidth = _spr->textWidth(_timebuffer);
        _timeheight = _spr->fontHeight();
        const uint16_t timeBlockW = _spr->textWidth("88:88");
        int16_t        timeX = (int16_t)timeBlockW - (int16_t)_timewidth;
        if (timeX < 0) timeX = 0;

        // --- pontos ':' pozíció számítás a ténylegesen kirajzolt időből ---
        char hourPart[3] = { _timebuffer[0], _timebuffer[1], '\0' };
        _dotsleft = timeX + _spr->textWidth(hourPart);
        _dotswidth = _spr->textWidth(":");

        if (config.store.clockFontStyle == CLOCKFONT_STYLE_DIGI7 && config.store.clockFontMono) {
            const char* ghost = config.store.clockAmPmStyle ? " 8:88" : "88:88";
            int16_t     ghostX = (int16_t)timeBlockW - (int16_t)_spr->textWidth(ghost);
            if (ghostX < 0) ghostX = 0;
            _spr->setTextColor(config.theme.clockbg, config.theme.background);
            _spr->setCursor(ghostX, 0);
            _spr->print(ghost);
            _spr->setTextColor(config.theme.clock);
        } else {
            _spr->setTextColor(config.theme.clock, config.theme.background);
        }

        _spr->setCursor(timeX, 0);
        _spr->print(_timebuffer);

        // --------------------------------------------------------
        // 2) Jobb oldali blokk (elválasztó vonalak nélkül)
        // --------------------------------------------------------
        // _linesleft: a tényleges óra szélessége + gap, de max a sprite jobbszéléig
        // Így kis fontnál sem tolódik el a másodperc jobbra.
        _linesleft = timeBlockW + _space;
        if (_linesleft > _clockwidth) _linesleft = _clockwidth;
        rightBlockWidth = _clockwidth - _linesleft;

        if (config.store.clockAmPmStyle) {
            // AM/PM felül, másodperc alatta
            char buf[3];
            strftime(buf, sizeof(buf), "%p", &_drawTimeinfo);

            applyAmPmFont();
            const uint16_t ampmW = _spr->textWidth(buf);
            const uint16_t ampmH = _spr->fontHeight();
            int16_t ampmX = _linesleft + ((int16_t)rightBlockWidth - (int16_t)ampmW) / 2;
            // AM/PM: a jobb blokk felső negyedébe igazítva
            int16_t ampmY = ((int16_t)_timeheight / 4) - ((int16_t)ampmH / 2);
            if (ampmY < 0) ampmY = 0;

            _spr->setTextColor(config.theme.seconds, config.theme.background);
            _spr->setCursor(ampmX, ampmY);
            _spr->print(buf);
        }

    }

    // ------------------------------------------------------------
    // 4) MÁSODPERCEK
    // ------------------------------------------------------------

    const int  currentSecond = _drawTimeinfo.tm_sec;
    const bool showDots = (currentSecond % 2) == 0;
    if (!redraw && _lastRenderedSecond == currentSecond && _lastRenderedDots == showDots) {
#if CLOCK_WIDGET_SEC_DEBUG
        if (_drawTimeinfo.tm_year > 100) {
            Serial.printf("[CLK SKIP] ms=%lu snap=%02d:%02d:%02d last=%d dots=%d redraw=%d\n", millis(), _drawTimeinfo.tm_hour, _drawTimeinfo.tm_min, _drawTimeinfo.tm_sec,
                          _lastRenderedSecond, (int)_lastRenderedDots, (int)redraw);
        }
#endif
        return;
    }

    applySecClockFont();

    snprintf(_tmp, sizeof(_tmp), "%02d", currentSecond);
    const uint16_t secW = _spr->textWidth(_tmp);
    uint16_t       secH = _spr->fontHeight();

    uint16_t leftSec;
    uint16_t secTop = _secTopSpace;

    if (!rightBlockWidth) { rightBlockWidth = _clockwidth - _linesleft; }
    leftSec = _linesleft + ((int16_t)rightBlockWidth - (int16_t)secW) / 2;
    if (config.store.clockAmPmStyle) {
        // AM/PM felül → sec a jobb blokk alsó részébe igazítva
        secTop = _timeheight - secH;
        if ((int16_t)secTop < 0) secTop = 0;
    } else {
        // Vízszintesen az órával egy vonalban: sec alsó éle = óra alsó éle
        secTop = _timeheight > secH ? _timeheight - secH : 0;
    }

    int16_t  secClearX = _linesleft + 1;
    uint16_t secClearW = (_clockwidth > _linesleft + 1) ? (_clockwidth - _linesleft - 1) : 0;
    uint16_t secClearY = secTop;
    uint16_t secClearH = secH;



    if (secClearX < 0) secClearX = 0;
    if ((uint16_t)secClearX + secClearW > _clockwidth) secClearW = _clockwidth - (uint16_t)secClearX;
    if (secClearY >= _clockheight) secClearY = _clockheight - 1;
    if (secClearY + secClearH > _clockheight) secClearH = _clockheight - secClearY;
    _spr->fillRect((uint16_t)secClearX, secClearY, secClearW, secClearH, config.theme.background);

    if (config.store.clockFontStyle == CLOCKFONT_STYLE_DIGI7 && config.store.clockFontMono) {
        _spr->setTextColor(config.theme.clockbg, config.theme.background);
        _spr->setCursor(leftSec, secTop);
        _spr->print("88");
        _spr->setTextColor(config.theme.seconds);
    } else {
        _spr->setTextColor(config.theme.seconds, config.theme.background);
    }

    _spr->setCursor(leftSec, secTop);
    _spr->print(_tmp);

    // ------------------------------------------------------------
    // 5) Villogó kettőspont
    // ------------------------------------------------------------
    applyMainClockFont();

    _spr->setTextColor(showDots ? config.theme.clock : config.theme.background, config.theme.background);

    if (!showDots) {
        _spr->fillRect(_dotsleft, 0, _dotswidth, _timeheight, config.theme.background);
    } else {
        _spr->setCursor(_dotsleft, 0);
        _spr->print(":");
    }
    // ------------------------------------------------------------
    // 6) Fő sprite kirajzolása
    // ------------------------------------------------------------
#if DSP_MODEL == DSP_AXS15231B
    if (!redraw) {
        auto* pixels = static_cast<uint16_t*>(_spr->getBuffer());
        if (pixels && dsp.blitFrameBlockDeferred(_clockleft, _config.top, _spr->width(), _spr->height(), pixels)) {
            _lastRenderedHour = _drawTimeinfo.tm_hour;
            _lastRenderedMinute = _drawTimeinfo.tm_min;
            _lastRenderedSecond = currentSecond;
            _lastRenderedDots = showDots;
#if CLOCK_WIDGET_SEC_DEBUG
            tm netDbg{};
            network_get_timeinfo_snapshot(&netDbg);
            Serial.printf("[CLK DRAW] ms=%lu snap=%02d:%02d:%02d net=%02d:%02d:%02d redraw=%d deferred=1\n", millis(), _drawTimeinfo.tm_hour, _drawTimeinfo.tm_min,
                          _drawTimeinfo.tm_sec, netDbg.tm_hour, netDbg.tm_min, netDbg.tm_sec, (int)redraw);
#endif
            return;
        }
    }
#endif
    if (_zoom != 1.0f) {
        // Zoom módban: nagy közbenső sprite → egy lépéses push (nincs villogás)
        // Teljes screensaver terület: dsp teljes képernyő
        const uint16_t dsW = dsp.width();
        const uint16_t dsH = dsp.height();

        LGFX_Sprite canvas(&dsp);
        canvas.setColorDepth(16);
        canvas.setPsram(true);
        canvas.createSprite(dsW, dsH);
        canvas.fillSprite(config.theme.background);

        // Zoom-olt óra középre a canvas felső részébe (dátumnak helyet hagyva)
        // Dátum magasság ~26px (vlw_22) + 8px padding = 34px
        const uint16_t dateAreaH = 34;
        const float cx = dsW / 2.0f;
        const float cy = (dsH - dateAreaH) / 2.0f - 10;  // kicsit feljebb, nagyobb zoom miatt
        _spr->pushRotateZoom(&canvas, cx, cy, 0.0f, _zoom, _zoom, config.theme.background);

        // Dátum rajzolása a canvas-ba - fix pozíció alul
        if (_ssDate[0] != '\0') {
            const uint16_t dateY = 271;  // top=265 + vlw_22 félmagasság (~11px)
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(config.theme.date, config.theme.background);
            if (font_vlw_22) {
                canvas.loadFont(font_vlw_22);
                canvas.setTextSize(1);
                canvas.setCursor(dsW / 2 - canvas.textWidth(_ssDate) / 2, dateY - canvas.fontHeight() / 2);
                canvas.print(_ssDate);
                canvas.unloadFont();
            } else {
                canvas.setTextSize(2);
                canvas.setCursor(dsW / 2 - canvas.textWidth(_ssDate) / 2, dateY - canvas.fontHeight() / 2);
                canvas.print(_ssDate);
            }
        }

        canvas.pushSprite(0, 0);
        canvas.deleteSprite();
    } else {
        _spr->pushSprite(_clockleft, _config.top);
    }
    _lastRenderedHour = _drawTimeinfo.tm_hour;
    _lastRenderedMinute = _drawTimeinfo.tm_min;
    _lastRenderedSecond = currentSecond;
    _lastRenderedDots = showDots;
#if CLOCK_WIDGET_SEC_DEBUG
    tm netDbg{};
    network_get_timeinfo_snapshot(&netDbg);
    Serial.printf("[CLK DRAW] ms=%lu snap=%02d:%02d:%02d net=%02d:%02d:%02d redraw=%d\n", millis(), _drawTimeinfo.tm_hour, _drawTimeinfo.tm_min, _drawTimeinfo.tm_sec, netDbg.tm_hour,
                  netDbg.tm_min, netDbg.tm_sec, (int)redraw);
#endif
}

void ClockWidget::_formatDate() {
#if defined(DSP_OLED) && (DSP_MODEL == DSP_SSD1322)
    // ===== SSD1322: rövid numerikus dátum, futásidőben kiválasztható formátum =====
    switch (config.store.dateFormat) {
        case 0:  snprintf(_tmp, sizeof(_tmp), "%04d.%02d.%02d", _drawTimeinfo.tm_year + 1900, _drawTimeinfo.tm_mon + 1, _drawTimeinfo.tm_mday); break; // HU: YYYY.MM.DD
        case 1:  snprintf(_tmp, sizeof(_tmp), "%02d/%02d/%04d", _drawTimeinfo.tm_mon + 1, _drawTimeinfo.tm_mday, _drawTimeinfo.tm_year + 1900); break;   // EN: MM/DD/YYYY
        case 2:  snprintf(_tmp, sizeof(_tmp), "%02d-%02d-%04d", _drawTimeinfo.tm_mday, _drawTimeinfo.tm_mon + 1, _drawTimeinfo.tm_year + 1900); break;   // NL: DD-MM-YYYY
        case 3:  snprintf(_tmp, sizeof(_tmp), "%02d.%02d.%04d", _drawTimeinfo.tm_mday, _drawTimeinfo.tm_mon + 1, _drawTimeinfo.tm_year + 1900); break;   // PL/DE: DD.MM.YYYY
        case 4:  snprintf(_tmp, sizeof(_tmp), "%02d/%02d/%04d", _drawTimeinfo.tm_mday, _drawTimeinfo.tm_mon + 1, _drawTimeinfo.tm_year + 1900); break;   // ES/GR: DD/MM/YYYY
        default: snprintf(_tmp, sizeof(_tmp), "%04d-%02d-%02d", _drawTimeinfo.tm_year + 1900, _drawTimeinfo.tm_mon + 1, _drawTimeinfo.tm_mday); break;   // ISO fallback
    }
    return;
#else
    // ===== MINDEN MÁS KIJELZŐ: hosszú, szöveges forma, futásidőben kiválasztható =====
    switch (config.store.dateFormat) {
        case 0:  snprintf(_tmp, sizeof(_tmp), "%d. %s %2d. %s",   _drawTimeinfo.tm_year + 1900, LANG::mnths[_drawTimeinfo.tm_mon], _drawTimeinfo.tm_mday, LANG::dowf[_drawTimeinfo.tm_wday]); break; // HU: YYYY. MMM DD. DOW
        case 1:  snprintf(_tmp, sizeof(_tmp), "%2d %s %d",         _drawTimeinfo.tm_mday, LANG::mnths[_drawTimeinfo.tm_mon], _drawTimeinfo.tm_year + 1900); break;                                       // EN/RU: DD MMM YYYY
        case 2:  snprintf(_tmp, sizeof(_tmp), "%s %2d %s %d",      LANG::dowf[_drawTimeinfo.tm_wday], _drawTimeinfo.tm_mday, LANG::mnths[_drawTimeinfo.tm_mon], _drawTimeinfo.tm_year + 1900); break; // NL: DOW DD MMM YYYY
        case 3:  snprintf(_tmp, sizeof(_tmp), "%s - %02d. %s. %04d", LANG::dowf[_drawTimeinfo.tm_wday], _drawTimeinfo.tm_mday, LANG::mnths[_drawTimeinfo.tm_mon], _drawTimeinfo.tm_year + 1900); break; // PL: DOW - DD MMM YYYY
        default: snprintf(_tmp, sizeof(_tmp), "%s - %02d. %s. %d",   LANG::dowf[_drawTimeinfo.tm_wday], _drawTimeinfo.tm_mday, LANG::mnths[_drawTimeinfo.tm_mon], _drawTimeinfo.tm_year + 1900); break; // DE/SK/UA/ES/GR: DOW, DD. MMM YYYY
    }
#endif
}
void ClockWidget::_clearClock() {
    if (_spr && _spr->getBuffer()) {
        _spr->fillSprite(config.theme.background);
    }
}

void ClockWidget::draw(bool redraw) {
    if (!_active) { return; }
    _captureTimeSnapshot();
    const bool layoutChanged = _syncLayoutIfNeeded(redraw);
    const bool needTimeRedraw = _getTime();
    const bool sameRenderedSecond = (_lastRenderedSecond == _drawTimeinfo.tm_sec) && (_lastRenderedDots == ((_drawTimeinfo.tm_sec % 2) == 0));
    const bool safeForcedRedraw = redraw && !sameRenderedSecond;
    _printClock(needTimeRedraw || safeForcedRedraw || layoutChanged);
}

void ClockWidget::_draw() {
    if (!_active) { return; }
    _captureTimeSnapshot();
    _syncLayoutIfNeeded(true);
    _printClock(true);
}

void ClockWidget::_reset() {
    if (_spr) {
        if (_spr->getBuffer()) { _spr->deleteSprite(); }
        _getTimeBounds();
        _begin();
    }
}

void ClockWidget::_clear() {
    _clearClock();
    if (_spr && _spr->getBuffer()) _spr->pushSprite(_clockleft, _config.top);
}
