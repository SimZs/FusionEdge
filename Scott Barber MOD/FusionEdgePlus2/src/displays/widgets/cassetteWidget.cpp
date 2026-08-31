#include "cassetteWidget.h"

#if DSP_MODEL != DSP_DUMMY && defined(USE_CASSETTE_SCREENSAVER)

#include "../../core/config.h"
#include "../../core/fonts.h"
#include "../../core/player.h"
#ifdef USE_BLUETOOTH
#include "../../core/bluetooth.h"
#endif
#include "../display_select.h"

#include <algorithm>
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include <string.h>

#ifndef CASSETTE_FRAME_MS
#define CASSETTE_FRAME_MS 100UL
#endif

#ifndef CASSETTE_PNG_PATH
#define CASSETTE_PNG_PATH "/images/screensaver/retro_audio_cassette.png"
#endif

namespace {
constexpr int16_t kBackgroundWidth = 456;
constexpr int16_t kBackgroundHeight = 291;
constexpr int16_t kPngLeftReelX = 131;
constexpr int16_t kPngRightReelX = 327;
constexpr int16_t kPngReelY = 129;
constexpr int16_t kReelSpriteSize = 82;
constexpr int16_t kReelCenter = kReelSpriteSize / 2;

// Every tape here must be a 456x291 RGBA PNG. reelLeftX/reelRightX/reelY give
// the pixel centers of that tape's *printed* reel holes at native (456x291)
// scale, so the animated reel overlay lands exactly on top of the artwork
// instead of assuming every tape uses the same template layout.
struct TapeArt {
    const char* path;
    int16_t     reelLeftX;
    int16_t     reelRightX;
    int16_t     reelY;
};

constexpr TapeArt kTapes[] = {
    {"/images/screensaver/retro_audio_cassette.png",      kPngLeftReelX, kPngRightReelX, kPngReelY},
    {"/images/screensaver/retro_audio_cassette_blue.png", kPngLeftReelX, kPngRightReelX, kPngReelY},
    {"/images/screensaver/retro_audio_cassette_red.png",  kPngLeftReelX, kPngRightReelX, kPngReelY},
};
constexpr uint8_t kTapeCount = sizeof(kTapes) / sizeof(kTapes[0]);

// Reads width/height straight out of a PNG's IHDR chunk (bytes 16-19 and
// 20-23, big-endian) so _loadBackground can scale to the tape's *actual*
// pixel size instead of blindly trusting kBackgroundWidth/kBackgroundHeight.
// Returns false if the buffer doesn't look like a PNG.
bool readPngDimensions(const uint8_t* buf, size_t len, int32_t& outW, int32_t& outH) {
    static const uint8_t kPngSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (len < 24 || memcmp(buf, kPngSig, 8) != 0 || memcmp(buf + 12, "IHDR", 4) != 0) {
        return false;
    }
    outW = (int32_t)((buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19]);
    outH = (int32_t)((buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23]);
    return outW > 0 && outH > 0;
}

uint32_t hashStationName(const char* s) {
    uint32_t h = 2166136261u; // FNV-1a
    while (s && *s) {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619u;
    }
    return h;
}

const TapeArt& tapeForStation(const char* station) {
    if (!station || !station[0]) { return kTapes[0]; }
    return kTapes[hashStationName(station) % kTapeCount];
}

uint8_t* fontForSize(uint8_t size) {
    return vlwBySize(size);
}

const char* formatName(BitrateFormat format) {
    switch (format) {
        case BF_MP3:  return "MP3";
        case BF_AAC:  return "AAC";
        case BF_FLAC: return "FLAC";
        case BF_OGG:  return "OGG";
        case BF_WAV:  return "WAV";
        case BF_VOR:  return "VORBIS";
        case BF_OPU:  return "OPUS";
        default:      return "";
    }
}

void appendInfoToken(char* destination, size_t destinationSize, const char* token) {
    if (!token || !token[0]) { return; }
    // Plain ASCII on purpose: the embedded VLW fonts here are ASCII-only, and
    // a UTF-8 multi-byte separator gets decoded as two bogus single-byte
    // glyphs without UTF-8 mode enabled on the display. That corrupted
    // dsp.textWidth() measurements for every multi-token string, making
    // _fitAudioInfo() think things didn't fit and drop tokens that would
    // have fit fine -- on the native-size 3.5" box just as much as 2.8".
    if (destination[0]) { strlcat(destination, " | ", destinationSize); }
    strlcat(destination, token, destinationSize);
}
}

CassetteWidget::~CassetteWidget() {
    _deleteBackground();
    delete _reelSprite;
    _reelSprite = nullptr;
}

void CassetteWidget::init(WidgetConfig conf, uint16_t fgcolor, uint16_t bgcolor) {
    Widget::init(conf, fgcolor, bgcolor);
    _layout();

    strlcpy(_currentTapePath, CASSETTE_PNG_PATH, sizeof(_currentTapePath));

    _reelSprite = new LGFX_Sprite(&dsp);
    if (_reelSprite) {
        _reelSprite->setColorDepth(16);
        _reelSprite->setPsram(false);
        if (!_reelSprite->createSprite(kReelSpriteSize, kReelSpriteSize)) {
            delete _reelSprite;
            _reelSprite = nullptr;
            log_w("##[CASSETTE]# reel sprite allocation failed, using direct draw");
        }
    }

    _loadBackground();
    _layout();
}

void CassetteWidget::setColors(uint16_t fg, uint16_t bg) {
    if (_fgcolor == fg && _bgcolor == bg) { return; }
    Widget::setColors(fg, bg);
    _loadBackground();
    _layout();
}

void CassetteWidget::_deleteBackground() {
    if (_backgroundSprite) {
        _backgroundSprite->deleteSprite();
        delete _backgroundSprite;
        _backgroundSprite = nullptr;
    }
    _pngReady = false;
}

bool CassetteWidget::_loadBackground() {
    _deleteBackground();
    _pngScale = 1.0f;

    if (dsp.width() < 160 || dsp.height() < 100) {
        log_w("##[CASSETTE]# display too small for cassette art, using code-drawn fallback");
        return false;
    }

    // Scale the artwork down to fit screens smaller than the native 456x291
    // canvas (e.g. ILI9341 @ 320x240) instead of refusing to show it at all.
    // A small margin keeps the cassette from touching the screen edges.
    float scale = 1.0f;
    if (dsp.width() < kBackgroundWidth || dsp.height() < kBackgroundHeight) {
        const float scaleX = (float)dsp.width() / (float)kBackgroundWidth;
        const float scaleY = (float)dsp.height() / (float)kBackgroundHeight;
        scale = std::min(scaleX, scaleY) * 0.94f;
    }

    File file = LittleFS.open(_currentTapePath, "r");
    if (!file) {
        log_w("##[CASSETTE]# PNG not found: %s, using code-drawn fallback", _currentTapePath);
        return false;
    }

    const size_t pngSize = file.size();
    if (pngSize == 0) {
        file.close();
        return false;
    }

    uint8_t* pngBuffer = static_cast<uint8_t*>(ps_malloc(pngSize));
    if (!pngBuffer) {
        file.close();
        log_w("##[CASSETTE]# PNG buffer allocation failed (%u bytes)", (unsigned)pngSize);
        return false;
    }

    const size_t readSize = file.read(pngBuffer, pngSize);
    file.close();
    if (readSize != pngSize) {
        free(pngBuffer);
        return false;
    }

    const int16_t targetW = std::max<int16_t>(160, (int16_t)(kBackgroundWidth * scale));
    // Re-derive the scale from the rounded width so drawPng's scaleX/scaleY
    // exactly matches the sprite we actually allocate, then use that same
    // scale for height so the aspect ratio stays exact.
    scale = (float)targetW / (float)kBackgroundWidth;
    const int16_t targetH = std::max<int16_t>(100, (int16_t)(kBackgroundHeight * scale + 0.5f));

    _backgroundSprite = new LGFX_Sprite(&dsp);
    if (!_backgroundSprite) {
        free(pngBuffer);
        return false;
    }
    _backgroundSprite->setColorDepth(16);
    _backgroundSprite->setPsram(true);
    if (!_backgroundSprite->createSprite(targetW, targetH)) {
        free(pngBuffer);
        _deleteBackground();
        log_w("##[CASSETTE]# background sprite allocation failed, using code-drawn fallback");
        return false;
    }

    int32_t realW = kBackgroundWidth;
    int32_t realH = kBackgroundHeight;
    if (!readPngDimensions(pngBuffer, pngSize, realW, realH)) {
        realW = kBackgroundWidth;
        realH = kBackgroundHeight;
    } else if (realW != kBackgroundWidth || realH != kBackgroundHeight) {
        log_w("##[CASSETTE]# %s is %dx%d, expected %dx%d -- scaling to fit so it fills the canvas",
              _currentTapePath, (int)realW, (int)realH, kBackgroundWidth, kBackgroundHeight);
    }

    // Scale explicitly from the PNG's *actual* pixel size to the target
    // canvas, rather than assuming every tape is exactly 456x291. Previously
    // the native-size path drew the PNG at its own dimensions with no
    // scale/clip, so any tape that wasn't exactly 456x291 left the sprite's
    // black fillSprite() background showing through as bars around it.
    _backgroundSprite->fillSprite(_bgcolor);
    // Single uniform scale for both axes -- matches the pattern proven
    // working in master's StationIconWidget::setCover() (scale = min(scaleX,
    // scaleY), one value used for both). Previously this used pngScaleX and
    // pngScaleY computed as two independently-rounded values (targetW and
    // targetH are each rounded separately), which meant a tiny non-uniform
    // stretch on every draw even for tapes that are exactly 456x291 -- the
    // decoder was asked to step X and Y at slightly different fractional
    // rates, which is the likely source of the black spots in the
    // detail-dense label area on larger displays. realW/realH always equal
    // kBackgroundWidth/kBackgroundHeight for the bundled tapes, so `scale`
    // (already the exact targetW/kBackgroundWidth ratio) is correct here.
    _backgroundSprite->drawPng(pngBuffer, pngSize, 0, 0, targetW, targetH, 0, 0, scale, scale);
    free(pngBuffer);
    _pngReady = true;
    _pngScale = scale;
    return true;
}

void CassetteWidget::_selectTapeForStation(const char* station) {
    const TapeArt& tape = tapeForStation(station);
    if (strcmp(tape.path, _currentTapePath) == 0) { return; }

    strlcpy(_currentTapePath, tape.path, sizeof(_currentTapePath));
    _tapeReelLeftX = tape.reelLeftX;
    _tapeReelRightX = tape.reelRightX;
    _tapeReelY = tape.reelY;
    _loadBackground();
    _layout();

    if (_active && !_locked) {
        dsp.fillScreen(_bgcolor);
        if (_pngReady && _backgroundSprite) {
            _backgroundSprite->pushSprite(&dsp, _bodyX, _bodyY);
        } else {
            _drawBody();
        }
    }
}

void CassetteWidget::_layout() {
    _screenW = dsp.width();
    _screenH = dsp.height();

    _bodyW = _pngReady ? (int16_t)lroundf(kBackgroundWidth * _pngScale)
                        : std::min<int16_t>(432, (int16_t)_screenW - 24);
    _bodyH = _pngReady ? (int16_t)lroundf(kBackgroundHeight * _pngScale)
                        : std::min<int16_t>(270, (int16_t)_screenH - 30);
    _bodyX = ((int16_t)_screenW - _bodyW) / 2;
    _bodyY = ((int16_t)_screenH - _bodyH) / 2;

    if (_pngReady) {
        // Round each rect's EDGES once and derive width/height by
        // subtraction, instead of rounding X/Y/W/H independently. Rounding
        // each dimension on its own (the old code) leaves a sub-pixel gap
        // between where a fill/clip rect ends and where the underlying PNG
        // artwork's own edge actually falls after continuous scaling. That
        // gap is invisible at ~1x scale but grows with _pngScale, showing
        // up as a thin black bar on larger panels where the art is scaled
        // up more (e.g. ST7796). This does not change _bodyW/_bodyH or the
        // background sprite's target size -- only where we clip/fill on
        // top of it.
        auto edge = [&](float baseUnits) -> int16_t {
            return (int16_t)lroundf(baseUnits * _pngScale);
        };

        const int16_t labelLeft   = _bodyX + edge(70);
        const int16_t labelTop    = _bodyY + edge(36);
        const int16_t labelRight  = _bodyX + _bodyW - edge(30);
        const int16_t labelBottom = std::max<int16_t>(labelTop + 34, _bodyY + edge(36 + 44));
        _labelX = labelLeft;
        _labelY = labelTop;
        _labelW = labelRight - labelLeft;
        _labelH = labelBottom - labelTop;

        const int16_t infoLeft   = _bodyX + edge(62);
        const int16_t infoTop    = _bodyY + edge(178);
        const int16_t infoRight  = _bodyX + _bodyW - edge(62);
        const int16_t infoBottom = _bodyY + edge(178 + 22);
        _infoX = infoLeft;
        _infoY = infoTop;
        _infoW = infoRight - infoLeft;
        _infoH = infoBottom - infoTop;

        _leftReelX = _bodyX + edge(_tapeReelLeftX);
        _rightReelX = _bodyX + edge(_tapeReelRightX);
        _reelY = _bodyY + edge(_tapeReelY);
        _labelColor = TFT_BLACK;
        _windowColor = TFT_BLACK;
        _spoolColor = dsp.color565(204, 208, 206);
        return;
    }

    _labelX = _bodyX + 24;
    _labelY = _bodyY + 18;
    _labelW = _bodyW - 48;
    _labelH = 72;
    _infoX = _bodyX + 70;
    _infoY = _bodyY + _bodyH - 50;
    _infoW = _bodyW - 140;
    _infoH = 24;

    _windowX = _bodyX + 45;
    _windowY = _bodyY + 105;
    _windowW = _bodyW - 90;
    _windowH = 96;
    _leftReelX = _windowX + 63;
    _rightReelX = _windowX + _windowW - 63;
    _reelY = _windowY + _windowH / 2;

    _bodyColor = dsp.color565(44, 47, 52);
    _bodyEdgeColor = dsp.color565(184, 190, 194);
    _labelColor = dsp.color565(224, 213, 176);
    _windowColor = dsp.color565(14, 17, 20);
    _tapeColor = dsp.color565(83, 52, 29);
    _spoolColor = dsp.color565(204, 208, 206);
}

void CassetteWidget::_draw() {
    if (!_active || _locked) { return; }
    _layout();
    dsp.fillScreen(_bgcolor);
    if (_pngReady && _backgroundSprite) {
        _backgroundSprite->pushSprite(&dsp, _bodyX, _bodyY);
    } else {
        _drawBody();
    }
    _refreshMetadata(true);
    _refreshAudioInfo(true);
    _drawReels();
    _lastAnimMs = millis();
}

void CassetteWidget::_drawBody() {
    const uint16_t shellShadow = dsp.color565(10, 12, 14);
    const uint16_t shellHighlight = dsp.color565(111, 117, 120);
    const uint16_t shellSeam = dsp.color565(25, 28, 31);
    const uint16_t metalDark = dsp.color565(73, 77, 79);

    dsp.fillRoundRect(_bodyX + 5, _bodyY + 6, _bodyW, _bodyH, 12, shellShadow);
    dsp.fillRoundRect(_bodyX, _bodyY, _bodyW, _bodyH, 12, _bodyColor);
    dsp.drawRoundRect(_bodyX, _bodyY, _bodyW, _bodyH, 12, _bodyEdgeColor);
    dsp.drawRoundRect(_bodyX + 3, _bodyY + 3, _bodyW - 6, _bodyH - 6, 10, dsp.color565(79, 84, 88));
    dsp.drawLine(_bodyX + 18, _bodyY + 14, _bodyX + _bodyW - 18, _bodyY + 14, shellHighlight);
    dsp.drawLine(_bodyX + 8, _bodyY + _bodyH - 7, _bodyX + _bodyW - 8, _bodyY + _bodyH - 7, shellSeam);

    dsp.setFont(nullptr);
    dsp.setTextSize(1);
    dsp.setTextDatum(lgfx::top_center);
    dsp.setTextColor(_bodyEdgeColor, _bodyColor);
    dsp.drawString("FUSION EDGE   NORMAL POSITION   TYPE I", _bodyX + _bodyW / 2, _bodyY + 5);
    dsp.setTextDatum(lgfx::top_left);

    dsp.fillRoundRect(_labelX, _labelY, _labelW, _labelH, 5, _labelColor);
    dsp.drawRoundRect(_labelX, _labelY, _labelW, _labelH, 5, dsp.color565(247, 239, 206));
    dsp.fillRect(_labelX + 12, _labelY + 8, _labelW - 24, 3, _fgcolor);
    dsp.drawFastVLine(_labelX + 12, _labelY + 15, _labelH - 22, dsp.color565(184, 75, 57));
    dsp.drawFastVLine(_labelX + _labelW - 13, _labelY + 15, _labelH - 22, dsp.color565(184, 75, 57));

    dsp.fillRoundRect(_windowX - 5, _windowY - 5, _windowW + 10, _windowH + 10, 9, shellSeam);
    dsp.fillRoundRect(_windowX, _windowY, _windowW, _windowH, 7, _windowColor);
    dsp.drawRoundRect(_windowX, _windowY, _windowW, _windowH, 7, _bodyEdgeColor);
    dsp.drawRoundRect(_windowX + 3, _windowY + 3, _windowW - 6, _windowH - 6, 5, metalDark);
    dsp.fillRect(_leftReelX, _reelY - 3, _rightReelX - _leftReelX, 6, _tapeColor);
    dsp.drawFastHLine(_leftReelX, _reelY - 4, _rightReelX - _leftReelX, dsp.color565(126, 84, 45));

    const int16_t baseY = _bodyY + _bodyH - 52;
    const int16_t midX = _bodyX + _bodyW / 2;
    dsp.fillTriangle(_bodyX + 92, _bodyY + _bodyH - 10,
                     _bodyX + _bodyW - 92, _bodyY + _bodyH - 10,
                     midX, baseY, dsp.color565(27, 29, 32));
    dsp.drawLine(_bodyX + 92, _bodyY + _bodyH - 10, midX, baseY, _bodyEdgeColor);
    dsp.drawLine(midX, baseY, _bodyX + _bodyW - 92, _bodyY + _bodyH - 10, _bodyEdgeColor);

    const int16_t mechanismY = _bodyY + _bodyH - 25;
    dsp.fillCircle(midX - 73, mechanismY, 12, shellSeam);
    dsp.drawCircle(midX - 73, mechanismY, 12, shellHighlight);
    dsp.fillCircle(midX - 73, mechanismY, 5, metalDark);
    dsp.fillCircle(midX + 73, mechanismY, 12, shellSeam);
    dsp.drawCircle(midX + 73, mechanismY, 12, shellHighlight);
    dsp.fillCircle(midX + 73, mechanismY, 5, metalDark);
    dsp.fillRoundRect(midX - 22, mechanismY - 9, 44, 18, 3, shellSeam);
    dsp.drawRoundRect(midX - 22, mechanismY - 9, 44, 18, 3, shellHighlight);
    dsp.fillRect(midX - 8, mechanismY - 6, 16, 12, metalDark);
    dsp.fillCircle(midX - 114, mechanismY - 3, 5, shellSeam);
    dsp.fillCircle(midX + 114, mechanismY - 3, 5, shellSeam);

    const int16_t screwInset = 15;
    const int16_t screwY1 = _bodyY + 15;
    const int16_t screwY2 = _bodyY + _bodyH - 15;
    const auto drawScrew = [&](int16_t x, int16_t y) {
        dsp.fillCircle(x, y, 5, metalDark);
        dsp.drawCircle(x, y, 5, shellHighlight);
        dsp.drawLine(x - 3, y, x + 3, y, shellSeam);
        dsp.drawLine(x, y - 3, x, y + 3, shellSeam);
    };
    drawScrew(_bodyX + screwInset, screwY1);
    drawScrew(_bodyX + _bodyW - screwInset, screwY1);
    drawScrew(_bodyX + screwInset, screwY2);
    drawScrew(_bodyX + _bodyW - screwInset, screwY2);
}

void CassetteWidget::_drawTextLine(const char* text, int16_t y, uint16_t color, uint8_t preferredSize,
                                   bool transparentBg) {
    if (!text || !text[0]) { return; }

    const uint8_t sizes[] = {22, 20, 18, 16, 12};
    uint8_t* selected = nullptr;
    for (uint8_t size : sizes) {
        if (size > preferredSize) { continue; }
        uint8_t* candidate = fontForSize(size);
        if (!candidate) { continue; }
        dsp.loadFont(candidate);
        dsp.setTextSize(1);
        if (dsp.textWidth(text) <= _labelW - 24) {
            selected = candidate;
            break;
        }
        dsp.unloadFont();
    }

    if (!selected) {
        selected = fontForSize(12);
        if (selected) {
            dsp.loadFont(selected);
            dsp.setTextSize(1);
        } else {
            dsp.setFont(nullptr);
            dsp.setTextSize(1);
        }
    }

    char displayText[192] = {};
    const size_t sourceLen = strlen(text);
    size_t cut = std::min<size_t>(sourceLen, sizeof(displayText) - 4);
    while (cut > 0 && ((uint8_t)text[cut] & 0xC0U) == 0x80U) { --cut; }
    bool shortened = cut < sourceLen;
    do {
        memcpy(displayText, text, cut);
        displayText[cut] = '\0';
        if (shortened) { strlcat(displayText, "...", sizeof(displayText)); }
        if (dsp.textWidth(displayText) <= _labelW - 24 || cut == 0) { break; }
        shortened = true;
        --cut;
        while (cut > 0 && ((uint8_t)text[cut] & 0xC0U) == 0x80U) { --cut; }
    } while (true);

    dsp.setTextDatum(lgfx::middle_center);
    if (transparentBg) {
        // pngReady tapes: the label rect was just restored from the tape's
        // own printed artwork (not a flat fill), so an opaque two-color
        // drawString would paint a solid backdrop rectangle behind every
        // glyph that doesn't match that artwork -- the "black bar" look.
        // Draw foreground pixels only and let the restored art show through.
        dsp.setTextColor(color);
    } else {
        dsp.setTextColor(color, _labelColor);
    }
    dsp.setTextWrap(false, false);
    dsp.drawString(displayText, _labelX + _labelW / 2, y);
    dsp.setTextDatum(lgfx::top_left);
    if (selected) { dsp.unloadFont(); }
}

void CassetteWidget::_drawLabels(const char* station, const char* title) {
    // See CoverArtWidget::_composite() -- same free-running-DMA race, guarded
    // the same way. This and _drawAudioInfo()/_drawReels() all draw straight
    // to dsp on independent timers, so without this a redraw here can start
    // before a still-in-flight transfer from one of the others has settled.
    dsp.waitDMA();
    if (_pngReady && _backgroundSprite) {
        // Restore the tape's own printed label artwork under this rect
        // instead of blacking it out -- that erases the previous frame's
        // text back to the real art instead of leaving a solid black bar
        // where the label graphic should be.
        // Pad the clip rect by ~1-2px, scaled with _pngScale, so it fully
        // overwrites the previous frame's text even if that text (or this
        // rect) landed a pixel off from a rounding edge case -- cheap
        // insurance on top of the edge-consistent _layout() math above.
        const int16_t pad = std::max<int16_t>(1, (int16_t)lroundf(1.5f * _pngScale));
        dsp.setClipRect(_labelX - pad, _labelY - pad, _labelW + pad * 2, _labelH + pad * 2);
        _backgroundSprite->pushSprite(&dsp, _bodyX, _bodyY);
        dsp.clearClipRect();
    } else {
        dsp.fillRoundRect(_labelX + 4, _labelY + 13, _labelW - 8, _labelH - 17, 3, _labelColor);
        dsp.fillRect(_labelX + 12, _labelY + 8, _labelW - 24, 3, _fgcolor);
        dsp.drawFastVLine(_labelX + 12, _labelY + 15, _labelH - 22, dsp.color565(184, 75, 57));
        dsp.drawFastVLine(_labelX + _labelW - 13, _labelY + 15, _labelH - 22, dsp.color565(184, 75, 57));
    }

    char artist[128] = {};
    char track[192] = {};
    const char* separator = title ? strstr(title, " - ") : nullptr;
    if (separator) {
        const size_t artistLen = std::min<size_t>((size_t)(separator - title), sizeof(artist) - 1);
        memcpy(artist, title, artistLen);
        artist[artistLen] = '\0';
        strlcpy(track, separator + 3, sizeof(track));
    } else {
        strlcpy(artist, station && station[0] ? station : "FUSIONEDGE", sizeof(artist));
        strlcpy(track, title && title[0] ? title : "PLAYING", sizeof(track));
    }

    if (_pngReady) {
        // Raised 4px so the station/artist line sits higher in the label
        // instead of crowding its bottom edge.
        const int16_t line1Y = _labelY + std::max<int16_t>(9, (int16_t)lroundf(11 * _pngScale)) - 4;
        const int16_t lineSpacing = std::max<int16_t>(15, (int16_t)lroundf(21 * _pngScale));
        _drawTextLine(artist, line1Y, _fgcolor, 16, true);
        _drawTextLine(track, line1Y + lineSpacing, _trackTextColor, 18, true);
    } else {
        _drawTextLine(artist, _labelY + 31, _fgcolor, 20);
        _drawTextLine(track, _labelY + 55, _trackTextColor, 22);
    }
}

void CassetteWidget::_refreshMetadata(bool force) {
    if (!force && millis() - _lastMetaMs < 400) { return; }
    _lastMetaMs = millis();

    char station[sizeof(_lastStation)] = {};
    char title[sizeof(_lastTitle)] = {};
    strlcpy(station, config.station.name, sizeof(station));
    strlcpy(title, config.station.title, sizeof(title));

    const bool stationChanged = strcmp(station, _lastStation) != 0;
    if (!force && !stationChanged && strcmp(title, _lastTitle) == 0) { return; }

    if (force || stationChanged) { _selectTapeForStation(station); }

    strlcpy(_lastStation, station, sizeof(_lastStation));
    strlcpy(_lastTitle, title, sizeof(_lastTitle));
    _drawLabels(station, title);
}

void CassetteWidget::_drawAudioInfo(const char* info) {
    // See _drawLabels() above for why this is here.
    dsp.waitDMA();
    const uint16_t background = TFT_BLACK;
    if (_pngReady) {
        dsp.fillRect(_infoX, _infoY, _infoW, _infoH, background);
    } else {
        dsp.fillRoundRect(_infoX, _infoY, _infoW, _infoH, 3, background);
        dsp.drawRoundRect(_infoX, _infoY, _infoW, _infoH, 3, _bodyEdgeColor);
    }
    if (!info || !info[0]) { return; }

    uint8_t* selected = nullptr;
    const uint8_t sizes[] = {16, 12};
    for (uint8_t size : sizes) {
        uint8_t* candidate = fontForSize(size);
        if (!candidate) { continue; }
        dsp.loadFont(candidate);
        dsp.setTextSize(1);
        if (dsp.textWidth(info) <= _infoW - 10) {
            selected = candidate;
            break;
        }
        dsp.unloadFont();
    }
    if (!selected) {
        dsp.setFont(nullptr);
        dsp.setTextSize(1);
    }

    dsp.setTextDatum(lgfx::middle_center);
    dsp.setTextColor(_audioInfoColor, background);
    dsp.setTextWrap(false, false);
    dsp.drawString(info, _infoX + _infoW / 2, _infoY + _infoH / 2);
    dsp.setTextDatum(lgfx::top_left);
    if (selected) { dsp.unloadFont(); }
}

// Tries the most detailed token combination first and drops the least
// essential ones (bit depth, then sample rate) until something fits the
// available width at some font size, so small screens show less detail
// instead of an overflowing/clipped string. Returns the string that was
// selected (and drawn).
void CassetteWidget::_fitAudioInfo(const char* codec, const char* bitrateToken,
                                    const char* sampleToken, const char* bitsToken,
                                    char* out, size_t outSize) {
    char candidates[4][sizeof(_lastAudioInfo)] = {};
    int variantCount = 0;

    auto build = [&](bool withSample, bool withBits) {
        char* dest = candidates[variantCount];
        dest[0] = '\0';
        appendInfoToken(dest, sizeof(_lastAudioInfo), codec);
        appendInfoToken(dest, sizeof(_lastAudioInfo), bitrateToken);
        if (withSample) { appendInfoToken(dest, sizeof(_lastAudioInfo), sampleToken); }
        if (withBits) { appendInfoToken(dest, sizeof(_lastAudioInfo), bitsToken); }
        ++variantCount;
    };

    build(true, true);   // codec + bitrate + samplerate + bits
    build(true, false);  // codec + bitrate + samplerate
    build(false, false); // codec + bitrate
    // final fallback: codec only
    {
        char* dest = candidates[variantCount];
        dest[0] = '\0';
        appendInfoToken(dest, sizeof(_lastAudioInfo), codec);
        ++variantCount;
    }

    const uint8_t sizes[] = {16, 12};
    for (int v = 0; v < variantCount; ++v) {
        if (!candidates[v][0]) { continue; }
        for (uint8_t size : sizes) {
            uint8_t* candidateFont = fontForSize(size);
            if (!candidateFont) { continue; }
            dsp.loadFont(candidateFont);
            dsp.setTextSize(1);
            const bool fits = dsp.textWidth(candidates[v]) <= _infoW - 10;
            dsp.unloadFont();
            if (fits) {
                strlcpy(out, candidates[v], outSize);
                return;
            }
        }
    }
    // Nothing fit even at the smallest font: use the shortest non-empty
    // candidate (codec only, or whatever we have) rather than the fullest
    // string, since it's the closest to fitting.
    for (int v = variantCount - 1; v >= 0; --v) {
        if (candidates[v][0]) {
            strlcpy(out, candidates[v], outSize);
            return;
        }
    }
    out[0] = '\0';
}

void CassetteWidget::_refreshAudioInfo(bool force) {
    const uint32_t now = millis();
    if (!force && now - _lastAudioInfoMs < 750U) { return; }
    _lastAudioInfoMs = now;

    char codec[16] = {};
    uint32_t bitrateKbps = 0;
    uint32_t sampleRate = 0;
    uint8_t bits = 0;

#ifdef USE_BLUETOOTH
    if (config.getMode() == PM_BLUETOOTH) {
        strlcpy(codec, bluetooth.streamCodec(), sizeof(codec));
        sampleRate = bluetooth.streamSampleRate();
        bits = bluetooth.streamBits();
    } else
#endif
    {
        strlcpy(codec, formatName(config.configFmt), sizeof(codec));
        if (!codec[0]) {
            const int codecIndex = player.getCodec();
            if (codecIndex > 0 && codecIndex <= 8) {
                strlcpy(codec, player.getCodecname(), sizeof(codec));
            }
        }
        bitrateKbps = config.station.bitrate;
        if (!bitrateKbps) {
            const uint32_t bitrate = player.getBitRate();
            if (bitrate) { bitrateKbps = (bitrate + 500U) / 1000U; }
        }
        sampleRate = player.getSampleRate();
        bits = player.getBitsPerSample();
    }

    char bitrateToken[24] = {};
    if (bitrateKbps) { snprintf(bitrateToken, sizeof(bitrateToken), "%lukbps", (unsigned long)bitrateKbps); }

    char sampleToken[24] = {};
    if (sampleRate) {
        if (sampleRate % 1000U == 0U) {
            snprintf(sampleToken, sizeof(sampleToken), "%lukHz", (unsigned long)(sampleRate / 1000U));
        } else {
            snprintf(sampleToken, sizeof(sampleToken), "%lu.%lukHz",
                     (unsigned long)(sampleRate / 1000U),
                     (unsigned long)((sampleRate % 1000U) / 100U));
        }
    }

    char bitsToken[16] = {};
    if (bits) { snprintf(bitsToken, sizeof(bitsToken), "%uBit", (unsigned)bits); }

    char info[sizeof(_lastAudioInfo)] = {};
    _fitAudioInfo(codec, bitrateToken, sampleToken, bitsToken, info, sizeof(info));

    if (!force && strcmp(info, _lastAudioInfo) == 0) { return; }
    strlcpy(_lastAudioInfo, info, sizeof(_lastAudioInfo));
    _drawAudioInfo(info);
}

void CassetteWidget::_drawReel(int16_t centerX, float angle, int16_t tapeRadius) {
    // Same DMA race as _drawLabels()/_drawAudioInfo() (see comment there):
    // this fires twice a frame on its own animation timer, independent of
    // the label/info redraws, and both reel pushes write straight to dsp.
    // This was the one draw path in this file that was still missing the
    // guard -- the label/info fix alone didn't stop the reels from racing
    // against each other or against a label/info push still in flight,
    // which is what showed up as black speckling on ST7796 (480x320 takes
    // proportionally longer to flush than ILI9341/ST7789, so the window
    // for a collision is wider there).
    dsp.waitDMA();
    if (_pngReady && _pngScale >= 0.999f && _backgroundSprite && _reelSprite) {
        const int16_t sourceX = centerX - _bodyX - kReelCenter;
        const int16_t sourceY = _reelY - _bodyY - kReelCenter;
        _reelSprite->fillSprite(_windowColor);
        _backgroundSprite->pushSprite(_reelSprite, -sourceX, -sourceY);

        _reelSprite->fillCircle(kReelCenter, kReelCenter, 29, dsp.color565(9, 10, 11));
        _reelSprite->drawCircle(kReelCenter, kReelCenter, 29, dsp.color565(151, 154, 155));
        _reelSprite->drawCircle(kReelCenter, kReelCenter, 10, dsp.color565(96, 100, 102));
        for (uint8_t i = 0; i < 6; ++i) {
            const float a = angle + (float)i * (PI / 3.0f);
            const int16_t x1 = kReelCenter + (int16_t)(cosf(a) * 12.0f);
            const int16_t y1 = kReelCenter + (int16_t)(sinf(a) * 12.0f);
            const int16_t x2 = kReelCenter + (int16_t)(cosf(a) * 25.0f);
            const int16_t y2 = kReelCenter + (int16_t)(sinf(a) * 25.0f);
            _reelSprite->drawLine(x1, y1, x2, y2, _spoolColor);
            _reelSprite->drawLine(x1 + 1, y1, x2 + 1, y2, _spoolColor);
            _reelSprite->fillCircle(x2, y2, 3, _spoolColor);
        }
        _reelSprite->fillCircle(kReelCenter, kReelCenter, 5, _fgcolor);
        _reelSprite->pushSprite(centerX - kReelCenter, _reelY - kReelCenter);
        return;
    }

    if (_pngReady) {
        // Scaled-down art (small screen): the fixed-size crop/composite trick
        // above assumes native 456x291 art, so draw a simple, proportionally
        // scaled reel directly over the artwork instead.
        const int16_t outerRadius = std::max<int16_t>(9, (int16_t)lroundf(29 * _pngScale));
        const int16_t hubRadius = std::max<int16_t>(3, (int16_t)lroundf(10 * _pngScale));
        const int16_t centerRadius = std::max<int16_t>(2, (int16_t)lroundf(5 * _pngScale));
        dsp.fillCircle(centerX, _reelY, outerRadius, dsp.color565(9, 10, 11));
        dsp.drawCircle(centerX, _reelY, outerRadius, dsp.color565(151, 154, 155));
        dsp.drawCircle(centerX, _reelY, hubRadius, dsp.color565(96, 100, 102));
        const float spokeInner = 12.0f * _pngScale;
        const float spokeOuter = 25.0f * _pngScale;
        for (uint8_t i = 0; i < 6; ++i) {
            const float a = angle + (float)i * (PI / 3.0f);
            const int16_t x1 = centerX + (int16_t)(cosf(a) * spokeInner);
            const int16_t y1 = _reelY + (int16_t)(sinf(a) * spokeInner);
            const int16_t x2 = centerX + (int16_t)(cosf(a) * spokeOuter);
            const int16_t y2 = _reelY + (int16_t)(sinf(a) * spokeOuter);
            dsp.drawLine(x1, y1, x2, y2, _spoolColor);
            dsp.fillCircle(x2, y2, std::max<int16_t>(1, (int16_t)lroundf(3 * _pngScale)), _spoolColor);
        }
        dsp.fillCircle(centerX, _reelY, centerRadius, _fgcolor);
        return;
    }

    if (!_reelSprite) {
        dsp.fillRect(centerX - kReelCenter, _reelY - kReelCenter, kReelSpriteSize, kReelSpriteSize, _windowColor);
        dsp.fillCircle(centerX, _reelY, tapeRadius, _tapeColor);
        dsp.drawCircle(centerX, _reelY, tapeRadius - 2, dsp.color565(126, 84, 45));
        dsp.fillCircle(centerX, _reelY, 30, _spoolColor);
        dsp.fillCircle(centerX, _reelY, 9, _windowColor);
        return;
    }

    _reelSprite->fillSprite(_windowColor);
    _reelSprite->fillCircle(kReelCenter, kReelCenter, tapeRadius, _tapeColor);
    if (tapeRadius > 30) {
        _reelSprite->drawCircle(kReelCenter, kReelCenter, tapeRadius - 3, dsp.color565(126, 84, 45));
        _reelSprite->drawCircle(kReelCenter, kReelCenter, tapeRadius - 6, dsp.color565(58, 36, 21));
    }
    _reelSprite->fillCircle(kReelCenter, kReelCenter, 31, dsp.color565(112, 117, 119));
    _reelSprite->fillCircle(kReelCenter, kReelCenter, 27, _spoolColor);
    _reelSprite->fillCircle(kReelCenter, kReelCenter, 10, _windowColor);

    for (uint8_t i = 0; i < 6; ++i) {
        const float a = angle + (float)i * (PI / 3.0f);
        const int16_t x1 = kReelCenter + (int16_t)(cosf(a) * 11.0f);
        const int16_t y1 = kReelCenter + (int16_t)(sinf(a) * 11.0f);
        const int16_t x2 = kReelCenter + (int16_t)(cosf(a) * 25.0f);
        const int16_t y2 = kReelCenter + (int16_t)(sinf(a) * 25.0f);
        _reelSprite->drawLine(x1, y1, x2, y2, _windowColor);
        _reelSprite->drawLine(x1 + 1, y1, x2 + 1, y2, _windowColor);
        _reelSprite->fillCircle(x2, y2, 2, _windowColor);
    }
    _reelSprite->drawCircle(kReelCenter, kReelCenter, 31, _bodyEdgeColor);
    _reelSprite->fillCircle(kReelCenter, kReelCenter, 5, _fgcolor);
    _reelSprite->pushSprite(centerX - kReelCenter, _reelY - kReelCenter);
}

void CassetteWidget::_drawReels() {
    const float transfer = (sinf((float)_phase * 0.0174532925f) + 1.0f) * 0.5f;
    const int16_t leftRadius = 37 - (int16_t)(transfer * 10.0f);
    const int16_t rightRadius = 27 + (int16_t)(transfer * 10.0f);
    const float angle = -(float)_phase * 0.104719755f;
    _drawReel(_leftReelX, angle, leftRadius);
    _drawReel(_rightReelX, angle, rightRadius);
}

void CassetteWidget::loop() {
    if (!_active || _locked) { return; }
    _refreshMetadata();
    _refreshAudioInfo();

    const uint32_t now = millis();
    if (!player.isRunning() || now - _lastAnimMs < CASSETTE_FRAME_MS) { return; }
    _lastAnimMs = now;
    _phase = (_phase + 1) % 360;
    _drawReels();
}

void CassetteWidget::_clear() {
    dsp.fillScreen(_bgcolor);
}

void CassetteWidget::_reset() {
    _phase = 0;
    _lastAnimMs = 0;
    _lastMetaMs = 0;
    _lastAudioInfoMs = 0;
    _lastStation[0] = '\0';
    _lastTitle[0] = '\0';
    _lastAudioInfo[0] = '\0';
}

#endif
