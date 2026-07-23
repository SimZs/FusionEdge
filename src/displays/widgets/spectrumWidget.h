#pragma once
#include "../../core/options.h"
#include "../../core/spectrumAnalyzer.h"  // SA_BANDS_MAX – guard előtt kell!
#include <LovyanGFX.hpp>
#if DSP_MODEL != DSP_DUMMY

#include "widgetsconfig.h"
#include "widget.h"

struct SpectrumConfig {
    uint16_t left;
    uint16_t top;
    uint16_t width;
    uint16_t height;
    uint8_t  bands;
    uint8_t  barGap;     // vízszintes rés az oszlopok között
    uint8_t  ledGap;     // függőleges rés a LED-szegmensek között
    uint8_t  barHeight;  // 1 LED-szegmens magassága px-ben (vuBarHeight store mező)
    uint8_t  midPct;     // zöld→sárga határvonal (% of totalLeds, 0..100)
    uint8_t  highPct;    // sárga→piros határvonal (% of totalLeds, 0..100)
    uint16_t peakColor;
    uint16_t bgColor;
    bool     showPeak;
};

class SpectrumWidget : public Widget {
  public:
    SpectrumWidget() {}
    SpectrumWidget(const SpectrumConfig& cfg) { init(cfg); }
    ~SpectrumWidget();

    using Widget::init;
    void init(const SpectrumConfig& cfg);
    void loop();
    void reset();
    void pauseFor(uint32_t ms);

  protected:
    void _draw()  override {}
    void _clear() override { reset(); }
    void _reset() override { reset(); }

  private:
    SpectrumConfig _cfg;
    LGFX_Sprite*   _canvas    = nullptr;
    LGFX_Sprite*   _waveCanvas = nullptr;
    uint16_t       _barW      = 0;
    uint8_t        _ledH      = 4;
    bool           _firstDraw = true;
    bool           _columnMode = false;
    uint16_t       _columnX = 0;
    uint16_t       _columnW = 0;
    uint32_t       _fadeMs    = 0;
    uint32_t       _pauseUntilMs = 0;
    uint32_t       _dualWaveLastFrameMs = 0;
#if DSP_MODEL == DSP_AXS15231B
    uint32_t       _axsBarsLastFrameMs = 0;
#endif
    float          _dualWaveLevel = 0.0f;
    int16_t        _dualWaveMainOffset[SA_BANDS_MAX] = {0};
    int16_t        _dualWaveShadowOffset[SA_BANDS_MAX] = {0};

    uint8_t  _prevSpec[64] = {0};
    uint8_t  _prevPeak[64] = {0};

    void _drawFrame(const uint8_t* spec, const uint8_t* peak);
    void _fillScreen(uint16_t color);
    void _fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void _pushFrame();
    bool _beginColumn(uint16_t x, uint16_t w);
    void _endColumn();
    bool _drawDualWave(const uint8_t* spec, const uint8_t* peak, uint8_t bands);
    bool _drawSoundWave(const uint8_t* spec, const uint8_t* peak, uint8_t bands);
};

#endif
