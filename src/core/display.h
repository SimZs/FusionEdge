#pragma once
#include "common.h"

#if DSP_MODEL == DSP_DUMMY
#    define DUMMYDISPLAY
#endif

#ifndef DUMMYDISPLAY
class ScrollWidget;
class PlayListWidget;
class BitrateWidget;
class PlayModeWidget;
class FillWidget;
class Pager;
class Page;
class SpectrumWidget;
class NumWidget;
class ClockWidget;
class TextWidget;
class textBoxWidget;
class SliderWidget;
class VolumeWidget;
class BufferWidget;
class WifiWidget;
class WeatherIconWidget;
class StationIconWidget;
class DateWidget;
class StatusWidget;
class EqWidget;
class CassetteWidget;

class Display {
  public:
    uint16_t      currentPlItem;
    uint16_t      numOfNextStation;
    displayMode_e _mode;

  public:
    Display() {};
    ~Display();
    displayMode_e mode() { return _mode; }
    void          mode(displayMode_e m) { _mode = m; }
    void          init();
    void          loop();
    void          _start();
    bool          ready() { return _bootStep == 2; }
    void          resetQueue();
    void          purgeQueuedRequestType(displayRequestType_e type);
    void          beginContentChange();
    bool          waitQueueEmpty(uint32_t timeoutMs = 300);
    bool          waitContentReady(uint32_t timeoutMs = 300);
    void          putRequest(displayRequestType_e type, int payload = 0);
    void          flip();
    void          invert();
    bool          deepsleep();
    void          wakeup();
    void          setContrast();
    void          lock();
    void          unlock();
    bool          isLocked() const { return _locked; }
    void          i2sReconfigBegin(); // Display DMA guard az I2S rövid újrakonfigurálásához
    void          i2sReconfigEnd();
    void          waitDMA();        // LGFX DMA transzfer befejezésének megvárása (SD/SPI szinkron)
    uint16_t      width();
    uint16_t      height();
    void          setBrightnessPercent(uint8_t percent);
    uint8_t       effectiveBrightnessPercent(uint8_t normalPercent) const;
    bool          clockScreensaverBrightnessActive() const { return _clockScreensaverBrightnessActive; }
    void          applyVuModeChange();
    void          invalidateThemeWidgets();  // Refresh all widgets when theme colors change
    bool          eqToggle();               // EQ overlay toggle, visszaadja az új állapotot
    bool          isEqOpen() const;         // EQ overlay nyitva van-e
    void          eqForceClose();           // EQ overlay kényszerbezárás
    void          _eqInlineDraw();          // EQ overlay újrarajzolása (display_eq_touch hívja)
    bool          tapPlaylistItem(uint16_t y); // Touch tap a playlist területén → kiválasztás

    // --- Mód-választó overlay (MODESELECT) ---
    void          modeSelectorOpen();
    void          modeSelectorScroll(int dir);
    void          modeSelectorConfirm();
    void          modeSelectorClose();
    void          modeSelectorTick();
    // Touchscreen számára szükséges elérés
    uint8_t       msModeCount() const { return _msCount; }
    void          msTapItem(uint8_t idx) { _msIdx = idx; modeSelectorConfirm(); }
    void          msHover(uint8_t idx) {
                      if (_mode != MODESELECT || idx == _msIdx) return;
                      _msIdx = idx;
                      putRequest(DRAWMODESELECT);
                  }
#    ifdef NAMEDAYS_FILE
    void loopDate(bool force = false);
#    endif

  private:
    void _updateStationIcon();
    bool _clockScreensaverBrightnessActive = false;
    ScrollWidget *  _meta, *_title1, *_plcurrent, *_weather, *_title2;
    PlayListWidget* _plwidget;
    BitrateWidget*  _bitratewidget;
    PlayModeWidget* _pmodewidget;
    FillWidget *    _metabackground, *_plbackground;
    SliderWidget *  _volbar;
    Pager*          _pager;
    Page*           _footer;
    SpectrumWidget* _spectrum;
    NumWidget*      _nums;
    ClockWidget*    _clock;
    Page*           _boot;
    TextWidget *    _volip, *_voltxt, *_rssi, *_bitrate, *_chtxt;
    textBoxWidget * _ipbox, *_rssibox, *_bootstring;
    VolumeWidget*   _volwidget;
    BufferWidget*   _bufferwidget;
    WifiWidget*     _wifiwidget;
    WeatherIconWidget*  _weatherIcon;
    StationIconWidget*  _stationIcon;
    DateWidget*     _datewidget;
    StatusWidget*   _statuswidget;
    EqWidget*       _eqwidget;
#    ifdef USE_CASSETTE_SCREENSAVER
    CassetteWidget* _cassettewidget = nullptr;
#    endif

    bool     _locked = false;
    bool     _panelAwake = true;
    uint8_t  _bootStep;
    int      _lastRssiText = 9999;
    uint32_t _lastRssiTextMs = 0;

    // --- Mód-választó overlay state (publikus, touchscreen.cpp éri el) ---
    static constexpr uint8_t MS_MAX_MODES = 5;
    uint8_t  _msCount      = 0;
    uint8_t  _msIdx        = 0;
    int8_t   _msValues[MS_MAX_MODES];     // mód értékek (-1 = DLNA)
    const char* _msLabels[MS_MAX_MODES];  // mód feliratok
    uint32_t _msAutoCloseMs = 0;          // auto-bezárás időpontja
    void     _msBuildList();              // elérhető módok listájának összeállítása
    void     _msDraw();                   // overlay rajzolása
    void     _time(bool redraw = false);
    void     _apScreen();
    void     _swichMode(displayMode_e newmode);
    void     _drawPlaylist();
    void     _volume();
    void     _ssUpdateDate();
    void     _volInlineShow();
    void     _volInlineHide();
    void     _volInlineDraw();
    void     _eqInlineShow();
    void     _eqInlineHide();
    void     _title();
    void     _station();
    void     _drawNextStationNum(uint16_t num);
    void     _createDspTask();
    void     _showDialog(const char* title);
    void     _sdProgressDraw(int count);   // SD indexelés progress a VOL_AREA sávban
    void     _buildPager();
    void     _bootScreen();
    void     _layoutChange(bool played);
    void     _setRSSI(int rssi);
    void     _updateBootSprite(int ssidIndex);
    void     _refreshThemeColors();
    void     _applyRssiMode();
};

#else

class Display {
  public:
    uint16_t      currentPlItem;
    uint16_t      numOfNextStation;
    displayMode_e _mode;

  public:
    Display() {};
    displayMode_e mode() { return _mode; }
    void          mode(displayMode_e m) { _mode = m; }
    void          init();
    void          _start();
    void          putRequest(displayRequestType_e type, int payload = 0);
    void          loop() {}
    bool          ready() { return true; }
    void          resetQueue() {}
    void          purgeQueuedRequestType(displayRequestType_e type) {}
    void          beginContentChange() {}
    bool          waitQueueEmpty(uint32_t timeoutMs = 0) { return true; }
    bool          waitContentReady(uint32_t timeoutMs = 0) { return true; }
    void          centerText(const char* text, uint8_t y, uint16_t fg, uint16_t bg) {}
    void          rightText(const char* text, uint8_t y, uint16_t fg, uint16_t bg) {}
    void          flip() {}
    void          invert() {}
    void          setContrast() {}
    void          applyVuModeChange() {}
    bool          deepsleep() { return true; }
    void          wakeup() {}
    void          printPLitem(uint8_t pos, const char* item) {}
    void          lock() {}
    void          unlock() {}
    bool          isLocked() const { return false; }
    uint16_t      width() { return 0; }
    uint16_t      height() { return 0; }
    void          setBrightnessPercent(uint8_t percent) {}
    uint8_t       effectiveBrightnessPercent(uint8_t normalPercent) const { return normalPercent > 100 ? 100 : normalPercent; }
    bool          clockScreensaverBrightnessActive() const { return false; }

  private:
    void _createDspTask();
};

#endif

void display_show_maintenance_screen();
void display_eq_touch(uint16_t x, uint16_t y, bool drag);

// FusionEdge: Auto EQ – genre-name alapú EQ preset keresés (case-insensitive).
// A preset nevek/értékek a display.cpp-ben élnek (eqPresetNames/eqPresetValues).
// Visszatér true-val és kitölti bass/mid/treb-et, ha talál egyezést, egyébként
// false-szal (a kimeneti paraméterek ilyenkor nem módosulnak).
bool findEqPresetByGenre(const char* genre, int8_t& bass, int8_t& mid, int8_t& treb);

extern Display display;
