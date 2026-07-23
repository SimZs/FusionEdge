#include "../../core/options.h"
#include "spectrumWidget.h"
#if DSP_MODEL != DSP_DUMMY
#if !defined(DSP_LCD) && !defined(DSP_OLED)

#include "../display_select.h"
#include "widgets.h"
#include "../../core/config.h"
#include "../../core/player.h"
#ifdef USE_BLUETOOTH
#    include "../../core/bluetooth.h"
#endif
#include "Arduino.h"
#include <math.h>
#include <string.h>

// =============================================================================
// SPECTRUM WIDGET – HANGOLHATÓ PARAMÉTEREK
// =============================================================================

// --- Mirror mód színzónák ---
// Lásd mirrorHeatColor() – itt csak a #define-ok vannak, a logika ott van

// --- Analyzer dinamika – spectrumAnalyzer.h-ban állítható ---
// DB_MIN, DB_RNG, ATTACK, RELEASE

// =============================================================================

SpectrumWidget::~SpectrumWidget() {
    if (_canvas) { delete _canvas; _canvas = nullptr; }
    if (_waveCanvas) { delete _waveCanvas; _waveCanvas = nullptr; }
}

void SpectrumWidget::init(const SpectrumConfig& cfg) {
    _cfg = cfg;

    WidgetConfig wconf = { cfg.left, cfg.top, 1, WA_LEFT };
    Widget::init(wconf, cfg.bgColor, cfg.bgColor);

    // Oszlopszélesség számítása a conf alapján
    uint8_t  bands    = cfg.bands > 0 ? cfg.bands : 16;
    uint16_t totalGap = (bands > 1) ? (uint16_t)((bands - 1) * cfg.barGap) : 0;
    _barW = (cfg.width - totalGap) / bands;
    if (_barW < 1) _barW = 1;

    // LED-szegmens magassága: közvetlenül a config-ból (vuBarHeight store mező)
    _ledH = (cfg.barHeight > 0) ? cfg.barHeight : 4;
    if (_ledH < 1) _ledH = 1;

    if (_canvas) { delete _canvas; _canvas = nullptr; }
    if (_waveCanvas) { delete _waveCanvas; _waveCanvas = nullptr; }
    _canvas = new Canvas(&dsp);
    _canvas->setPsram(false);
    if (!_canvas->createSprite(_barW, cfg.height)) {
        delete _canvas;
        _canvas = nullptr;
        log_e("##[SPECTRUM]# column sprite failed, fallback direct w=%u h=%u", _barW, cfg.height);
    }
    _firstDraw = true;
    memset(_prevSpec, 0, sizeof(_prevSpec));
    memset(_prevPeak, 0, sizeof(_prevPeak));
}

void SpectrumWidget::reset() {
    _fillScreen(_cfg.bgColor);
    _pushFrame();
    _firstDraw = true;
    _dualWaveLastFrameMs = 0;
#if DSP_MODEL == DSP_AXS15231B
    _axsBarsLastFrameMs = 0;
#endif
    _dualWaveLevel = 0.0f;
    memset(_dualWaveMainOffset, 0, sizeof(_dualWaveMainOffset));
    memset(_dualWaveShadowOffset, 0, sizeof(_dualWaveShadowOffset));
    memset(_prevSpec, 0, sizeof(_prevSpec));
    memset(_prevPeak, 0, sizeof(_prevPeak));
}

void SpectrumWidget::pauseFor(uint32_t ms) {
    _pauseUntilMs = millis() + ms;
}

void SpectrumWidget::_fillScreen(uint16_t color) {
    dsp.fillRect(_cfg.left, _cfg.top, _cfg.width, _cfg.height, color);
}

void SpectrumWidget::_fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (w == 0 || h == 0) return;
    if (_columnMode && _canvas && _canvas->getBuffer()) {
        if (x < _columnX || x >= _columnX + _columnW) return;
        uint16_t localX = x - _columnX;
        if (localX + w > _columnW) { w = _columnW - localX; }
        _canvas->fillRect(localX, y, w, h, color);
    } else {
        dsp.fillRect(_cfg.left + x, _cfg.top + y, w, h, color);
    }
}

void SpectrumWidget::_pushFrame() {
}

bool SpectrumWidget::_beginColumn(uint16_t x, uint16_t w) {
    if (!_canvas || !_canvas->getBuffer() || w > (uint16_t)_canvas->width()) {
        _columnMode = false;
        return false;
    }
    _columnX = x;
    _columnW = w;
    _columnMode = true;
    _canvas->fillScreen(_cfg.bgColor);
    return true;
}

void SpectrumWidget::_endColumn() {
    if (_columnMode && _canvas && _canvas->getBuffer()) {
        _canvas->pushSprite(&dsp, _cfg.left + _columnX, _cfg.top);
    }
    _columnMode = false;
}

static uint16_t rainbowByHeight(uint8_t led, uint8_t totalLeds) {
    // alul piros, fent lila (270°)
    uint16_t h = (totalLeds > 1) ? (uint32_t)270 * led / (totalLeds - 1) : 0;
    uint8_t region = h / 45;
    uint8_t rem    = (h % 45) * 255 / 45;
    uint8_t p = 0, q = 255 - rem, t = rem, v = 255;
    uint8_t r, g, b;
    switch (region) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default:r=v; g=p; b=q; break;
    }
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t fgWeight) {
    const uint16_t bgWeight = 255 - fgWeight;
    const uint16_t r = ((uint32_t)(fg >> 11) * fgWeight + (uint32_t)(bg >> 11) * bgWeight + 127) / 255;
    const uint16_t g = ((uint32_t)((fg >> 5) & 0x3f) * fgWeight +
                        (uint32_t)((bg >> 5) & 0x3f) * bgWeight + 127) / 255;
    const uint16_t b = ((uint32_t)(fg & 0x1f) * fgWeight + (uint32_t)(bg & 0x1f) * bgWeight + 127) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void drawWaveSegment(Canvas* canvas,
                            int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            uint16_t glow, uint16_t core,
                            uint8_t glowRadius, uint8_t coreRadius) {
    for (int8_t offset = -(int8_t)glowRadius; offset <= (int8_t)glowRadius; offset++) {
        canvas->drawLine(x0, y0 + offset, x1, y1 + offset, glow);
    }
    for (int8_t offset = -(int8_t)coreRadius; offset <= (int8_t)coreRadius; offset++) {
        canvas->drawLine(x0, y0 + offset, x1, y1 + offset, core);
    }
}

static void drawSmoothWave(Canvas* canvas, const int16_t* controlY, uint16_t count,
                           uint16_t width, uint16_t height,
                           uint16_t glow, uint16_t core,
                           uint8_t glowRadius, uint8_t coreRadius) {
    if (!canvas || !controlY || count < 2 || width < 2) return;

    for (uint16_t segment = 0; segment + 1 < count; segment++) {
        const int16_t p0 = controlY[segment > 0 ? segment - 1 : segment];
        const int16_t p1 = controlY[segment];
        const int16_t p2 = controlY[segment + 1];
        const int16_t p3 = controlY[segment + 2 < count ? segment + 2 : segment + 1];
        const int16_t x0 = (int16_t)((uint32_t)segment * (width - 1) / (count - 1));
        const int16_t x1 = (int16_t)((uint32_t)(segment + 1) * (width - 1) / (count - 1));
        const uint8_t steps = (uint8_t)max(2, (x1 - x0 + 2) / 3);
        int16_t prevX = x0;
        int16_t prevY = p1;

        for (uint8_t step = 1; step <= steps; step++) {
            const float t = (float)step / steps;
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float y = 0.5f * ((2.0f * p1) +
                (-p0 + p2) * t +
                (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
            const int16_t x = x0 + (int16_t)((int32_t)(x1 - x0) * step / steps);
            const int16_t yi = constrain((int16_t)y, 1, (int16_t)height - 2);
            drawWaveSegment(canvas, prevX, prevY, x, yi, glow, core, glowRadius, coreRadius);
            prevX = x;
            prevY = yi;
        }
    }
}

// Mirror mód színek – drawLeds alapján dinamikusan skálázva:
// Fehér: mindig a külső MIRROR_WHITE_LEDS LED (fix)
// Sötétkék: belső zóna MIRROR_YELLOW_PCT %-a
// Világoskék (cyan): a maradék legbelső LEDek
// Ha drawLeds <= MIRROR_MIN_FOR_COLOR: minden fehér
#define MIRROR_MIN_FOR_COLOR    4    // ennyi LED alatt minden fehér
#define MIRROR_WHITE_LEDS       3    // mindig ennyi külső LED fehér (fix)
#define MIRROR_YELLOW_PCT      15    // a belső zóna hány %-a sötétkék

static uint16_t mirrorHeatColor(uint8_t led, uint8_t drawLeds,
                                bool midColorOn,
                                uint16_t colMin, uint16_t colMid, uint16_t colMax) {
    if (!midColorOn) return colMin;                                // egyszínű mód: csak vumin
    if (drawLeds < MIRROR_MIN_FOR_COLOR) return colMin;
    if (led >= drawLeds - MIRROR_WHITE_LEDS) return colMin;       // külső: vumin (fehér)
    uint8_t innerLeds = drawLeds - MIRROR_WHITE_LEDS;
    uint8_t darkLeds  = (uint8_t)((uint32_t)innerLeds * MIRROR_YELLOW_PCT / 100);
    if (darkLeds < 1) darkLeds = 1;
    uint8_t cyanLeds  = innerLeds - darkLeds;
    if (led < cyanLeds) return colMax;                             // legbelső: vumax
    return colMid;                                                  // középső: vumid
}

bool SpectrumWidget::_drawDualWave(const uint8_t* spec, const uint8_t* peak, uint8_t bands) {
    (void)peak;
    if (!spec || bands < 2) return true;

    const uint32_t now = millis();
    constexpr uint32_t frameMs = 40;
    if (_dualWaveLastFrameMs != 0 && now - _dualWaveLastFrameMs < frameMs) return true;
    _dualWaveLastFrameMs = now;

    if (!_waveCanvas) {
        _waveCanvas = new Canvas(&dsp);
        _waveCanvas->setColorDepth(16);
        _waveCanvas->setPsram(true);
        if (!_waveCanvas->createSprite(_cfg.width, _cfg.height)) {
            delete _waveCanvas;
            _waveCanvas = nullptr;
            log_e("##[SPECTRUM]# dualwave sprite failed w=%u h=%u", _cfg.width, _cfg.height);
            return true;
        }
    }

    constexpr uint16_t wavePoints = SA_BANDS_MAX;
    float rawWave[wavePoints];
    bool hasSignal = false;
    for (uint8_t i = 0; i < bands; i++) {
        if (spec[i] != 0) {
            hasSignal = true;
            break;
        }
    }
    const float rms = hasSignal ? spectrumAnalyzer.getWaveform(rawWave, wavePoints) : 0.0f;
    if (!hasSignal) memset(rawWave, 0, sizeof(rawWave));

    const float targetLevel = min(1.0f, rms * 3.0f);
    const float levelFollow = targetLevel > _dualWaveLevel ? 0.55f : 0.18f;
    _dualWaveLevel += (targetLevel - _dualWaveLevel) * levelFollow;

    const uint16_t W = _cfg.width;
    const uint16_t H = _cfg.height;
    const int16_t centerY = H / 2;
    const int16_t mainMaxAmp = max(1, centerY - 5);
    const int16_t shadowMaxAmp = max(1, (int16_t)H / 3);
    const uint16_t mainColor = config.theme.vumin;
    const uint16_t shadowColor = config.store.vuMidOn ? config.theme.vumax : mainColor;
    const uint16_t dotColor = config.store.vuMidOn ? config.theme.vumid : mainColor;
    const uint8_t mainGlowWeight = (uint8_t)constrain(
        (int)(48.0f + _dualWaveLevel * 82.0f), 48, 130);
    const uint8_t shadowGlowWeight = (uint8_t)constrain(
        (int)(38.0f + _dualWaveLevel * 62.0f), 38, 100);
    const uint16_t mainGlow = blend565(mainColor, _cfg.bgColor, mainGlowWeight);
    const uint16_t shadowGlow = blend565(shadowColor, _cfg.bgColor, shadowGlowWeight);
    const uint16_t gridColor = blend565(config.theme.vumax, _cfg.bgColor, 155);
    const uint16_t gridAxisColor = blend565(config.theme.vumin, _cfg.bgColor, 200);
    const uint16_t centerGlow = dsp.color565(35, 35, 40);

    constexpr uint16_t controlCount = wavePoints + 2;
    int16_t mainY[controlCount];
    int16_t shadowY[controlCount];
    float centeredWave[wavePoints];
    float mainStrength[wavePoints];
    mainY[0] = centerY;
    shadowY[0] = centerY;

    constexpr int16_t baselineRadius = 8;
    for (uint16_t i = 0; i < wavePoints; i++) {
        const int16_t position = (int16_t)i;
        const int16_t first = position > baselineRadius ? position - baselineRadius : 0;
        const int16_t last = position + baselineRadius < (int16_t)wavePoints
            ? position + baselineRadius
            : (int16_t)wavePoints - 1;
        float localMean = 0.0f;
        for (int16_t sample = first; sample <= last; sample++) {
            localMean += rawWave[sample];
        }
        localMean /= (last - first + 1);
        centeredWave[i] = rawWave[i] - localMean;
    }

    for (uint16_t i = 0; i < wavePoints; i++) {
        const float left = centeredWave[i > 0 ? i - 1 : 0];
        const float center = centeredWave[i];
        const float right = centeredWave[i + 1 < wavePoints ? i + 1 : wavePoints - 1];
        const float smooth = (left + center * 2.0f + right) * 0.25f;
        const float mainSample = tanhf(smooth * 4.30f) * 0.98f;
        mainStrength[i] = fabsf(mainSample);
        const int16_t mainTarget = (int16_t)(mainSample * mainMaxAmp);
        const uint8_t mainFollow = abs((int)mainTarget) > abs((int)_dualWaveMainOffset[i]) ? 3 : 2;
        _dualWaveMainOffset[i] +=
            (int16_t)(((int32_t)mainTarget - _dualWaveMainOffset[i]) * mainFollow / 8);
        mainY[i + 1] = centerY - _dualWaveMainOffset[i];
    }
    mainY[controlCount - 1] = centerY;

    for (uint16_t i = 0; i < wavePoints; i++) {
        const uint16_t shifted = min((uint16_t)(i + 2), (uint16_t)(wavePoints - 1));
        const float shadowSample = constrain(rawWave[shifted] * 0.72f, -1.0f, 1.0f);
        const int16_t shadowTarget = (int16_t)(shadowSample * shadowMaxAmp);
        _dualWaveShadowOffset[i] +=
            (int16_t)(((int32_t)shadowTarget - _dualWaveShadowOffset[i]) / 2);
        shadowY[i + 1] = centerY - _dualWaveShadowOffset[i];
    }
    shadowY[controlCount - 1] = centerY;

    _waveCanvas->fillSprite(_cfg.bgColor);
    constexpr uint8_t gridColumns = 10;
    constexpr uint8_t gridRows = 4;
    for (uint8_t column = 1; column < gridColumns; column++) {
        const int16_t x = (int16_t)((uint32_t)column * (W - 1) / gridColumns);
        const bool centerAxis = column == gridColumns / 2;
        const uint8_t step = centerAxis ? 3 : 4;
        const uint16_t color = centerAxis ? gridAxisColor : gridColor;
        for (uint16_t y = centerAxis ? 0 : 1; y < H; y += step) {
            _waveCanvas->drawPixel(x, y, color);
        }
    }
    for (uint8_t row = 1; row < gridRows; row++) {
        if (row == gridRows / 2) continue;
        const int16_t y = (int16_t)((uint32_t)row * (H - 1) / gridRows);
        for (uint16_t x = 1; x < W; x += 4) {
            _waveCanvas->drawPixel(x, y, gridColor);
        }
    }
    for (uint16_t x = 0; x < W; x += 3) {
        _waveCanvas->drawPixel(x, centerY, centerGlow);
    }
    const uint16_t pulseW = (uint16_t)(_dualWaveLevel * W);
    if (pulseW > 4) {
        _waveCanvas->drawFastHLine((W - pulseW) / 2, centerY, pulseW, mainGlow);
    }

    drawSmoothWave(_waveCanvas, shadowY, controlCount, W, H,
                   shadowGlow, shadowColor, 2, 0);
    drawSmoothWave(_waveCanvas, mainY, controlCount, W, H,
                   mainGlow, mainColor, 3, 1);

    for (uint16_t i = 0; i < wavePoints; i++) {
        if (mainStrength[i] < 0.72f || (i & 3)) continue;
        const int16_t x = (int16_t)((uint32_t)(i + 1) * (W - 1) / (controlCount - 1));
        _waveCanvas->fillCircle(x, mainY[i + 1], 2, dotColor);
    }

    _waveCanvas->pushSprite(&dsp, _cfg.left, _cfg.top);
    for (uint8_t i = 0; i < bands; i++) {
        _prevSpec[i] = spec[i];
        _prevPeak[i] = 0;
    }
    _pushFrame();
    return true;
}

bool SpectrumWidget::_drawSoundWave(const uint8_t* spec, const uint8_t* peak, uint8_t bands) {
    (void)peak;
    if (!spec || bands == 0) { return true; }

    bool anyDirty = _firstDraw;
    for (uint8_t i = 0; i < bands; i++) {
        if (spec[i] != _prevSpec[i]) {
            anyDirty = true;
            break;
        }
    }
    if (!anyDirty) { return true; }

    if (!_waveCanvas) {
        _waveCanvas = new Canvas(&dsp);
        _waveCanvas->setColorDepth(16);
        _waveCanvas->setPsram(true);
        if (!_waveCanvas->createSprite(_cfg.width, _cfg.height)) {
            delete _waveCanvas;
            _waveCanvas = nullptr;
            log_e("##[SPECTRUM]# soundwave sprite failed w=%u h=%u", _cfg.width, _cfg.height);
            return true;
        }
    }

    const uint16_t W = _cfg.width;
    const uint16_t H = _cfg.height;
    const int16_t centerY = H / 2;
    const uint8_t points = bands * 2;
    const uint16_t fg = config.theme.vumin;
    const uint16_t dotColor = config.store.vuMidOn ? config.theme.vumid : fg;
    const uint16_t shadowColor = config.store.vuMidOn ? config.theme.vumax : fg;
    const uint16_t centerGlow = dsp.color565(35, 35, 40);

    _waveCanvas->fillSprite(_cfg.bgColor);
    _waveCanvas->drawFastHLine(0, centerY - 1, W, centerGlow);
    _waveCanvas->drawFastHLine(0, centerY + 1, W, centerGlow);

    int16_t prevX = 0;
    int16_t prevAmp = 0;
    uint16_t energy = 0;
    for (uint8_t p = 0; p < points; p++) {
        const uint8_t si = (p < bands) ? p : (points - 1 - p);
        uint16_t mixed = spec[si];
        if (si + 1 < bands) { mixed = (mixed + spec[si + 1]) >> 1; }
        const int16_t amp = (int16_t)((uint32_t)mixed * (H / 2 - 3) / 255);
        const int16_t x = (points > 1) ? (int16_t)((uint32_t)p * (W - 1) / (points - 1)) : 0;
        energy += mixed;

        if (p > 0) {
            const int16_t prevGlow = prevAmp / 2;
            const int16_t glow = amp / 2;
            _waveCanvas->drawLine(prevX, centerY - prevGlow, x, centerY - glow, shadowColor);
            _waveCanvas->drawLine(prevX, centerY + prevGlow, x, centerY + glow, shadowColor);
            for (int8_t t = -1; t <= 1; t++) {
                _waveCanvas->drawLine(prevX, centerY - prevAmp + t, x, centerY - amp + t, fg);
                _waveCanvas->drawLine(prevX, centerY + prevAmp + t, x, centerY + amp + t, fg);
            }
            if (mixed > 190 && (p & 1) == 0) {
                _waveCanvas->fillCircle(x, centerY - amp, 2, dotColor);
                _waveCanvas->fillCircle(x, centerY + amp, 2, dotColor);
            }
        }
        prevX = x;
        prevAmp = amp;
    }

    const uint16_t pulseW = (uint16_t)((uint32_t)(energy / points) * W / 255);
    const uint16_t pulseX = (pulseW < W) ? (W - pulseW) / 2 : 0;
    for (uint16_t x = 0; x < W; x += 3) {
        _waveCanvas->drawPixel(x, centerY, centerGlow);
    }
    if (pulseW > 4) { _waveCanvas->drawFastHLine(pulseX, centerY, pulseW, fg); }
    _waveCanvas->pushSprite(&dsp, _cfg.left, _cfg.top);

    for (uint8_t i = 0; i < bands; i++) {
        _prevSpec[i] = spec[i];
        _prevPeak[i] = 0;
    }
    _pushFrame();
    return true;
}

void SpectrumWidget::_drawFrame(const uint8_t* spec, const uint8_t* peak) {
    if (!config.store.vumeter) {
        _firstDraw = true;
        memset(_prevSpec, 0, sizeof(_prevSpec));
        memset(_prevPeak, 0, sizeof(_prevPeak));
        return;
    }

    const uint16_t H        = _cfg.height;
    const uint16_t W        = _cfg.width;
    const uint8_t  G        = _cfg.barGap;
    const uint8_t  LED_GAP  = _cfg.ledGap;
    const uint8_t  LED_H    = _ledH;
    const uint8_t  LED_CELL = LED_H + LED_GAP;
    const uint16_t bg       = _cfg.bgColor;
    const uint8_t  bands    = _cfg.bands;
    const uint8_t  mode     = config.store.vuSpecMode; // 0=Std,1=DualWave,2=RainbowFill,3=SoundWave

    const uint8_t totalLeds      = (uint8_t)(H / LED_CELL);
    const uint8_t midPct         = config.store.vuMidPctDef;
    const uint8_t highPct        = config.store.vuHighPctDef;
    const uint8_t ledYellowStart = (uint8_t)((uint32_t)totalLeds * midPct  / 100);
    const uint8_t ledRedStart    = (uint8_t)((uint32_t)totalLeds * highPct / 100);

    const uint16_t colGreen  = config.theme.vumin;
    const uint16_t colYellow = config.theme.vumid;
    const uint16_t colRed    = config.theme.vumax;
    const bool     midColorOn = (config.store.vuMidOn != 0);
    const bool     showPeak   = (config.store.vuPeakOn != 0);

    const uint8_t  PEAK_H   = LED_H / 2;   // peak csík vastagsága: LED_H fele

    if (_firstDraw) {
        _fillScreen(bg);
        _firstDraw = false;
    }

    // Both wave modes redraw one complete sprite to avoid partial-clear flicker.
    if (mode == 1) {
        if (_drawDualWave(spec, peak, bands)) { return; }
    }

    if (mode == 3) {
        if (_drawSoundWave(spec, peak, bands)) { return; }

        const uint8_t  mirrorBands    = bands * 2;
        const uint16_t totalMirrorGap = (mirrorBands > 1) ? (uint16_t)((mirrorBands - 1) * G) : 0;
        const uint16_t mirrorBarW     = (W > totalMirrorGap) ? (W - totalMirrorGap) / mirrorBands : 1;
        // Mirror módban nincs LED gap → folyamatos vonalak
        const uint8_t  M_CELL         = LED_H;   // gap = 0
        const uint8_t  halfLeds       = (uint8_t)(H / 2 / M_CELL);
        bool anyDirty = false;

        for (uint8_t mi = 0; mi < mirrorBands; mi++) {
            // Tükrözés: bal fél → spec[0..bands-1], jobb fél → spec[bands-1..0]
            uint8_t si = (mi < bands) ? mi : (mirrorBands - 1 - mi);

            // Csak ha ez a sáv (vagy tükörpárja) változott
            // Csak ha ez a sav (vagy tukorparja) valtozott
            if (spec[si] == _prevSpec[si] && (!peak || peak[si] == _prevPeak[si])) continue;
            anyDirty = true;

            // Szomszédos átlag interpoláció (integer)
            uint8_t s_mi, p_mi;
            uint8_t old_s_mi, old_p_mi;
            if (mi < bands && si + 1 < bands) {
                s_mi = (uint8_t)(((uint16_t)spec[si] + spec[si + 1]) >> 1);
                p_mi = peak ? (uint8_t)(((uint16_t)peak[si] + peak[si + 1]) >> 1) : 0;
                old_s_mi = (uint8_t)(((uint16_t)_prevSpec[si] + _prevSpec[si + 1]) >> 1);
                old_p_mi = peak ? (uint8_t)(((uint16_t)_prevPeak[si] + _prevPeak[si + 1]) >> 1) : 0;
            } else {
                s_mi = spec[si];
                p_mi = peak ? peak[si] : 0;
                old_s_mi = _prevSpec[si];
                old_p_mi = peak ? _prevPeak[si] : 0;
            }

            const uint16_t mx         = (uint16_t)(mi * (mirrorBarW + G));
            const uint8_t  litLeds_mi = (uint8_t)((uint32_t)s_mi * halfLeds * M_CELL / 255 / M_CELL);
            const uint8_t  drawLeds   = litLeds_mi;
            const uint8_t  peakLed_mi = p_mi ? (uint8_t)((uint32_t)p_mi * halfLeds * M_CELL / 255 / M_CELL) : 0;
            const uint8_t  oldDrawLeds = (uint8_t)((uint32_t)old_s_mi * halfLeds * M_CELL / 255 / M_CELL);
            const uint8_t  oldPeakLed  = old_p_mi ? (uint8_t)((uint32_t)old_p_mi * halfLeds * M_CELL / 255 / M_CELL) : 0;
            uint8_t clearLeds = max(drawLeds, oldDrawLeds);
            if (showPeak) { clearLeds = max(clearLeds, (uint8_t)max(peakLed_mi, oldPeakLed)); }

            bool columnBuffered = _beginColumn(mx, mirrorBarW);
            if (clearLeds > 0) {
                uint16_t clearH = (uint16_t)clearLeds * M_CELL + PEAK_H + 2;
                if (clearH > H / 2) { clearH = H / 2; }
                _fillRect(mx, H / 2, mirrorBarW, clearH, bg);
                _fillRect(mx, H / 2 - clearH, mirrorBarW, clearH, bg);
            }

            for (uint8_t led = 0; led < drawLeds; led++) {
                uint16_t col  = mirrorHeatColor(led, drawLeds, midColorOn, colGreen, colYellow, colRed);
                uint16_t botY = H / 2 + (uint16_t)led * M_CELL;
                if (botY + M_CELL <= H)
                    _fillRect(mx, botY, mirrorBarW, M_CELL, col);
                uint16_t topY = H / 2 - (uint16_t)(led + 1) * M_CELL;
                if ((int16_t)topY >= 0)
                    _fillRect(mx, topY, mirrorBarW, M_CELL, col);
            }
            if (showPeak && peakLed_mi > 0) {
                uint16_t pbTop = H / 2 + (uint16_t)peakLed_mi * M_CELL;
                if (pbTop + PEAK_H <= H)
                    _fillRect(mx, pbTop, mirrorBarW, PEAK_H, 0xFFFF);
                int16_t ptTop = (int16_t)(H / 2) - (int16_t)(peakLed_mi + 1) * M_CELL - PEAK_H - 1;
                if (ptTop >= 0)
                    _fillRect(mx, (uint16_t)ptTop, mirrorBarW, PEAK_H, 0xFFFF);
            }
            if (columnBuffered) { _endColumn(); }
        }
        if (anyDirty) {
            for (uint8_t i = 0; i < bands; i++) {
                _prevSpec[i] = spec[i];
                _prevPeak[i] = peak ? peak[i] : 0;
            }
            _pushFrame();
        }
        return;
    }

#if DSP_MODEL == DSP_AXS15231B
    // The QSPI panel is much faster with one contiguous transfer than with a
    // separate sprite transfer for every bar. The wave modes already use this
    // path; use the same full-frame strategy for the classic bar modes.
    if (mode == 0 || mode == 2) {
        const uint32_t now = millis();
        constexpr uint32_t frameMs = 40;
        if (_axsBarsLastFrameMs != 0 && now - _axsBarsLastFrameMs < frameMs) { return; }
        _axsBarsLastFrameMs = now;

        if (!_waveCanvas) {
            _waveCanvas = new Canvas(&dsp);
            _waveCanvas->setColorDepth(16);
            _waveCanvas->setPsram(true);
            if (!_waveCanvas->createSprite(W, H)) {
                delete _waveCanvas;
                _waveCanvas = nullptr;
                log_e("##[SPECTRUM]# AXS bar sprite failed w=%u h=%u", W, H);
            }
        }

        if (_waveCanvas && _waveCanvas->getBuffer()) {
            _waveCanvas->fillSprite(bg);
            for (uint8_t i = 0; i < bands; i++) {
                const uint8_t  s       = spec[i];
                const uint8_t  p       = peak ? peak[i] : 0;
                const uint16_t x       = (uint16_t)(i * (_barW + G));
                const uint8_t  litLeds = (uint8_t)((uint32_t)s * totalLeds / 255);
                const uint8_t  peakLed = p > 0 ? (uint8_t)((uint32_t)p * totalLeds / 255) : 0;

                for (uint8_t led = 0; led < litLeds; led++) {
                    const uint16_t ledY = H - (led + 1) * LED_CELL + LED_GAP;
                    uint16_t col;
                    if (mode == 2) {
                        col = rainbowByHeight(led, totalLeds);
                    } else if (!midColorOn) {
                        col = colGreen;
                    } else if (led >= ledRedStart) {
                        col = colRed;
                    } else if (led >= ledYellowStart) {
                        col = colYellow;
                    } else {
                        col = colGreen;
                    }
                    _waveCanvas->fillRect(x, ledY, _barW, LED_H, col);
                }

                if (showPeak && peakLed > 0 && peakLed <= totalLeds) {
                    const uint16_t ledTopY = H - (uint16_t)peakLed * LED_CELL + LED_GAP;
                    const int16_t  peakY   = (int16_t)ledTopY - PEAK_H - 1;
                    if (peakY >= 0) { _waveCanvas->fillRect(x, (uint16_t)peakY, _barW, PEAK_H, 0xFFFF); }
                }

                _prevSpec[i] = s;
                _prevPeak[i] = p;
            }
            _waveCanvas->pushSprite(&dsp, _cfg.left, _cfg.top);
            _pushFrame();
            return;
        }
    }
#endif

    for (uint8_t i = 0; i < bands; i++) {
        uint8_t s = spec[i];
        uint8_t p = peak ? peak[i] : 0;

        if (s == _prevSpec[i] && p == _prevPeak[i]) continue;

        const uint16_t x       = (uint16_t)(i * (_barW + G));
        const uint8_t  litLeds = (uint8_t)((uint32_t)s * totalLeds / 255);
        const uint8_t  peakLed = (p > 0) ? (uint8_t)((uint32_t)p * totalLeds / 255) : 0;

        bool columnBuffered = _beginColumn(x, _barW);
        _fillRect(x, 0, _barW, H, bg);

        // ----------------------------------------------------------------
        // MODE 0 – Standard
        // ----------------------------------------------------------------
        if (mode == 0) {
            for (uint8_t led = 0; led < litLeds; led++) {
                uint16_t ledY = H - (led + 1) * LED_CELL + LED_GAP;
                uint16_t col;
                if (!midColorOn)               col = colGreen;
                else if (led >= ledRedStart)   col = colRed;
                else if (led >= ledYellowStart)col = colYellow;
                else                           col = colGreen;
                _fillRect(x, ledY, _barW, LED_H, col);
            }
            if (showPeak && peakLed > 0 && peakLed <= totalLeds) {
                // Peak: közvetlenül a legfelső lit LED fölé, LED_H/2 magas
                uint16_t ledTopY = H - (uint16_t)peakLed * LED_CELL + LED_GAP;
                int16_t  peakY   = (int16_t)ledTopY - PEAK_H - 1;
                if (peakY >= 0)
                    _fillRect(x, (uint16_t)peakY, _barW, PEAK_H, 0xFFFF);
            }
        }

        // ----------------------------------------------------------------
        // MODE 2 - Rainbow Fill
        // ----------------------------------------------------------------
        else if (mode == 2) {  // RainbowFill
            for (uint8_t led = 0; led < litLeds; led++) {
                uint16_t ledY = H - (led + 1) * LED_CELL + LED_GAP;
                uint16_t col  = rainbowByHeight(led, totalLeds);
                _fillRect(x, ledY, _barW, LED_H, col);
            }
            if (showPeak && peakLed > 0 && peakLed <= totalLeds) {
                uint16_t ledTopY = H - (uint16_t)peakLed * LED_CELL + LED_GAP;
                int16_t  peakY   = (int16_t)ledTopY - PEAK_H - 1;
                if (peakY >= 0)
                    _fillRect(x, (uint16_t)peakY, _barW, PEAK_H, 0xFFFF);
            }
        }

        _prevSpec[i] = s;
        _prevPeak[i] = p;
        if (columnBuffered) { _endColumn(); }
    }

    _pushFrame();
}

void SpectrumWidget::loop() {
    if (!_active || _locked) return;
    if (_pauseUntilMs != 0 && (int32_t)(millis() - _pauseUntilMs) < 0) return;
    _pauseUntilMs = 0;

    // FusionEdge: BT módban a normál Audio decode task áll (player.isRunning()
    // == false), az audio a bluetooth.cpp I2S hídján és a spectrumAnalyzer
    // pushSamples() hívásán keresztül jut be — ezért a widgetnek a
    // player.isRunning() mellett a BT hidat is figyelnie kell, különben BT
    // lejátszás alatt mindig a "néma" (fade/reset) ágra futna.
#ifdef USE_BLUETOOTH
    const bool btBridgeActive = (config.getMode() == PM_BLUETOOTH) && bluetooth.bridgeRunning() && bluetooth.playing();
    if (config.getMode() != PM_BLUETOOTH) {
        spectrumAnalyzer.setSampleRate(player.getSampleRate());
    }
#else
    constexpr bool btBridgeActive = false;
    spectrumAnalyzer.setSampleRate(player.getSampleRate());
#endif

    if (!player.isRunning() && !btBridgeActive) {
        uint32_t now = millis();
        if (now - _fadeMs < 100) return;
        _fadeMs = now;
        spectrumAnalyzer.resetSmooth();
        uint8_t spec[64] = {0};
        uint8_t peak[64] = {0};
        _drawFrame(spec, config.store.vuPeakOn ? peak : nullptr);
        return;
    }

    _fadeMs = millis();
    const bool spectrumUpdated = spectrumAnalyzer.process();
    if (!spectrumUpdated && config.store.vuSpecMode != 1) return;
    uint8_t spec[64] = {0};
    uint8_t peak[64] = {0};
    bool wantPeak = (config.store.vuPeakOn != 0);
    spectrumAnalyzer.getData(spec, wantPeak ? peak : nullptr);
    _drawFrame(spec, wantPeak ? peak : nullptr);
}

#endif
#endif
