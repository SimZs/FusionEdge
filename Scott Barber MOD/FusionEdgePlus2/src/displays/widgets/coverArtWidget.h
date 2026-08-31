#pragma once

#include "../../core/options.h"

#if DSP_MODEL != DSP_DUMMY && defined(USE_COVERART_SCREENSAVER)

#include "widget.h"

// ── CoverArtWidget ───────────────────────────────────────────────────────────
// Full-screen album art screensaver (screensaverStyle == 3). Reuses the same
// last.fm/MusicBrainz cover fetched for the small station icon (CoverArtManager
// in coverart.h/.cpp) and shows it edge-to-edge, cropped to fill the panel,
// with a station/track info bar along the bottom. Falls back to a plain
// background + text if no cover art is available yet (e.g. right after the
// screensaver starts, before the async fetch completes).
class CoverArtWidget : public Widget {
  public:
    CoverArtWidget() = default;
    ~CoverArtWidget();

    using Widget::init;
    void init(WidgetConfig conf, uint16_t fgcolor, uint16_t bgcolor) override;
    void loop() override;
    void setColors(uint16_t fg, uint16_t bg) override;
    void setTextColors(uint16_t trackColor, uint16_t infoColor) {
        _trackTextColor = trackColor;
        _infoTextColor  = infoColor;
    }

  protected:
    void _draw() override;
    void _clear() override;
    void _reset() override;

  private:
    LGFX_Sprite* _canvas = nullptr;

    uint8_t* _coverBuf  = nullptr;
    size_t   _coverSize = 0;
    bool     _coverJpeg = false;
    uint32_t _coverGeneration = 0;
    uint16_t _coverImgW = 0;
    uint16_t _coverImgH = 0;
    bool     _haveCover = false;

    // Station-logo fallback (/images/stations/181fm.png) shown in place of
    // the placeholder disc when no cover art has been found yet.
    uint8_t* _fallbackBuf   = nullptr;
    size_t   _fallbackSize  = 0;
    uint16_t _fallbackImgW  = 0;
    uint16_t _fallbackImgH  = 0;
    bool     _fallbackLoaded = false;
    bool     _fallbackValid  = false;

    uint16_t _screenW = 0;
    uint16_t _screenH = 0;
    int16_t  _barY = 0;
    int16_t  _barH = 0;
    bool     _smallPanel = false;

    uint16_t _trackTextColor = 0;
    uint16_t _infoTextColor  = 0;
    uint16_t _barColor       = 0;

    char     _lastStation[128] = {};
    char     _lastTitle[192]   = {};
    uint32_t _lastMetaMs  = 0;
    uint32_t _lastPollMs  = 0;

    void _ensureCanvas();
    void _deleteCanvas();
    void _freeCover();
    void _freeFallbackCover();
    bool _ensureFallbackCover();
    bool _tryFetchCover();
    void _composite();
    void _drawInfoBar(const char* station, const char* title);
    void _drawTextLine(const char* text, int16_t y, uint16_t color, uint8_t preferredSize, int16_t maxWidth, int16_t centerX);
};

#endif
