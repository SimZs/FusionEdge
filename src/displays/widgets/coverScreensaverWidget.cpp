#include "../../core/options.h"

#if DSP_MODEL != DSP_DUMMY && defined(USE_COVER_SCREENSAVER)

#include <LittleFS.h>

#include "../../core/config.h"
#include "../../core/fonts.h"
#include "../display_select.h"
#include "coverScreensaverWidget.h"

// Album-cover screensaver concept and original implementation by Volodimyr Zagranichniy.
static_assert(DSP_WIDTH == 480 && DSP_HEIGHT == 320,
              "USE_COVER_SCREENSAVER currently requires a 480x320 display");

namespace {

constexpr char STATION_MAP_FILE[] = "/images/stations/map.csv";

void loadFont(LGFX_Sprite& canvas, uint8_t* font) {
    if (font) {
        canvas.loadFont(font);
    } else {
        canvas.unloadFont();
        canvas.setFont(nullptr);
    }
    canvas.setTextSize(1);
}

uint8_t bestFontSize(uint8_t wanted, uint8_t minimum) {
    constexpr uint8_t sizes[] = {36, 26, 24, 22, 20, 18, 16, 12, 9};
    for (uint8_t size : sizes) {
        if (size <= wanted && size >= minimum && vlwBySize(size)) return size;
    }
    return wanted;
}

void trimUtf8ToWidth(LGFX_Sprite& canvas, char* text, int16_t maxWidth) {
    size_t length = strlen(text);
    while (length > 0 && canvas.textWidth(text) > maxWidth) {
        do {
            --length;
        } while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0U) == 0x80U);
        text[length] = '\0';
    }
}

uint16_t drawWrappedText(LGFX_Sprite& canvas, const char* text,
                         int16_t centerX, int16_t y, int16_t maxWidth,
                         uint8_t wantedSize, uint8_t minimumSize,
                         uint8_t maxLines) {
    if (!text || !text[0] || maxWidth <= 0 || maxLines == 0) return 0;

    constexpr uint8_t sizes[] = {36, 26, 24, 22, 20, 18, 16, 12, 9};
    uint8_t size = bestFontSize(wantedSize, minimumSize);

    for (uint8_t candidate : sizes) {
        if (candidate > size || candidate < minimumSize || !vlwBySize(candidate)) continue;
        loadFont(canvas, vlwBySize(candidate));

        bool wordTooWide = false;
        const char* cursor = text;
        while (*cursor) {
            while (*cursor == ' ') ++cursor;
            if (!*cursor) break;

            char word[96] = {};
            size_t length = 0;
            while (*cursor && *cursor != ' ' && length < sizeof(word) - 1) {
                word[length++] = *cursor++;
            }
            word[length] = '\0';
            if (canvas.textWidth(word) > maxWidth) {
                wordTooWide = true;
                break;
            }
        }

        size = candidate;
        if (!wordTooWide) break;
    }

    loadFont(canvas, vlwBySize(size));
    const uint16_t lineHeight = canvas.fontHeight();
    canvas.setTextDatum(top_center);

    char line[192] = {};
    uint8_t lines = 0;
    const char* cursor = text;

    while (*cursor && lines < maxLines) {
        while (*cursor == ' ') ++cursor;
        if (!*cursor) break;

        char word[96] = {};
        size_t wordLength = 0;
        while (*cursor && *cursor != ' ' && wordLength < sizeof(word) - 1) {
            word[wordLength++] = *cursor++;
        }
        word[wordLength] = '\0';

        char candidate[192] = {};
        strlcpy(candidate, line, sizeof(candidate));
        if (candidate[0]) strlcat(candidate, " ", sizeof(candidate));
        strlcat(candidate, word, sizeof(candidate));

        if (canvas.textWidth(candidate) <= maxWidth) {
            strlcpy(line, candidate, sizeof(line));
            continue;
        }

        if (line[0]) {
            canvas.drawString(line, centerX, y + lines * (lineHeight + 3));
            ++lines;
            line[0] = '\0';
            if (lines >= maxLines) break;
        }

        trimUtf8ToWidth(canvas, word, maxWidth);
        strlcpy(line, word, sizeof(line));
    }

    if (line[0] && lines < maxLines) {
        canvas.drawString(line, centerX, y + lines * (lineHeight + 3));
        ++lines;
    }

    if (vlwBySize(size)) canvas.unloadFont();
    return lines ? lines * lineHeight + (lines - 1) * 3 : 0;
}

void drawClock(LGFX_Sprite& canvas, int16_t x, int16_t y,
               int16_t maxWidth, const char* timeText) {
    int hour = 0;
    int minute = 0;
    if (!timeText || sscanf(timeText, "%d:%d", &hour, &minute) != 2) return;

    uint8_t* mainFont = nullptr;
    uint8_t* secondaryFont = nullptr;
    getClockFontStylePointers(config.store.clockFontStyle, &mainFont, &secondaryFont);
    loadFont(canvas, mainFont);

    char mainTime[8] = {};
    if (config.store.clockAmPmStyle) {
        int hour12 = hour % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(mainTime, sizeof(mainTime), "%2d:%02d", hour12, minute);
    } else {
        snprintf(mainTime, sizeof(mainTime), "%02d:%02d", hour, minute);
    }

    const uint16_t timeWidth = canvas.textWidth(mainTime);
    const uint16_t blockWidth = canvas.textWidth("88:88");
    const uint16_t timeHeight = canvas.fontHeight();
    int16_t timeX = x + static_cast<int16_t>(blockWidth) - static_cast<int16_t>(timeWidth);
    if (timeX < x) timeX = x;
    if (timeX + timeWidth > x + maxWidth) timeX = x + maxWidth - timeWidth;
    if (timeX < x) timeX = x;

    if (config.store.clockFontStyle == CLOCKFONT_STYLE_DIGI7 && config.store.clockFontMono) {
        const char* ghost = config.store.clockAmPmStyle ? " 8:88" : "88:88";
        canvas.setTextColor(config.theme.clockbg, TFT_BLACK);
        canvas.setTextDatum(top_left);
        canvas.drawString(ghost, timeX, y);
    }

    canvas.setTextColor(config.theme.clock, TFT_BLACK);
    canvas.setTextDatum(top_left);
    canvas.drawString(mainTime, timeX, y);
    if (mainFont) canvas.unloadFont();

    if (!config.store.clockAmPmStyle) return;

    loadFont(canvas, font_vlw_18);
    const char* amPm = hour >= 12 ? "PM" : "AM";
    const uint16_t amPmWidth = canvas.textWidth(amPm);
    const uint16_t amPmHeight = canvas.fontHeight();
    const int16_t rightX = x + blockWidth + 4;
    const int16_t rightWidth = maxWidth > blockWidth + 4 ? maxWidth - blockWidth - 4 : 0;
    int16_t amPmX = rightX + (rightWidth - static_cast<int16_t>(amPmWidth)) / 2;
    if (amPmX < rightX) amPmX = rightX;
    const int16_t amPmY = y + max<int16_t>(0, static_cast<int16_t>(timeHeight / 4) - static_cast<int16_t>(amPmHeight / 2));

    canvas.setTextColor(config.theme.seconds, TFT_BLACK);
    canvas.setTextDatum(top_left);
    canvas.drawString(amPm, amPmX, amPmY);
    if (font_vlw_18) canvas.unloadFont();
}

bool stationIconPath(const char* stationName, uint8_t playMode,
                     char* path, size_t pathSize) {
    const char* fallback = "/images/stations/plmodeweb.png";
    if (playMode == DPS_SDCARD) fallback = "/images/stations/plmodesd.png";
    else if (playMode == DPS_DLNA) fallback = "/images/stations/plmodedlna.png";
#ifdef USE_BLUETOOTH
    else if (playMode == DPS_BLUETOOTH) fallback = "/images/stations/plmodebt.png";
#endif

    if (playMode != DPS_WEB || !stationName || !stationName[0]) {
        strlcpy(path, fallback, pathSize);
        return false;
    }

    File map = LittleFS.open(STATION_MAP_FILE, "r");
    if (map) {
        char line[128];
        while (map.available()) {
            size_t length = 0;
            while (map.available() && length < sizeof(line) - 1) {
                const char character = map.read();
                if (character == '\n') break;
                if (character != '\r') line[length++] = character;
            }
            line[length] = '\0';
            if (!length || line[0] == '#') continue;

            char* separator = strchr(line, '\t');
            if (!separator) continue;
            *separator = '\0';
            if (strcasecmp(line, stationName) == 0) {
                snprintf(path, pathSize, "/images/stations/%s", separator + 1);
                map.close();
                return true;
            }
        }
        map.close();
    }

    strlcpy(path, fallback, pathSize);
    return false;
}

}  // namespace

CoverScreensaverWidget::~CoverScreensaverWidget() {
    _freeImage();
    _freeWeather();
    _deleteSprite();
}

void CoverScreensaverWidget::init(WidgetConfig widgetConfig, uint16_t background) {
    Widget::init(widgetConfig, 0, background);
    _ensureSprite();
}

void CoverScreensaverWidget::setColors(uint16_t foreground, uint16_t background) {
    Widget::setColors(foreground, background);
    _imageDirty = true;
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::_ensureSprite() {
    if (_sprite) return;
    _sprite = new LGFX_Sprite(&dsp);
    if (!_sprite) return;

    _sprite->setColorDepth(16);
    _sprite->setPsram(true);
    if (_sprite->createSprite(COVER_WIDTH + PANEL_WIDTH, HEIGHT) == nullptr) {
        log_w("##[COVER-SS]# unable to allocate 480x320 PSRAM sprite");
        delete _sprite;
        _sprite = nullptr;
    }
}

void CoverScreensaverWidget::_deleteSprite() {
    if (!_sprite) return;
    _sprite->deleteSprite();
    delete _sprite;
    _sprite = nullptr;
}

void CoverScreensaverWidget::_pushSprite() {
    if (!_sprite) return;
#if DSP_MODEL == DSP_AXS15231B
    auto* pixels = static_cast<uint16_t*>(_sprite->getBuffer());
    if (pixels && dsp.blitFrameBlock(_config.left, _config.top,
                                     _sprite->width(), _sprite->height(), pixels)) return;
#endif
    _sprite->pushSprite(_config.left, _config.top);
}

void CoverScreensaverWidget::_freeImage() {
    if (_imageData) free(_imageData);
    _imageData = nullptr;
    _imageSize = 0;
    _imageValid = false;
    _imageJpeg = false;
    _imageWidth = COVER_WIDTH;
    _imageHeight = HEIGHT;
    _coverGeneration = 0;
    _imagePath[0] = '\0';
    _imageDirty = true;
}

void CoverScreensaverWidget::_freeWeather() {
    if (_weatherData) free(_weatherData);
    _weatherData = nullptr;
    _weatherSize = 0;
    _weatherCode[0] = '\0';
}

void CoverScreensaverWidget::_detectImageSize() {
    _imageWidth = COVER_WIDTH;
    _imageHeight = HEIGHT;
    if (!_imageData || _imageSize < 24) return;

    if (!_imageJpeg && _imageData[0] == 0x89 && _imageData[1] == 'P' &&
        _imageData[2] == 'N' && _imageData[3] == 'G') {
        _imageWidth = static_cast<uint16_t>((static_cast<uint32_t>(_imageData[16]) << 24) |
                                            (static_cast<uint32_t>(_imageData[17]) << 16) |
                                            (static_cast<uint32_t>(_imageData[18]) << 8) |
                                            _imageData[19]);
        _imageHeight = static_cast<uint16_t>((static_cast<uint32_t>(_imageData[20]) << 24) |
                                             (static_cast<uint32_t>(_imageData[21]) << 16) |
                                             (static_cast<uint32_t>(_imageData[22]) << 8) |
                                             _imageData[23]);
        return;
    }

    if (!_imageJpeg || _imageData[0] != 0xFF || _imageData[1] != 0xD8) return;
    size_t offset = 2;
    while (offset + 9 < _imageSize) {
        if (_imageData[offset] != 0xFF) {
            ++offset;
            continue;
        }
        const uint8_t marker = _imageData[offset + 1];
        if (marker == 0xD8 || marker == 0xD9) {
            offset += 2;
            continue;
        }
        if (offset + 3 >= _imageSize) break;
        const uint16_t segmentLength = (static_cast<uint16_t>(_imageData[offset + 2]) << 8) |
                                       _imageData[offset + 3];
        if (segmentLength < 2 || offset + 2 + segmentLength > _imageSize) break;
        const bool startOfFrame = (marker >= 0xC0 && marker <= 0xC3) ||
                                  (marker >= 0xC5 && marker <= 0xC7) ||
                                  (marker >= 0xC9 && marker <= 0xCB) ||
                                  (marker >= 0xCD && marker <= 0xCF);
        if (startOfFrame && segmentLength >= 7) {
            _imageHeight = (static_cast<uint16_t>(_imageData[offset + 5]) << 8) |
                           _imageData[offset + 6];
            _imageWidth = (static_cast<uint16_t>(_imageData[offset + 7]) << 8) |
                          _imageData[offset + 8];
            return;
        }
        offset += 2 + segmentLength;
    }
}

bool CoverScreensaverWidget::_loadImage(const char* path) {
    if (!path || !path[0]) return false;
    if (_imageValid && _coverGeneration == 0 && strcmp(_imagePath, path) == 0) return true;

    File file = LittleFS.open(path, "r");
    if (!file) return false;
    const size_t size = file.size();
    if (!size) {
        file.close();
        return false;
    }

    uint8_t* data = static_cast<uint8_t*>(ps_malloc(size));
    if (!data) {
        file.close();
        return false;
    }
    const size_t bytesRead = file.read(data, size);
    file.close();
    if (bytesRead != size) {
        free(data);
        return false;
    }

    _freeImage();
    _imageData = data;
    _imageSize = size;
    _imageValid = true;
    _imageJpeg = false;
    strlcpy(_imagePath, path, sizeof(_imagePath));
    _detectImageSize();
    return true;
}

bool CoverScreensaverWidget::_loadWeather(const char* code) {
    if (!code || !code[0]) {
        _freeWeather();
        return false;
    }
    if (_weatherData && strcmp(_weatherCode, code) == 0) return true;

    char path[48];
    snprintf(path, sizeof(path), "/images/weather/%s.png", code);
    File file = LittleFS.open(path, "r");
    if (!file) {
        _freeWeather();
        return false;
    }
    const size_t size = file.size();
    uint8_t* data = size ? static_cast<uint8_t*>(ps_malloc(size)) : nullptr;
    if (!data) {
        file.close();
        _freeWeather();
        return false;
    }
    const size_t bytesRead = file.read(data, size);
    file.close();
    if (bytesRead != size) {
        free(data);
        _freeWeather();
        return false;
    }

    _freeWeather();
    _weatherData = data;
    _weatherSize = size;
    strlcpy(_weatherCode, code, sizeof(_weatherCode));
    return true;
}

void CoverScreensaverWidget::setCover(uint8_t* data, size_t size,
                                      bool jpeg, uint32_t generation) {
    if (!data || !size || !generation) {
        if (data) free(data);
        return;
    }
    if (_imageValid && _coverGeneration == generation && !_imagePath[0]) {
        free(data);
        return;
    }

    _freeImage();
    _imageData = data;
    _imageSize = size;
    _imageValid = true;
    _imageJpeg = jpeg;
    _coverGeneration = generation;
    _detectImageSize();
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setStation(const char* stationName, uint8_t playMode) {
    char path[96] = {};
    stationIconPath(stationName, playMode, path, sizeof(path));
    if (!_loadImage(path)) {
        const char* fallback = playMode == DPS_SDCARD ? "/images/stations/plmodesd.png" :
                               playMode == DPS_DLNA ? "/images/stations/plmodedlna.png" :
#ifdef USE_BLUETOOTH
                               playMode == DPS_BLUETOOTH ? "/images/stations/plmodebt.png" :
#endif
                               "/images/stations/plmodeweb.png";
        if (!_loadImage(fallback)) _freeImage();
    }
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setTime(const char* time) {
    char minuteTime[8] = {};
    if (time) {
        int hour = 0;
        int minute = 0;
        if (sscanf(time, "%d:%d", &hour, &minute) == 2) {
            snprintf(minuteTime, sizeof(minuteTime), "%02d:%02d", hour, minute);
        }
    }
    if (strcmp(_time, minuteTime) == 0) return;
    strlcpy(_time, minuteTime, sizeof(_time));
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setDate(const char* date) {
    const char* value = date ? date : "";
    if (strcmp(_date, value) == 0) return;
    strlcpy(_date, value, sizeof(_date));
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setTrack(const char* artist, const char* title) {
    const char* artistValue = artist ? artist : "";
    const char* titleValue = title ? title : "";
    if (strcmp(_artist, artistValue) == 0 && strcmp(_title, titleValue) == 0) return;
    strlcpy(_artist, artistValue, sizeof(_artist));
    strlcpy(_title, titleValue, sizeof(_title));
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setArtist(const char* artist) {
    const char* value = artist ? artist : "";
    if (strcmp(_artist, value) == 0) return;
    strlcpy(_artist, value, sizeof(_artist));
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setTitle(const char* title) {
    const char* value = title ? title : "";
    if (strcmp(_title, value) == 0) return;
    strlcpy(_title, value, sizeof(_title));
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::setWeather(const char* code, float temperature) {
    _loadWeather(code);
#ifdef IMPERIALUNIT
    snprintf(_temperature, sizeof(_temperature), "%.0f \xC2\xB0" "F", temperature);
#else
    snprintf(_temperature, sizeof(_temperature), "%.0f \xC2\xB0" "C", temperature);
#endif
    _infoDirty = true;
    if (_active && !_locked) _draw();
}

void CoverScreensaverWidget::_draw() {
    if (!_active) return;
    _ensureSprite();
    if (!_sprite) return;

    if (_imageDirty) {
        _sprite->fillRect(0, 0, COVER_WIDTH, HEIGHT, _bgcolor);
        if (_imageValid && _imageData && _imageSize) {
            constexpr int16_t margin = 6;
            constexpr int16_t imageSize = COVER_WIDTH - margin * 2;
            const float scaleX = _imageWidth ? static_cast<float>(imageSize) / _imageWidth : 1.0f;
            const float scaleY = _imageHeight ? static_cast<float>(imageSize) / _imageHeight : 1.0f;
            const float scale = min(scaleX, scaleY);

            if (_imageJpeg) {
                _sprite->drawJpg(_imageData, _imageSize, margin, margin,
                                 imageSize, imageSize, 0, 0, scale, scale,
                                 datum_t::middle_center);
            } else {
                _sprite->fillRect(margin, margin, imageSize, imageSize, 0x2104);
                _sprite->drawPng(_imageData, _imageSize, margin, margin,
                                 imageSize, imageSize, 0, 0, scale, scale,
                                 datum_t::middle_center);
            }

            constexpr int16_t radius = 14;
            for (int16_t dy = 0; dy < radius; ++dy) {
                for (int16_t dx = 0; dx < radius; ++dx) {
                    const int16_t cx = radius - 1 - dx;
                    const int16_t cy = radius - 1 - dy;
                    if (cx * cx + cy * cy <= radius * radius) continue;
                    _sprite->drawPixel(margin + dx, margin + dy, _bgcolor);
                    _sprite->drawPixel(margin + imageSize - 1 - dx, margin + dy, _bgcolor);
                    _sprite->drawPixel(margin + dx, margin + imageSize - 1 - dy, _bgcolor);
                    _sprite->drawPixel(margin + imageSize - 1 - dx,
                                       margin + imageSize - 1 - dy, _bgcolor);
                }
            }
        }
        _imageDirty = false;
    }

    if (_infoDirty) {
        constexpr int16_t panelX = COVER_WIDTH;
        constexpr int16_t textWidth = PANEL_WIDTH - 10;
        const int16_t centerX = panelX + PANEL_WIDTH / 2;

        _sprite->fillRect(panelX, 0, PANEL_WIDTH, HEIGHT, TFT_BLACK);
        _sprite->drawFastVLine(panelX, 0, HEIGHT, config.theme.date);

        if (_time[0]) drawClock(*_sprite, panelX + 5, 8, textWidth, _time);

        if (_date[0]) {
            WidgetConfig dateConfig;
            memcpy_P(&dateConfig, &dateConf, sizeof(dateConfig));
            const uint8_t dateSize = dateConfig.textsize > 2 ? dateConfig.textsize - 2 : dateConfig.textsize;
            _sprite->setTextColor(config.theme.date, TFT_BLACK);
            drawWrappedText(*_sprite, _date, centerX - 6, 68, textWidth,
                            dateSize, dateSize, 2);
        }

        uint16_t artistHeight = 0;
        if (_artist[0]) {
            _sprite->setTextColor(config.theme.title1, TFT_BLACK);
            artistHeight = drawWrappedText(*_sprite, _artist, centerX, 132,
                                           textWidth, 20, 14, 3);
        }
        if (_title[0]) {
            _sprite->setTextColor(config.theme.title2, TFT_BLACK);
            const int16_t titleY = _artist[0] ? 140 + artistHeight : 164;
            drawWrappedText(*_sprite, _title, centerX, titleY,
                            textWidth, 16, 12, 3);
        }

        if (_temperature[0]) {
            constexpr int16_t weatherY = 252;
            if (_weatherData && _weatherSize) {
                _sprite->drawPng(_weatherData, _weatherSize,
                                 panelX + 5, weatherY, 64, 64);
            }
            loadFont(*_sprite, font_vlw_20);
            _sprite->setTextColor(config.theme.weatherIconTxt, TFT_BLACK);
            _sprite->setTextDatum(middle_left);
            _sprite->drawString(_temperature, panelX + 86, weatherY + 32);
            if (font_vlw_20) _sprite->unloadFont();
        }
        _infoDirty = false;
    }

    _pushSprite();
}

void CoverScreensaverWidget::_clear() {
    if (!_active) return;
    _ensureSprite();
    if (!_sprite) return;
    _sprite->fillSprite(_bgcolor);
    _pushSprite();
}

#endif
