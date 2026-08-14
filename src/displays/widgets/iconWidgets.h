#pragma once
#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY

#include <LovyanGFX.hpp>
#include "widgetsconfig.h"
#include "../display_select.h"
#include "widget.h"

// ── WeatherIconWidget ────────────────────────────────────────────────────────
// 64×64px PNG ikonok a /images/weather/ mappából (pl. "01d.png", "10n.png")
// A hőmérséklet szöveg az ikon bal oldalán jelenik meg, saját theme színnel.
class WeatherIconWidget : public Widget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t bgcolor);
    void setWeather(const char* code, float tempC);
    void setIcon(const char* code);   // OpenWeatherMap icon kód pl. "01d"
    void setTemp(float tempC);
    void clearArea() { if (_active) _clear(); }
    ~WeatherIconWidget();

    static constexpr uint16_t ICO_W = 64;
    static constexpr uint16_t ICO_H = 64;

  protected:
    char     _iconCode[8] = {0};
    char     _temp[10]    = {0};
    bool     _hasTemp     = false;
    uint8_t* _pngBuf      = nullptr;
    size_t   _pngSize     = 0;
    uint16_t _bgcolor     = 0;

    bool _loadPng(const char* code);
    void _freePng();
    void _draw()  override;
    void _clear() override;
    void _reset() override {}
};

// ── StationIconWidget ────────────────────────────────────────────────────────
// 80×80px PNG logók a /images/stations/ mappából.
// A map.csv (tab-elválasztott: állomásnév<TAB>fájlnév) alapján keres.
// Ha nincs találat: plmodeweb/plmodesd/plmodedlna.png fallback.
class StationIconWidget : public Widget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t bgcolor);
    void setStation(const char* stationName, uint8_t playMode);
#ifdef USE_LASTFM_COVER
    void setCover(uint8_t* data, size_t size, bool jpeg, uint32_t generation);
#endif
    void clearStation();
    ~StationIconWidget();

    static constexpr uint16_t ICO_W = 80;
    static constexpr uint16_t ICO_H = 80;

  protected:
    uint8_t* _buf     = nullptr;
    size_t   _sz      = 0;
    bool     _valid   = false;
    bool     _dirty   = true;
    uint16_t _bgcolor = 0;
    LGFX_Sprite* _spr = nullptr;
    char _path[64] = {0};
#ifdef USE_LASTFM_COVER
    bool _jpeg = false;
    uint16_t _imageWidth = ICO_W;
    uint16_t _imageHeight = ICO_H;
    uint32_t _coverGeneration = 0;
#endif

    bool _loadPng(const char* path);
#ifdef USE_LASTFM_COVER
    void _setImageSize();
#endif
    void _freePng();
    void _ensureSprite();
    void _deleteSprite();
    void _pushSprite();
    void _draw()  override;
    void _clear() override;
    void _reset() override { _dirty = true; }
};

#endif // DSP_MODEL != DSP_DUMMY
