#pragma once

#include "../../core/options.h"

#if DSP_MODEL != DSP_DUMMY && defined(USE_COVER_SCREENSAVER)

#include <LovyanGFX.hpp>

#include "widget.h"

class CoverScreensaverWidget : public Widget {
  public:
    using Widget::init;

    ~CoverScreensaverWidget() override;

    void init(WidgetConfig config, uint16_t background);
    void setColors(uint16_t foreground, uint16_t background) override;
    void setCover(uint8_t* data, size_t size, bool jpeg, uint32_t generation);
    void setStation(const char* stationName, uint8_t playMode);
    void setTime(const char* time);
    void setDate(const char* date);
    void setTrack(const char* artist, const char* title);
    void setArtist(const char* artist);
    void setTitle(const char* title);
    void setWeather(const char* code, float temperature);
    bool ready() const { return _sprite && _sprite->getBuffer(); }

    static constexpr uint16_t COVER_WIDTH = 320;
    static constexpr uint16_t PANEL_WIDTH = 160;
    static constexpr uint16_t HEIGHT = 320;

  protected:
    uint8_t* _imageData = nullptr;
    size_t _imageSize = 0;
    bool _imageValid = false;
    bool _imageJpeg = false;
    bool _imageDirty = true;
    bool _infoDirty = true;
    uint16_t _imageWidth = COVER_WIDTH;
    uint16_t _imageHeight = HEIGHT;
    uint32_t _coverGeneration = 0;
    char _imagePath[96] = {};

    uint8_t* _weatherData = nullptr;
    size_t _weatherSize = 0;
    char _weatherCode[8] = {};

    char _time[8] = {};
    char _date[192] = {};
    char _artist[96] = {};
    char _title[96] = {};
    char _temperature[16] = {};

    LGFX_Sprite* _sprite = nullptr;

    void _draw() override;
    void _clear() override;
    void _reset() override { _imageDirty = true; _infoDirty = true; }

    void _ensureSprite();
    void _deleteSprite();
    void _pushSprite();
    void _freeImage();
    void _freeWeather();
    void _detectImageSize();
    bool _loadImage(const char* path);
    bool _loadWeather(const char* code);
};

#endif
