#include "options.h"
#if (TS_MODEL != TS_MODEL_UNDEFINED) && (DSP_MODEL != DSP_DUMMY)
#    include "Arduino.h"
#    include "touchscreen.h"
#    include "config.h"
#    include "controls.h"
#    include "display.h"
#    include "player.h"
#    include "presets.h"
#    include "../displays/display_select.h" // DspCore + extern dsp
#    include "../pluginsManager/pluginsManager.h"
#    include "../plugins/backlight/backlight.h"
#    include "commandhandler.h"
#    include "fonts.h"
#    include "netserver.h"

#    ifndef TS_STEPS
#        define TS_STEPS 40
#    endif

// ── Widget területek (conf_480x320.h konstansokkal szinkronban) ──────────────
// StatusWidget: left=127, top=105, SW=50, SGAP=8, SH=25
#    define SW_STATUS_L    127
#    define SW_STATUS_T    105
#    define SW_STATUS_B    130   // top + SH
#    define SW_STATUS_SLOT  58   // SW(50) + SGAP(8) — egy slot szélessége

// PlayMode widget: pmodeConf left=10, top=105, dim=50 → ~110×25px
#    define SW_PMODE_L     10
#    define SW_PMODE_R    120   // left + 2*dim ≈ 110 + kis margó
#    define SW_PMODE_T    105
#    define SW_PMODE_B    131
// Station icon: left=TFT_FRAMEWDT(10), top=15, W=80, H=80 → 10..90 × 15..95
// A preset sáv (y<50) megelőzi, ezért az ikon alsó fele (y=50..95) effektív playmode zóna
#    define SW_PMODE_ICON_L   10
#    define SW_PMODE_ICON_R   90
#    define SW_PMODE_ICON_T   50   // y<50 = preset zóna, tehát onnan lefelé
#    define SW_PMODE_ICON_B   95

// ClockWidget: clockConf left=280, top=157, height≈106px (157+106=263)
#    define SW_CLOCK_L    280
#    define SW_CLOCK_R    480
#    define SW_CLOCK_T    157
#    define SW_CLOCK_B    263

// SpectrumWidget: left=TFT_FRAMEWDT(10), top=157, width=266, height=106
#    define SW_SPEC_L     10
#    define SW_SPEC_R    266
#    define SW_SPEC_T    157
#    define SW_SPEC_B    263

// Meta+Title terület (Stop/Start zóna)
#    define SW_META_T      50
#    define SW_META_B     135

// VOL/Playlist swipe zóna – a VOL_AREA_TOP-tól lefelé
// (VOL_AREA_TOP = 137, def. display.cpp-ben)
#    define SW_VOL_T      137

void TouchScreen::init(uint16_t w, uint16_t h) {
    // A LovyanGFX a dsp.init() során már inicializálta a touch IC-t a buszon.
    _width = w;
    _height = h;
}

tsDirection_e TouchScreen::_tsDirection(uint16_t x, uint16_t y) {
    int16_t dX = x - _oldTouchX;
    int16_t dY = y - _oldTouchY;
    // Az eredeti üzleti logika szerinti 20 pixeles küszöb
    if (abs(dX) > 20 || abs(dY) > 20) {
        if (abs(dX) > abs(dY)) {
            return (dX > 0) ? TSD_RIGHT : TSD_LEFT;
        } else {
            return (dY > 0) ? TSD_DOWN : TSD_UP;
        }
    }
    return TDS_REQUEST;
}

// ── Segédfüggvények ──────────────────────────────────────────────────────────

// Megadja hogy x,y beleesik-e a [left..right) × [top..bottom) téglalapba
static inline bool _inRect(uint16_t x, uint16_t y,
                            uint16_t left, uint16_t right,
                            uint16_t top,  uint16_t bottom) {
    return (x >= left && x < right && y >= top && y < bottom);
}

// Ciklikusan lépteti a clockFontStyle-t (0→1→2→3→4→5→6→0)
static void _touchCycleClockFont() {
    uint8_t style = config.store.clockFontStyle;
    style = (style + 1) % (CLOCKFONT_STYLE_SQUAREFONT + 1);
    char val[4];
    snprintf(val, sizeof(val), "%d", (int)style);
    cmd.exec("clockfont", val);
}

// Ciklikusan lépteti a vuSpecMode-ot (0→1→2→3→0)
static void _touchCycleVuStyle() {
    uint8_t mode = config.store.vuSpecMode;
    mode = (mode + 1) % 4;
    char val[4];
    snprintf(val, sizeof(val), "%d", (int)mode);
    cmd.exec("vustyle", val);
}

// vuPeakOn toggle
static void _touchToggleVuPeak() {
    uint8_t newVal = config.store.vuPeakOn ? 0 : 1;
    char val[4];
    snprintf(val, sizeof(val), "%d", (int)newVal);
    cmd.exec("vupeak", val);
}

// StatusWidget slot hit-test → 0=FADE, 1=TTS, 2=RGB, -1=miss
static int8_t _statusHit(uint16_t x, uint16_t y) {
    if (!_inRect(x, y, SW_STATUS_L, SW_STATUS_L + SW_STATUS_SLOT * 3, SW_STATUS_T, SW_STATUS_B))
        return -1;
    return (int8_t)((x - SW_STATUS_L) / SW_STATUS_SLOT);
}

// StatusWidget egyes slot cycle-ja
// FADE:  0=ki → 1=be → 0=ki → ...  (2 állapot)
// TTS:   0=ki → 1=be → 2=be+onlyWhenNoStream → 0=ki → ...  (3 állapot)
// RGB:   0=ki → 1=be → 2=be+duringScreensaver → 0=ki → ...  (3 állapot)
static void _touchToggleStatus(int8_t slot) {
    switch (slot) {
        case 0: { // FADE – 2 állapot
            uint8_t newVal = config.store.fadeEnabled ? 0 : 1;
            char val[4];
            snprintf(val, sizeof(val), "%d", (int)newVal);
            cmd.exec("fadeenabled", val);
            break;
        }
        case 1: { // TTS – 3 állapot: ki → be → be+only → ki
            bool en   = config.store.clockTtsEnabled;
            bool only = config.store.clockTtsOnlyWhenNoStream;
            if (!en) {
                // ki → be
                cmd.exec("clocktts", "1");
                config.saveValue(&config.store.clockTtsOnlyWhenNoStream, false);
                display.putRequest(INVALIDATETHEMEWIDGETS);
            } else if (!only) {
                // be → be+only
                config.saveValue(&config.store.clockTtsOnlyWhenNoStream, true);
                display.putRequest(INVALIDATETHEMEWIDGETS);
            } else {
                // be+only → ki
                cmd.exec("clocktts", "0");
                config.saveValue(&config.store.clockTtsOnlyWhenNoStream, false);
                display.putRequest(INVALIDATETHEMEWIDGETS);
            }
            break;
        }
        case 2: { // RGB – 3 állapot: ki → be → be+screensaver → ki
            uint8_t en = config.store.lsEnabled;
            uint8_t ss = config.store.lsSsEnabled;
            if (!en) {
                // ki → be
                cmd.exec("lsenabled", "1");
                cmd.exec("lsssena",   "0");
            } else if (!ss) {
                // be → be+screensaver
                cmd.exec("lsssena", "1");
            } else {
                // be+screensaver → ki
                cmd.exec("lsenabled", "0");
                cmd.exec("lsssena",   "0");
            }
            break;
        }
        default: break;
    }
}

// PlayMode ciklikus váltás:
//   USE_SD + USE_DLNA:  WEB → SD → DLNA → WEB → ...
//   USE_SD csak:        WEB → SD → WEB → ...
//   Egyik sem:         nincs hatás
static void __attribute__((unused)) _touchCyclePlayMode() {
#    if defined(USE_SD) || defined(USE_DLNA)

    // Aktuális logikai állapot meghatározása
    // Állapot 0 = WEB, 1 = SD, 2 = DLNA
    uint8_t curState = 0;
#    ifdef USE_SD
    if (config.getMode() == PM_SDCARD) {
        curState = 1;
    } else
#    endif
    { // FusionEdge: explicit blokk, hogy a fenti "else" ne nyelje el a következő
      // utasítást, ha USE_DLNA nincs definiálva (és ez a blokk üresen maradna)
#    ifdef USE_DLNA
    if (config.store.playlistSource == PL_SRC_DLNA) {
        curState = 2;
    }
#    endif
    }

    // Következő állapot meghatározása
    uint8_t nextState;
    switch (curState) {
        case 0: // WEB → SD (ha van SD) vagy DLNA (ha nincs SD, de van DLNA) vagy marad WEB
#    ifdef USE_SD
            nextState = 1; // WEB → SD
#    elif defined(USE_DLNA)
            nextState = 2; // WEB → DLNA
#    else
            nextState = 0;
#    endif
            break;
        case 1: // SD →
#    ifdef USE_DLNA
            nextState = 2; // SD → DLNA
#    else
            nextState = 0; // SD → WEB
#    endif
            break;
        case 2: // DLNA → WEB
        default:
            nextState = 0;
            break;
    }

    // Átváltás végrehajtása az állapotnak megfelelő logikával
    if (nextState == 1) {
        // → SD
#    ifdef USE_SD
        config.changeMode(PM_SDCARD);
        netserver.requestOnChange(GETPLAYERMODE, 0);
        netserver.requestOnChange(GETINDEX, 0);
#    endif
    } else if (nextState == 2) {
        // → DLNA (PM_WEB + PL_SRC_DLNA)
#    ifdef USE_DLNA
#        ifdef USE_SD
        if (config.getMode() == PM_SDCARD) { config.changeMode(PM_WEB); }
#        endif
        config.store.playlistSource = PL_SRC_DLNA;
        config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_DLNA);
        config.indexDLNAPlaylist();
        config.initDLNAPlaylist();
        netserver.requestOnChange(GETINDEX, 0);
        netserver.requestOnChange(GETPLAYERMODE, 0);
#    endif
    } else {
        // → WEB (PM_WEB + PL_SRC_WEB)
        config.store.playlistSource = PL_SRC_WEB;
        config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_WEB);
#    ifdef USE_SD
        if (config.getMode() == PM_SDCARD) { config.changeMode(PM_WEB); }
        else {
#    endif
            config.indexPlaylist();
            config.initPlaylist();
            netserver.requestOnChange(GETINDEX, 0);
#    ifdef USE_SD
        }
#    endif
        netserver.requestOnChange(GETPLAYERMODE, 0);
    }

#    endif // USE_SD || USE_DLNA
}

// ─────────────────────────────────────────────────────────────────────────────

void TouchScreen::loop() {
    uint16_t             touchX = 0, touchY = 0;
    static uint32_t      touchLongPress;
    static tsDirection_e direct;
    static uint16_t      touchVol, touchStation;
#    if (DSP_WIDTH == 480) && (DSP_HEIGHT == 320)
    static uint32_t presetsLastActivity = 0;
    static int      presetActionDone = 0; // 0=play, 1=save, 2=del
    static int      presetHoldSlot = -1;
    static int      favHold = -1;
    static bool     favLongTriggered = false;
    static uint32_t lastTouchMs = 0;
    static bool     specLongTriggered = false;  // spectrum long-press már kisült?
#    endif
    static bool lastStTouched = false;

    if (!_checklpdelay(20, _touchdelay)) { return; }

    // ---- LovyanGFX touch beolvasás ----
    uint16_t x = 0, y = 0;
    bool     istouched = dsp.getTouch(&x, &y);

    // CSAK AKKOR frissítsük a touchX/Y-t, ha tényleg érintés van!
    // Ha istouched hamis, akkor a touchX/Y maradjon az előző érvényes érték.
    if (istouched) {
        if(config.store.dbgtouch) {
            Serial.printf("[TOUCH] raw x=%d y=%d  screen=%dx%d\n", x, y, _width, _height);
        }
        touchX = x;
        touchY = y;

        if (config.store.xTouchMirroring) {
            touchX = _width - 1 - touchX;
        }
        if (config.store.yTouchMirroring) {
            touchY = _height - 1 - touchY;
        }

        lastTouchMs = millis();
    }

    bool stTouched = (istouched || ((uint32_t)(millis() - lastTouchMs) < 120));
    bool newTouch = (stTouched && !lastStTouched);
    bool endTouch = (!stTouched && lastStTouched);

#    if (DSP_WIDTH == 480) && (DSP_HEIGHT == 320)
    // Auto-exit presets screen after 15s
    if (display.mode() == PRESETS) {
        if (presets_toastExpired()) { return; }
        if (presetsLastActivity == 0) { presetsLastActivity = millis(); }
        if (stTouched) {
            presetsLastActivity = millis();
        } else if ((uint32_t)(millis() - presetsLastActivity) > 15000UL) {
            display.putRequest(NEWMODE, PLAYER);
            presetsLastActivity = 0;
            direct = TSD_STAY;
            return;
        }
    } else {
        presetsLastActivity = 0;
    }
#    endif

    if (stTouched) {
        if (newTouch) { /************************** START TOUCH *************************************/
            // Screensaverből kilépés bármilyen érintésre
            if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
                display.putRequest(NEWMODE, PLAYER);
                direct = TSD_STAY;
                lastStTouched = stTouched;
                return;
            }

            _oldTouchX = touchX;
            _oldTouchY = touchY;
            touchVol = touchX;
            touchStation = touchY;
            direct = TDS_REQUEST;
            touchLongPress = millis();
            specLongTriggered = false;

#    if (DSP_WIDTH == 480) && (DSP_HEIGHT == 320)
            if (display.mode() == PRESETS && !presets_keyboardActive()) {
                presetHoldSlot = presets_hitTest(touchX, touchY);
                presetActionDone = 0;
                presets_setPressedSlot(presetHoldSlot);
                presets_drawPressed();
                favHold = presets_hitTestFav(touchX, touchY);
                favLongTriggered = false;
            }
#    endif
        } else { /************************** HOLD / SWIPE TOUCH *************************************/
            if (display.mode() == PRESETS) {
                direct = TDS_REQUEST;
            } else {
                // KRITIKUS: Csak akkor nézzük a mozgást, ha a hardver MÉG ÉRZÉKELI az ujjad (istouched)
                if (_oldTouchY >= 50 && istouched) {

                    int16_t distX = abs(touchX - _oldTouchX);
                    int16_t distY = abs(touchY - _oldTouchY);

                    if (distX < 30 && distY < 30) {
                        direct = TDS_REQUEST;
                        // MODESELECT: azonnal kijelöli azt az elemet, ami az ujj alatt van
                        if (display.mode() == MODESELECT) {
                            constexpr uint16_t MS_TOP  = 137;
                            constexpr uint16_t MS_H    = 149;
                            constexpr uint16_t LIST_OFF = 35;
                            uint8_t cnt = display.msModeCount();
                            if (cnt > 0 && touchY >= (MS_TOP + LIST_OFF) && touchY < (MS_TOP + MS_H)) {
                                uint16_t itemH = (MS_H - 40) / cnt;
                                uint8_t  hovered = (touchY - (MS_TOP + LIST_OFF)) / itemH;
                                if (hovered < cnt) { display.msHover(hovered); }
                            }
                        }
                    } else {
                        direct = _tsDirection(touchX, touchY);
                    }

                    switch (direct) {
                        case TSD_LEFT:
                        case TSD_RIGHT: {
                            // EQ overlay drag
                            if (display.mode() == PLAYER && display.isEqOpen() &&
                                touchY >= 137 && touchY < 287) {
                                display_eq_touch(touchX, touchY, true);
                                break;
                            }
                            touchLongPress = millis() - 5000;

                            // VOL swipe: csak a VOL_AREA zónában (y >= SW_VOL_T)
                            if ((display.mode() == PLAYER || display.mode() == VOL) &&
                                _oldTouchY >= SW_VOL_T) {
                                int16_t xDelta = map(abs(touchVol - touchX), 0, _width, 0, TS_STEPS);
                                if (xDelta >= 1) {
                                    display.putRequest(NEWMODE, VOL);
                                    controlsEvent((touchVol - touchX) < 0);
                                    touchVol = touchX;
                                }
                            }
                            break;
                        }
                        case TSD_UP:
                        case TSD_DOWN: {
                            touchLongPress = millis() - 5000;
                            // MODESELECT swipe: görgetés a módok között
                            if (display.mode() == MODESELECT) {
                                int16_t yDelta = map(abs(touchStation - touchY), 0, _height, 0, TS_STEPS);
                                if (yDelta >= 1) {
                                    display.modeSelectorScroll((touchStation - touchY) < 0 ? 1 : -1);
                                    touchStation = touchY;
                                }
                                break;
                            }
                            // STATIONS swipe: csak a VOL_AREA zónában (y >= SW_VOL_T)
                            if ((display.mode() == PLAYER || display.mode() == STATIONS) &&
                                _oldTouchY >= SW_VOL_T) {
                                int16_t yDelta = map(abs(touchStation - touchY), 0, _height, 0, TS_STEPS);
                                if (yDelta >= 1) {
                                    display.putRequest(NEWMODE, STATIONS);
                                    controlsEvent((touchStation - touchY) < 0);
                                    touchStation = touchY;
                                }
                            }
                            break;
                        }
                        default: break;
                    }
                }
            }
        }
/********************************** PRESETS HOLD (real-time) ************************************/
#    if (DSP_WIDTH == 480) && (DSP_HEIGHT == 320)
        if (display.mode() == PRESETS && !presets_keyboardActive()) {
            uint32_t held = millis() - touchLongPress;

            if (favHold >= 0 && !favLongTriggered) {
                if (held >= BTN_PRESS_TICKS * 2) {
                    presets_keyboardOpen((uint8_t)favHold);
                    presets_drawScreen();
                    favLongTriggered = true;
                    direct = TSD_STAY;
                }
            }

            if (presetHoldSlot >= 0) {
                presets_drawHoldBar(held);
                presets_setPressedSlot(presetHoldSlot);
                presets_drawPressed();

                if (held >= BTN_PRESS_TICKS * 4)
                    presetActionDone = 2; // DEL
                else if (held >= BTN_PRESS_TICKS * 2)
                    presetActionDone = 1; // SAVE
                else
                    presetActionDone = 0; // PLAY
            }
        }

        // ── Spectrum widget hosszú érintés: peak toggle ──────────────────────
        if (display.mode() == PLAYER && !specLongTriggered && direct == TDS_REQUEST) {
            if (_inRect(_oldTouchX, _oldTouchY, SW_SPEC_L, SW_SPEC_R, SW_SPEC_T, SW_SPEC_B)) {
                uint32_t held = millis() - touchLongPress;
                if (held >= (uint32_t)BTN_PRESS_TICKS * 2) {
                    _touchToggleVuPeak();
                    specLongTriggered = true;
                    direct = TSD_STAY;   // ne triggerelj tap-et is
                }
            }
        }

        // ── PlayMode widget hosszú érintés: már nincs akció (tap nyitja a MODESELECT menüt) ────
#    endif
    } else {
        if (endTouch) { /**************************** END TOUCH *********************************/
            if (direct == TDS_REQUEST) {
                uint32_t pressTicks = millis() - touchLongPress;

                // ── MODESELECT: minden touch event IDE fut, más handler nem kapja meg ──
                if (display.mode() == MODESELECT) {
                    // VOL_AREA: y=137..286 (VOL_AREA_TOP=137, VOL_AREA_HEIGHT=149, defined in display.cpp)
                    constexpr uint16_t MS_AREA_TOP = 137;
                    constexpr uint16_t MS_AREA_H   = 149;
                    if (_oldTouchY >= MS_AREA_TOP && _oldTouchY < (MS_AREA_TOP + MS_AREA_H)) {
                        uint16_t listTop = MS_AREA_TOP + 35;
                        uint8_t  cnt     = display.msModeCount();
                        if (cnt > 0 && _oldTouchY >= listTop) {
                            uint16_t itemH = (MS_AREA_H - 40) / cnt;
                            int8_t tapped  = (_oldTouchY - listTop) / itemH;
                            if (tapped >= 0 && tapped < (int8_t)cnt) {
                                display.msTapItem((uint8_t)tapped);
                                goto finish_touch;
                            }
                        }
                    }
                    display.modeSelectorClose();
                    goto finish_touch;
                }

#    if (DSP_WIDTH == 480) && (DSP_HEIGHT == 320)
                presets_clearPressed();
                presets_setPressedSlot(-1);

                // 1) PRESETS HÍVÁS: Felső sáv (50px) érintése a főképernyőn
                if (_oldTouchY < 50 && pressTicks > 50 && pressTicks < BTN_PRESS_TICKS * 2) {
                    if (display.mode() == PLAYER) {
                        display.putRequest(NEWMODE, PRESETS);
                        presetsLastActivity = millis();
                    } else if (display.mode() == PRESETS) {
                        display.putRequest(NEWMODE, PLAYER);
                        presetsLastActivity = 0;
                    }
                    goto finish_touch;
                }

                // 2) PRESETS MÓD BELSŐ KEZELÉSE
                if (display.mode() == PRESETS) {
                    if (presets_keyboardActive()) {
                        if (pressTicks > 50) presets_keyboardTap(_oldTouchX, _oldTouchY);
                    } else {
                        int fav = presets_hitTestFav(_oldTouchX, _oldTouchY);
                        if (fav >= 0) {
                            if (!favLongTriggered && pressTicks > 50) presets_selectBank((uint8_t)fav);
                            presets_drawScreen();
                        } else {
                            int hit = presets_hitTest(_oldTouchX, _oldTouchY);
                            if (hit >= 0) {
                                if (presetActionDone == 2)
                                    presets_clear((uint8_t)hit);
                                else if (presetActionDone == 1)
                                    presets_save((uint8_t)hit);
                                else {
                                    if (!presets_play((uint8_t)hit)) display.putRequest(NEWMODE, PLAYER);
                                }
                            }
                        }
                    }
                    goto finish_touch;
                }

                // 2b) EQ GOMB: top=105..131, left=310..360
                if (display.mode() == PLAYER &&
                    pressTicks > 50 && pressTicks < BTN_PRESS_TICKS * 2 &&
                    _oldTouchX >= 310 && _oldTouchX < 360 &&
                    _oldTouchY >= 105 && _oldTouchY < 131) {
                    display.eqToggle();
                    goto finish_touch;
                }

                // 2c) EQ OVERLAY belső érintés (137..286px sáv, ha EQ nyitva van)
                if (display.mode() == PLAYER && display.isEqOpen() &&
                    pressTicks > 50 && pressTicks < BTN_PRESS_TICKS * 2 &&
                    _oldTouchY >= 137 && _oldTouchY < 287) {
                    display_eq_touch(_oldTouchX, _oldTouchY, false);
                    goto finish_touch;
                }

                // Ha EQ nyitva van, innentől ne kezeljük a többi widgetet
                if (display.isEqOpen()) goto finish_touch;

                // ── Widget tap-ok: csak rövid, egyértelmű érintés ────────────
                if (pressTicks > 50 && pressTicks < BTN_PRESS_TICKS * 2) {

                    // STATIONS mód: playlist elem kiválasztása touch-al
                    if (display.mode() == STATIONS && _oldTouchY >= SW_VOL_T) {
                        display.tapPlaylistItem(_oldTouchY);
                        goto finish_touch;
                    }

                    // 3) StatusWidget tap (FADE / TTS / RGB)
                    {
                        int8_t slot = _statusHit(_oldTouchX, _oldTouchY);
                        if (slot >= 0) {
                            _touchToggleStatus(slot);
                            goto finish_touch;
                        }
                    }

                    // 4) PlayModeWidget tap → mód-választó menü megnyitása
                    if (_inRect(_oldTouchX, _oldTouchY, SW_PMODE_L, SW_PMODE_R, SW_PMODE_T, SW_PMODE_B) ||
                        _inRect(_oldTouchX, _oldTouchY, SW_PMODE_ICON_L, SW_PMODE_ICON_R, SW_PMODE_ICON_T, SW_PMODE_ICON_B)) {
                        display.modeSelectorOpen();
                        goto finish_touch;
                    }

                    // 5) ClockWidget tap → ciklikus óra-font váltás
                    if (_inRect(_oldTouchX, _oldTouchY, SW_CLOCK_L, SW_CLOCK_R, SW_CLOCK_T, SW_CLOCK_B)) {
                        _touchCycleClockFont();
                        goto finish_touch;
                    }

                    // 6) SpectrumWidget tap → vuStyle ciklus (long press már specLongTriggered-be ment)
                    if (!specLongTriggered &&
                        _inRect(_oldTouchX, _oldTouchY, SW_SPEC_L, SW_SPEC_R, SW_SPEC_T, SW_SPEC_B)) {
                        _touchCycleVuStyle();
                        goto finish_touch;
                    }

                    // 7) START/STOP – title1+title2 sáv (y: 50..105) IGEN, widget sor (y: 105..131) NEM
                    // A SW_STATUS_T..SW_STATUS_B sáv (105..130) kizárva: ott van pmode/status/EQ/bitrate
                    if (_oldTouchY >= SW_META_T && _oldTouchY < SW_META_B &&
                        !(_oldTouchY >= SW_STATUS_T && _oldTouchY < SW_STATUS_B)) {
                        bool consumedByPlugin = false;
#    if (BRIGHTNESS_PIN != 255)
                        if (config.store.fadeEnabled && backlightPlugin.isFadeControl()) {
                            consumedByPlugin = true;
                        }
#    endif
                        if (!consumedByPlugin) {
                            pm.on_user_activity(consumedByPlugin);
                        }
                        if (consumedByPlugin) {
                            // Csak a fény jön vissza
                        } else {
                            onBtnClick(EVT_BTNCENTER); // STOP / START toggle
                        }
                        goto finish_touch;
                    }
                }
#    endif
            }

        finish_touch:
#    if (DSP_WIDTH == 480) && (DSP_HEIGHT == 320)
            presetHoldSlot = -1;
            presetActionDone = 0;
            specLongTriggered = false;
#    endif
            direct = TSD_STAY;
        }
    }
    lastStTouched = stTouched;
}

bool TouchScreen::_checklpdelay(int m, uint32_t& tstamp) {
    if (millis() - tstamp > (uint32_t)m) {
        tstamp = millis();
        return true;
    }
    return false;
}

#endif
