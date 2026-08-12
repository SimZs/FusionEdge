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
    if (destination[0]) { strlcat(destination, " \xC2\xB7 ", destinationSize); }
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

    if (dsp.width() < kBackgroundWidth || dsp.height() < kBackgroundHeight) {
        log_w("##[CASSETTE]# display too small for %dx%d PNG, using code-drawn fallback",
              kBackgroundWidth, kBackgroundHeight);
        return false;
    }

    File file = LittleFS.open(CASSETTE_PNG_PATH, "r");
    if (!file) {
        log_w("##[CASSETTE]# PNG not found: %s, using code-drawn fallback", CASSETTE_PNG_PATH);
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

    _backgroundSprite = new LGFX_Sprite(&dsp);
    if (!_backgroundSprite) {
        free(pngBuffer);
        return false;
    }
    _backgroundSprite->setColorDepth(16);
    _backgroundSprite->setPsram(true);
    if (!_backgroundSprite->createSprite(kBackgroundWidth, kBackgroundHeight)) {
        free(pngBuffer);
        _deleteBackground();
        log_w("##[CASSETTE]# background sprite allocation failed, using code-drawn fallback");
        return false;
    }

    _backgroundSprite->fillSprite(_bgcolor);
    _backgroundSprite->drawPng(pngBuffer, pngSize, 0, 0);
    free(pngBuffer);
    _pngReady = true;
    return true;
}

void CassetteWidget::_layout() {
    _screenW = dsp.width();
    _screenH = dsp.height();

    _bodyW = _pngReady ? kBackgroundWidth : std::min<int16_t>(432, (int16_t)_screenW - 24);
    _bodyH = _pngReady ? kBackgroundHeight : std::min<int16_t>(270, (int16_t)_screenH - 30);
    _bodyX = ((int16_t)_screenW - _bodyW) / 2;
    _bodyY = ((int16_t)_screenH - _bodyH) / 2;

    if (_pngReady) {
        _labelX = _bodyX + 70;
        _labelY = _bodyY + 36;
        _labelW = _bodyW - 100;
        _labelH = 44;
        _infoX = _bodyX + 62;
        _infoY = _bodyY + 178;
        _infoW = _bodyW - 124;
        _infoH = 22;
        _leftReelX = _bodyX + kPngLeftReelX;
        _rightReelX = _bodyX + kPngRightReelX;
        _reelY = _bodyY + kPngReelY;
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

void CassetteWidget::_drawTextLine(const char* text, int16_t y, uint16_t color, uint8_t preferredSize) {
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
    dsp.setTextColor(color, _labelColor);
    dsp.setTextWrap(false, false);
    dsp.drawString(displayText, _labelX + _labelW / 2, y);
    dsp.setTextDatum(lgfx::top_left);
    if (selected) { dsp.unloadFont(); }
}

void CassetteWidget::_drawLabels(const char* station, const char* title) {
    if (_pngReady) {
        dsp.fillRect(_labelX, _labelY, _labelW, _labelH, _labelColor);
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
        _drawTextLine(artist, _labelY + 11, _fgcolor, 16);
        _drawTextLine(track, _labelY + 32, _trackTextColor, 18);
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

    if (!force && strcmp(station, _lastStation) == 0 && strcmp(title, _lastTitle) == 0) { return; }
    strlcpy(_lastStation, station, sizeof(_lastStation));
    strlcpy(_lastTitle, title, sizeof(_lastTitle));
    _drawLabels(station, title);
}

void CassetteWidget::_drawAudioInfo(const char* info) {
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

    char info[sizeof(_lastAudioInfo)] = {};
    appendInfoToken(info, sizeof(info), codec);

    char token[24] = {};
    if (bitrateKbps) {
        snprintf(token, sizeof(token), "%lukbps", (unsigned long)bitrateKbps);
        appendInfoToken(info, sizeof(info), token);
    }
    if (sampleRate) {
        if (sampleRate % 1000U == 0U) {
            snprintf(token, sizeof(token), "%lukHz", (unsigned long)(sampleRate / 1000U));
        } else {
            snprintf(token, sizeof(token), "%lu.%lukHz",
                     (unsigned long)(sampleRate / 1000U),
                     (unsigned long)((sampleRate % 1000U) / 100U));
        }
        appendInfoToken(info, sizeof(info), token);
    }
    if (bits) {
        snprintf(token, sizeof(token), "%uBit", (unsigned)bits);
        appendInfoToken(info, sizeof(info), token);
    }

    if (!force && strcmp(info, _lastAudioInfo) == 0) { return; }
    strlcpy(_lastAudioInfo, info, sizeof(_lastAudioInfo));
    _drawAudioInfo(info);
}

void CassetteWidget::_drawReel(int16_t centerX, float angle, int16_t tapeRadius) {
    if (_pngReady && _backgroundSprite && _reelSprite) {
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

    if (_pngReady) { return; }

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
