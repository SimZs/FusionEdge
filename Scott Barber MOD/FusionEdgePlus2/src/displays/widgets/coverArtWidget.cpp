#include "coverArtWidget.h"

#if DSP_MODEL!= DSP_DUMMY && defined(USE_COVERART_SCREENSAVER)

#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/fonts.h"
#include "../../core/player.h"
#include "../../core/coverart.h"
#include "../display_select.h"

#include <LittleFS.h>
#include <algorithm>
#include <string.h>

namespace {

constexpr uint32_t COVER_POLL_MS = 800UL;
constexpr uint32_t META_CHECK_MS = 400UL;

uint8_t* fontForSize(uint8_t size) { return vlwBySize(size); }

const char* kFallbackCoverPath = "/images/stations/plmodeweb.png";

bool readPngDimensions(const uint8_t* buf, size_t len, uint16_t& outW, uint16_t& outH) {
 static const uint8_t kPngSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
 if (len < 24 || memcmp(buf, kPngSig, 8)!= 0 || memcmp(buf + 12, "IHDR", 4)!= 0) {
 return false;
 }
 outW = (uint16_t)((buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19]);
 outH = (uint16_t)((buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23]);
 return outW > 0 && outH > 0;
}

bool readJpegDimensions(const uint8_t* buf, size_t len, uint16_t& outW, uint16_t& outH) {
 if (len < 4 || buf[0]!= 0xFF || buf[1]!= 0xD8) return false;
 size_t offset = 2;
 while (offset + 9 < len) {
 if (buf[offset]!= 0xFF) { ++offset; continue; }
 const uint8_t marker = buf[offset + 1];
 if (marker == 0xD8 || marker == 0xD9) { offset += 2; continue; }
 if (offset + 3 >= len) break;
 const uint16_t segLen = (uint16_t)((buf[offset + 2] << 8) | buf[offset + 3]);
 if (segLen < 2 || offset + 2 + segLen > len) break;
 const bool isSof = (marker >= 0xC0 && marker <= 0xC3) ||
 (marker >= 0xC5 && marker <= 0xC7) ||
 (marker >= 0xC9 && marker <= 0xCB) ||
 (marker >= 0xCD && marker <= 0xCF);
 if (isSof && segLen >= 7) {
 outH = (uint16_t)((buf[offset + 5] << 8) | buf[offset + 6]);
 outW = (uint16_t)((buf[offset + 7] << 8) | buf[offset + 8]);
 return outW > 0 && outH > 0;
 }
 offset += 2 + segLen;
 }
 return false;
}

} // namespace

CoverArtWidget::~CoverArtWidget() {
 _freeCover();
 _freeFallbackCover();
 _deleteCanvas();
}

void CoverArtWidget::init(WidgetConfig conf, uint16_t fgcolor, uint16_t bgcolor) {
 Widget::init(conf, fgcolor, bgcolor);
 _barColor = TFT_BLACK;
 _ensureCanvas();
}

void CoverArtWidget::setColors(uint16_t fg, uint16_t bg) {
 Widget::setColors(fg, bg);
}

void CoverArtWidget::_ensureCanvas() {
 _screenW = dsp.width();
 _screenH = dsp.height();
 if (_canvas && (uint16_t)_canvas->width() == _screenW && (uint16_t)_canvas->height() == _screenH) {
 return;
 }
 _deleteCanvas();
 _canvas = new LGFX_Sprite(&dsp);
 if (!_canvas) return;
 _canvas->setColorDepth(16);
 _canvas->setPsram(true);
 if (!_canvas->createSprite(_screenW, _screenH)) {
 delete _canvas;
 _canvas = nullptr;
 log_w("##[COVERSS]# full-screen canvas allocation failed (%ux%u)",
 (unsigned)_screenW, (unsigned)_screenH);
 return;
 }
 _smallPanel = _screenH <= 250;
 _barH = _smallPanel? std::max<int16_t>(44, (int16_t)(_screenH * 0.18f))
 : std::max<int16_t>(46, (int16_t)(_screenH * 0.18f));
 _barY = (int16_t)_screenH - _barH; // bar at bottom, art above
}

void CoverArtWidget::_deleteCanvas() {
 if (!_canvas) return;
 _canvas->deleteSprite();
 delete _canvas;
 _canvas = nullptr;
}

void CoverArtWidget::_freeCover() {
 if (_coverBuf) { free(_coverBuf); _coverBuf = nullptr; }
 _coverSize = 0;
 _haveCover = false;
}

void CoverArtWidget::_freeFallbackCover() {
 if (_fallbackBuf) { free(_fallbackBuf); _fallbackBuf = nullptr; }
 _fallbackSize = 0;
 _fallbackLoaded = false;
 _fallbackValid = false;
}

bool CoverArtWidget::_ensureFallbackCover() {
 if (_fallbackLoaded) return _fallbackValid;
 _fallbackLoaded = true;

 File f = LittleFS.open(kFallbackCoverPath, "r");
 if (!f) {
 log_w("##[COVERSS]# fallback cover '%s' not found", kFallbackCoverPath);
 return false;
 }
 size_t sz = f.size();
 if (sz == 0) { f.close(); return false; }
 uint8_t* buf = (uint8_t*)ps_malloc(sz);
 if (!buf) { f.close(); return false; }
 const size_t readSz = f.read(buf, sz);
 f.close();
 if (readSz!= sz) {
 free(buf);
 return false;
 }

 uint16_t w = 0, h = 0;
 const bool dims = readPngDimensions(buf, sz, w, h);
 _fallbackBuf = buf;
 _fallbackSize = sz;
 _fallbackImgW = dims? w : _screenW;
 _fallbackImgH = dims? h : _screenH;
 _fallbackValid = true;
 return true;
}

bool CoverArtWidget::_tryFetchCover() {
 uint8_t* data = nullptr;
 size_t size = 0;
 bool jpeg = false;
 uint32_t generation = 0;
 if (!coverArt.copyReadyFor(config.station.title, config.getMode() == PM_BLUETOOTH,
 data, size, jpeg, generation)) {
 return false;
 }
 if (generation == _coverGeneration && _haveCover) {
 free(data);
 return true;
 }
 _freeCover();
 _coverBuf = data;
 _coverSize = size;
 _coverJpeg = jpeg;
 _coverGeneration = generation;

 uint16_t w = 0, h = 0;
 bool ok = jpeg? readJpegDimensions(_coverBuf, _coverSize, w, h)
 : readPngDimensions(_coverBuf, _coverSize, w, h);
 _coverImgW = ok? w : _screenW;
 _coverImgH = ok? h : _screenH;
 _haveCover = true;
 return true;
}

void CoverArtWidget::_composite() {
 if (!_canvas) return;
 DisplayMutexGuard guard(portMAX_DELAY);
 dsp.waitDMA();
 _canvas->fillSprite(_bgcolor);

 const int16_t coverAreaH = _barY; // area above the bottom bar
 if (_haveCover && _coverBuf && _coverSize > 0) {
 const float scaleX = _coverImgW > 0? (float)_screenW / (float)_coverImgW : 1.0f;
 const float scaleY = _coverImgH > 0? (float)coverAreaH / (float)_coverImgH : 1.0f;
 const float scale = std::min(scaleX, scaleY);
#ifdef COVERART_DEBUG_HEAP
 log_w("##[COVERSS_HEAP]# pre-decode free=%u minFree=%u imgW=%u imgH=%u scale=%.3f jpeg=%d",
 (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
 (unsigned)_coverImgW, (unsigned)_coverImgH, scale, (int)_coverJpeg);
#endif
 if (_coverJpeg) {
 _canvas->drawJpg(_coverBuf, _coverSize, 0, 0, _screenW, coverAreaH, 0, 0,
 scale, scale, datum_t::middle_center);
 } else {
 _canvas->drawPng(_coverBuf, _coverSize, 0, 0, _screenW, coverAreaH, 0, 0,
 scale, scale, datum_t::middle_center);
 }
#ifdef COVERART_DEBUG_HEAP
 log_w("##[COVERSS_HEAP]# post-decode free=%u minFree=%u",
 (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
#endif
 } else if (_ensureFallbackCover()) {
 const float scaleX = _fallbackImgW > 0? (float)_screenW / (float)_fallbackImgW : 1.0f;
 const float scaleY = _fallbackImgH > 0? (float)coverAreaH / (float)_fallbackImgH : 1.0f;
 const float scale = std::min(scaleX, scaleY);
 _canvas->drawPng(_fallbackBuf, _fallbackSize, 0, 0, _screenW, coverAreaH, 0, 0,
 scale, scale, datum_t::middle_center);
 } else {
 const int16_t divOuter = _smallPanel? 9 : 6;
 const int16_t divInner = _smallPanel? 27 : 18;
 const int16_t shortSide = std::min(_screenW, _screenH);
 _canvas->fillCircle(_screenW / 2, coverAreaH / 2, shortSide / divOuter, _fgcolor);
 _canvas->fillCircle(_screenW / 2, coverAreaH / 2, shortSide / divInner, _bgcolor);
 }

 _drawInfoBar(config.station.name, config.station.title);
 _canvas->pushSprite(&dsp, 0, 0);
}

void CoverArtWidget::_drawTextLine(const char* text, int16_t y, uint16_t color,
 uint8_t preferredSize, int16_t maxWidth, int16_t centerX) {
 if (!text ||!text[0] ||!_canvas) return;

 const uint8_t sizes[] = {22, 20, 18, 16, 12};
 uint8_t* selected = nullptr;
 for (uint8_t size : sizes) {
 if (size > preferredSize) { continue; }
 uint8_t* candidate = fontForSize(size);
 if (!candidate) { continue; }
 _canvas->loadFont(candidate);
 _canvas->setTextSize(1);
 if (_canvas->textWidth(text) <= maxWidth) {
 selected = candidate;
 break;
 }
 _canvas->unloadFont();
 }
 if (!selected) {
 selected = fontForSize(12);
 if (selected) {
 _canvas->loadFont(selected);
 _canvas->setTextSize(1);
 } else {
 _canvas->setFont(nullptr);
 _canvas->setTextSize(1);
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
 if (_canvas->textWidth(displayText) <= maxWidth || cut == 0) { break; }
 shortened = true;
 --cut;
 while (cut > 0 && ((uint8_t)text[cut] & 0xC0U) == 0x80U) { --cut; }
 } while (true);

 _canvas->setTextDatum(lgfx::middle_center);
 _canvas->setTextColor(color);
 _canvas->setTextWrap(false, false);
 _canvas->drawString(displayText, centerX, y);
 _canvas->setTextDatum(lgfx::top_left);
 if (selected) { _canvas->unloadFont(); }
}

void CoverArtWidget::_drawInfoBar(const char* station, const char* title) {
 if (!_canvas) return;
 _canvas->fillRect(0, _barY, _screenW, _barH, _barColor);

 char artist[128] = {};
 char track[192] = {};
 const char* separator = title? strstr(title, " - ") : nullptr;
 if (separator) {
 const size_t artistLen = std::min<size_t>((size_t)(separator - title), sizeof(artist) - 1);
 memcpy(artist, title, artistLen);
 artist[artistLen] = '\0';
 strlcpy(track, separator + 3, sizeof(track));
 } else {
 strlcpy(artist, station && station[0]? station : "FUSIONEDGE", sizeof(artist));
 strlcpy(track, title && title[0]? title : "", sizeof(track));
 }

 const int16_t centerX = _screenW / 2;
 const int16_t pad = _smallPanel? 8 : 16;
 const int16_t maxW = _screenW - pad * 2;
 const uint8_t artistSize = _smallPanel? 16 : 18;
 const uint8_t trackSize = _smallPanel? 16 : 20;
 if (track[0]!= '\0') {
 _drawTextLine(artist, _barY + (int16_t)(_barH * 0.30f) - 4, _trackTextColor, artistSize, maxW, centerX);
 _drawTextLine(track, _barY + (int16_t)(_barH * 0.60f), _infoTextColor, trackSize, maxW, centerX);
 } else {
 _drawTextLine(artist, _barY + _barH / 2, _trackTextColor, trackSize, maxW, centerX);
 }
}

void CoverArtWidget::_draw() {
 if (!_active || _locked) return;
 _ensureCanvas();
 strlcpy(_lastStation, config.station.name, sizeof(_lastStation));
 strlcpy(_lastTitle, config.station.title, sizeof(_lastTitle));
 _tryFetchCover();
 _composite();
 _lastMetaMs = millis();
 _lastPollMs = millis();
}

void CoverArtWidget::loop() {
 if (!_active || _locked ||!_canvas) return;

 const uint32_t now = millis();
 bool needsRedraw = false;

 if (now - _lastMetaMs >= META_CHECK_MS) {
 _lastMetaMs = now;
 if (strcmp(config.station.name, _lastStation)!= 0 ||
 strcmp(config.station.title, _lastTitle)!= 0) {
 strlcpy(_lastStation, config.station.name, sizeof(_lastStation));
 strlcpy(_lastTitle, config.station.title, sizeof(_lastTitle));
 _freeCover();
 _coverGeneration = 0;
 needsRedraw = true;
 }
 }

 if (!_haveCover && now - _lastPollMs >= COVER_POLL_MS) {
 _lastPollMs = now;
 if (_tryFetchCover()) { needsRedraw = true; }
 }

 if (needsRedraw) { _composite(); }
}

void CoverArtWidget::_clear() {
 DisplayMutexGuard guard(portMAX_DELAY);
 dsp.waitDMA();
 dsp.fillScreen(_bgcolor);
}

void CoverArtWidget::_reset() {
 _freeCover();
 _coverGeneration = 0;
 _lastStation[0] = '\0';
 _lastTitle[0] = '\0';
 _lastMetaMs = 0;
 _lastPollMs = 0;
}

#endif
