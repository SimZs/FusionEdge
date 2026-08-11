#pragma once

#include "../../core/options.h"

#if DSP_MODEL != DSP_DUMMY && defined(USE_CASSETTE_SCREENSAVER)

#include "widget.h"

class CassetteWidget : public Widget {
  public:
    CassetteWidget() = default;
    ~CassetteWidget();

    using Widget::init;
    void init(WidgetConfig conf, uint16_t fgcolor, uint16_t bgcolor) override;
    void loop() override;
    void setColors(uint16_t fg, uint16_t bg) override;
    void setTextColors(uint16_t trackColor, uint16_t audioInfoColor) {
        _trackTextColor = trackColor;
        _audioInfoColor = audioInfoColor;
    }

  protected:
    void _draw() override;
    void _clear() override;
    void _reset() override;

  private:
    LGFX_Sprite* _backgroundSprite = nullptr;
    LGFX_Sprite* _reelSprite = nullptr;
    bool         _pngReady = false;

    uint16_t _screenW = 0;
    uint16_t _screenH = 0;
    int16_t  _bodyX = 0;
    int16_t  _bodyY = 0;
    int16_t  _bodyW = 0;
    int16_t  _bodyH = 0;
    int16_t  _labelX = 0;
    int16_t  _labelY = 0;
    int16_t  _labelW = 0;
    int16_t  _labelH = 0;
    int16_t  _infoX = 0;
    int16_t  _infoY = 0;
    int16_t  _infoW = 0;
    int16_t  _infoH = 0;
    int16_t  _windowX = 0;
    int16_t  _windowY = 0;
    int16_t  _windowW = 0;
    int16_t  _windowH = 0;
    int16_t  _leftReelX = 0;
    int16_t  _rightReelX = 0;
    int16_t  _reelY = 0;

    uint16_t _bodyColor = 0;
    uint16_t _bodyEdgeColor = 0;
    uint16_t _labelColor = 0;
    uint16_t _trackTextColor = 0;
    uint16_t _audioInfoColor = 0;
    uint16_t _windowColor = 0;
    uint16_t _tapeColor = 0;
    uint16_t _spoolColor = 0;

    uint32_t _lastAnimMs = 0;
    uint32_t _lastMetaMs = 0;
    uint32_t _lastAudioInfoMs = 0;
    uint16_t _phase = 0;
    char     _lastStation[128] = {};
    char     _lastTitle[192] = {};
    char     _lastAudioInfo[96] = {};

    void _layout();
    bool _loadBackground();
    void _deleteBackground();
    void _drawBody();
    void _drawLabels(const char* station, const char* title);
    void _drawTextLine(const char* text, int16_t y, uint16_t color, uint8_t preferredSize);
    void _drawAudioInfo(const char* info);
    void _drawReels();
    void _drawReel(int16_t centerX, float angle, int16_t tapeRadius);
    void _refreshMetadata(bool force = false);
    void _refreshAudioInfo(bool force = false);
};

#endif
