#include "coverart.h"

#ifdef USE_LASTFM_COVER

#include <HTTPClient.h>
#include <WiFi.h>
#include <cJSON.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include "display.h"
#ifdef USE_DLNA
#    include "../dlna/dlna_http_guard.h"
#endif

namespace {

constexpr size_t API_RESPONSE_LIMIT = 96 * 1024;
constexpr size_t COVER_IMAGE_LIMIT  = 256 * 1024;
constexpr size_t MUSICBRAINZ_CANDIDATE_LIMIT = 6;
constexpr uint32_t REQUEST_SETTLE_MS = 3000;
constexpr uint32_t MUSICBRAINZ_INTERVAL_MS = 1100;
constexpr uint32_t COVER_CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t COVER_READ_TIMEOUT_MS = 8000;
constexpr char HTTP_USER_AGENT[] =
    "FusionEdge/" FW_VERSION " (https://github.com/SimZs/FusionEdge)";

uint32_t lastMusicBrainzRequestMs = 0;

struct MbidCandidates {
    char ids[MUSICBRAINZ_CANDIDATE_LIMIT][40]{};
    size_t count = 0;

    void add(const char* id) {
        if (!id || count >= MUSICBRAINZ_CANDIDATE_LIMIT) return;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(ids[i], id) == 0) return;
        }
        strlcpy(ids[count++], id, sizeof(ids[0]));
    }
};

void* cJsonPsramMalloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void cJsonPsramFree(void* pointer) {
    free(pointer);
}

class PsramBufferStream : public Stream {
  public:
    explicit PsramBufferStream(size_t capacity)
        : _capacity(capacity), _data(static_cast<uint8_t*>(ps_malloc(capacity + 1))) {}

    ~PsramBufferStream() override {
        if (_data) free(_data);
    }

    size_t write(uint8_t value) override { return write(&value, 1); }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!_data || !buffer || size > _capacity - _size) return 0;
        memcpy(_data + _size, buffer, size);
        _size += size;
        return size;
    }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    bool valid() const { return _data != nullptr; }
    size_t size() const { return _size; }

    uint8_t* release() {
        if (!_data) return nullptr;
        _data[_size] = 0;
        uint8_t* result = _data;
        _data = nullptr;
        return result;
    }

  private:
    size_t   _capacity;
    size_t   _size = 0;
    uint8_t* _data;
};

class CooperativeNetworkClient : public NetworkClient {
  public:
    int available() override {
        if (!_serviceNetwork()) return 0;
        return NetworkClient::available();
    }

    int read() override {
        if (!_serviceNetwork()) return -1;
        uint8_t value = 0;
        const int result = NetworkClient::read(&value, 1);
        if (result < 0) return result;
        return result == 0 ? -1 : value;
    }

    int read(uint8_t* buffer, size_t size) override {
        if (!_serviceNetwork()) return -1;
        return NetworkClient::read(buffer, size);
    }

    uint8_t connected() override {
        if (!_serviceNetwork()) return 0;
        return NetworkClient::connected();
    }

  private:
    bool _serviceNetwork() {
        if (coverArt.networkPaused()) {
            NetworkClient::stop();
            return false;
        }

        // HTTPClient reads response headers byte-by-byte. A malformed or very
        // long line can otherwise keep CPU0 away from IDLE0 until the task WDT
        // fires. Yield by elapsed time instead of operation count: yielding
        // every few bytes made larger HTTP headers hit the read timeout.
        const uint32_t now = millis();
        if (now - _lastYieldMs >= 20U) {
            _lastYieldMs = now;
            vTaskDelay(1);
        }
        return true;
    }

    uint32_t _lastYieldMs = 0;
};

char* trim(char* text) {
    if (!text) return text;
    while (*text && isspace(static_cast<unsigned char>(*text))) ++text;
    char* end = text + strlen(text);
    while (end > text && isspace(static_cast<unsigned char>(end[-1]))) --end;
    *end = '\0';
    return text;
}

void normalizeKeyPart(const char* input, char* output, size_t outputSize) {
    if (!output || outputSize == 0) return;
    output[0] = '\0';
    if (!input) return;

    size_t out = 0;
    bool pendingSpace = false;
    while (*input && out + 1 < outputSize) {
        const unsigned char ch = static_cast<unsigned char>(*input++);
        if (isspace(ch)) {
            pendingSpace = out > 0;
            continue;
        }
        if (pendingSpace && out + 1 < outputSize) output[out++] = ' ';
        pendingSpace = false;
        output[out++] = ch < 0x80 ? static_cast<char>(tolower(ch)) : static_cast<char>(ch);
    }
    output[out] = '\0';
}

bool splitCombinedTitle(const char* combined, char* artist, size_t artistSize,
                        char* title, size_t titleSize) {
    if (!combined || !artist || !title || artistSize == 0 || titleSize == 0) return false;
    const char* separator = strstr(combined, " - ");
    if (!separator) return false;

    const size_t artistLength = static_cast<size_t>(separator - combined);
    if (artistLength == 0 || artistLength >= artistSize) return false;
    memcpy(artist, combined, artistLength);
    artist[artistLength] = '\0';
    strlcpy(title, separator + 3, titleSize);

    char* cleanArtist = trim(artist);
    char* cleanTitle  = trim(title);
    if (cleanArtist != artist) memmove(artist, cleanArtist, strlen(cleanArtist) + 1);
    if (cleanTitle != title) memmove(title, cleanTitle, strlen(cleanTitle) + 1);
    return artist[0] != '\0' && title[0] != '\0';
}

void makeKey(const char* artist, const char* title, char* key, size_t keySize) {
    char normalizedArtist[128];
    char normalizedTitle[192];
    normalizeKeyPart(artist, normalizedArtist, sizeof(normalizedArtist));
    normalizeKeyPart(title, normalizedTitle, sizeof(normalizedTitle));
    snprintf(key, keySize, "%s\x1f%s", normalizedArtist, normalizedTitle);
}

char* findAsciiCaseInsensitive(char* text, const char* needle) {
    if (!text || !needle || *needle == '\0') return text;
    const size_t needleLength = strlen(needle);
    for (char* start = text; *start; ++start) {
        size_t index = 0;
        while (index < needleLength && start[index] &&
               tolower(static_cast<unsigned char>(start[index])) ==
                   tolower(static_cast<unsigned char>(needle[index]))) {
            ++index;
        }
        if (index == needleLength) return start;
    }
    return nullptr;
}

void makeMusicBrainzSearchPart(const char* input, char* output, size_t outputSize,
                               bool removeFeaturedArtist) {
    if (!output || outputSize == 0) return;
    strlcpy(output, input ? input : "", outputSize);

    if (removeFeaturedArtist) {
        static const char* const markers[] = {
            " feat. ", " feat ", " ft. ", " ft ", " featuring ", " (feat.", " (feat "
        };
        for (const char* marker : markers) {
            char* position = findAsciiCaseInsensitive(output, marker);
            if (position) {
                *position = '\0';
                break;
            }
        }
    }

    for (char* cursor = output; *cursor; ++cursor) {
        if (*cursor == '"') *cursor = ' ';
    }
    char* clean = trim(output);
    if (clean != output) memmove(output, clean, strlen(clean) + 1);
}

bool containsVideoNoise(char* text) {
    static const char* const words[] = {
        "lyric", "official", "video", "audio", "full hd", "4k",
        "visualizer", "karaoke", "sing-along", "sing along", "remix"
    };
    for (const char* word : words) {
        if (findAsciiCaseInsensitive(text, word)) return true;
    }
    return false;
}

void canonicalizeBluetoothTrackTitle(char* text) {
    if (!text || text[0] == '\0') return;

    // Remove bracketed video descriptors while preserving genuine title
    // qualifiers such as artist names when they do not contain a noise word.
    for (char* cursor = text; *cursor;) {
        char* openRound = strchr(cursor, '(');
        char* openSquare = strchr(cursor, '[');
        char* open = !openRound ? openSquare
                   : !openSquare ? openRound
                   : (openRound < openSquare ? openRound : openSquare);
        if (!open) break;
        const char closing = *open == '(' ? ')' : ']';
        char* close = strchr(open + 1, closing);
        if (!close) break;

        const char saved = *close;
        *close = '\0';
        const bool remove = containsVideoNoise(open + 1);
        *close = saved;
        if (remove) {
            memmove(open, close + 1, strlen(close + 1) + 1);
            cursor = open;
        } else {
            cursor = close + 1;
        }
    }

    static const char* const suffixes[] = {
        " with lyrics", " lyrics", " lyric video", " official video",
        " official audio", " live video", " music video", " full video",
        " full hd", " visualizer", " karaoke", " sing-along", " sing along"
    };
    char* firstSuffix = nullptr;
    for (const char* suffix : suffixes) {
        char* found = findAsciiCaseInsensitive(text, suffix);
        if (found && (!firstSuffix || found < firstSuffix)) firstSuffix = found;
    }
    const bool removedSuffix = firstSuffix != nullptr;
    if (firstSuffix) *firstSuffix = '\0';

    char* clean = trim(text);
    if (clean != text) memmove(text, clean, strlen(clean) + 1);

    // A year immediately before a removed video suffix is also only a video
    // qualifier. Keep genuine titles ending in a year unchanged.
    char* end = text + strlen(text);
    char* lastSpace = strrchr(text, ' ');
    if (removedSuffix && lastSpace && end - lastSpace == 5) {
        bool year = true;
        for (char* p = lastSpace + 1; p < end; ++p) {
            if (!isdigit(static_cast<unsigned char>(*p))) { year = false; break; }
        }
        if (year) *lastSpace = '\0';
    }

    static const char* const unavailable[] = {
        "not provided", "unknown", "n/a", "no title", "untitled"
    };
    for (const char* value : unavailable) {
        if (strcasecmp(text, value) == 0) {
            text[0] = '\0';
            return;
        }
    }
}

void makeBluetoothTrackIdentity(const char* input, char* output, size_t outputSize) {
    if (!output || outputSize == 0) return;
    strlcpy(output, input ? input : "", outputSize);

    // YouTube commonly appends the channel, genre or video description after
    // a pipe. It is not part of the recording title.
    char* pipe = strstr(output, " | ");
    if (!pipe) pipe = strchr(output, '|');
    if (pipe) *pipe = '\0';

    char* clean = trim(output);
    if (clean != output) memmove(output, clean, strlen(clean) + 1);
    canonicalizeBluetoothTrackTitle(output);
}

void makeBluetoothTrackSearch(const char* input, char* output, size_t outputSize) {
    makeBluetoothTrackIdentity(input, output, outputSize);
    if (!output || output[0] == '\0') return;

    // If the video title itself is "Artist - Track" (ASCII, en dash or em
    // dash), the QCC ARTS field is usually only the YouTube channel. Search
    // using the right-hand track portion instead.
    static const char* const separators[] = {
        " - ", " \xE2\x80\x93 ", " \xE2\x80\x94 "
    };
    char* splitAt = nullptr;
    size_t separatorLength = 0;
    for (const char* separator : separators) {
        char* found = strstr(output, separator);
        if (found && (!splitAt || found < splitAt)) {
            splitAt = found;
            separatorLength = strlen(separator);
        }
    }
    if (splitAt && splitAt[separatorLength] != '\0') {
        memmove(output, splitAt + separatorLength,
                strlen(splitAt + separatorLength) + 1);
    }

    char* clean = trim(output);
    if (clean != output) memmove(output, clean, strlen(clean) + 1);
}

bool makeLookupKey(const char* artist, const char* title, bool bluetoothTitleMode,
                   char* key, size_t keySize) {
    if (!bluetoothTitleMode) {
        makeKey(artist, title, key, keySize);
        return key[0] != '\0';
    }

    // QCC's ARTS field may change from one YouTube channel name to another
    // while TITL still describes the same playing item. Use only the cleaned
    // title as the BT identity so a late metadata update cannot restart an
    // already successful cover lookup.
    char trackIdentity[192];
    makeBluetoothTrackIdentity(title, trackIdentity, sizeof(trackIdentity));
    if (trackIdentity[0] == '\0') {
        key[0] = '\0';
        return false;
    }
    makeKey("", trackIdentity, key, keySize);
    return true;
}

String urlEncode(const char* value) {
    static const char hex[] = "0123456789ABCDEF";
    String result;
    if (!value) return result;
    result.reserve(strlen(value) * 3 + 1);
    while (*value) {
        const uint8_t ch = static_cast<uint8_t>(*value++);
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            result += static_cast<char>(ch);
        } else {
            result += '%';
            result += hex[ch >> 4];
            result += hex[ch & 0x0F];
        }
    }
    return result;
}

String requestHost(const String& url) {
    const int schemeEnd = url.indexOf("://");
    const int hostStart = schemeEnd >= 0 ? schemeEnd + 3 : 0;
    int hostEnd = url.indexOf('/', hostStart);
    if (hostEnd < 0) hostEnd = url.length();
    return url.substring(hostStart, hostEnd);
}

bool httpGetToPsram(const String& url, size_t limit, uint8_t*& data, size_t& size,
                    uint8_t retries = 1) {
    data = nullptr;
    size = 0;
    if (coverArt.networkPaused()) return false;
    if (!url.startsWith("http://")) {
        log_w("##[COVER]# non-HTTP URL rejected to preserve audio TLS memory");
        return false;
    }

    for (uint8_t attempt = 0; attempt <= retries; ++attempt) {
        if (coverArt.networkPaused()) return false;
        bool retry = false;
        {
#ifdef USE_DLNA
            // DLNA Browse and cover lookup both use sizeable HTTP responses.
            // Keep them off the network heap at the same time.
            DlnaHttpGuard dlnaHttpLock;
#endif
            if (coverArt.networkPaused()) return false;
            CooperativeNetworkClient plainClient;
            HTTPClient http;
            http.setConnectTimeout(COVER_CONNECT_TIMEOUT_MS);
            http.setTimeout(COVER_READ_TIMEOUT_MS);
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            http.setRedirectLimit(4);
            http.setUserAgent(HTTP_USER_AGENT);
            if (!http.begin(plainClient, url)) return false;

            // HTTPClient can spend several seconds parsing a slow response or
            // redirect chain. Let IDLE0 service the task watchdog first.
            vTaskDelay(1);
            const int status = http.GET();
            vTaskDelay(1);
            if (coverArt.networkPaused()) {
                http.end();
                return false;
            }
            if (status != HTTP_CODE_OK) {
                retry = attempt < retries &&
                        (status == 429 || status == HTTP_CODE_SERVICE_UNAVAILABLE);
                if (retry) {
                    log_w("##[COVER]# HTTP request failed host=%s status=%d, retrying once",
                          requestHost(url).c_str(), status);
                } else if (status == HTTP_CODE_NOT_FOUND) {
                    log_d("##[COVER]# resource not found: %d", status);
                } else {
                    log_w("##[COVER]# HTTP request failed host=%s status=%d",
                          requestHost(url).c_str(), status);
                }
                http.end();
            } else {
                const int contentLength = http.getSize();
                if (contentLength > 0 && static_cast<size_t>(contentLength) > limit) {
                    log_w("##[COVER]# response too large: %d bytes", contentLength);
                    http.end();
                    return false;
                }

                PsramBufferStream sink(limit);
                if (!sink.valid()) {
                    log_w("##[COVER]# PSRAM allocation failed (%u bytes)", static_cast<unsigned>(limit));
                    http.end();
                    return false;
                }
                const int written = http.writeToStream(&sink);
                http.end();
                if (coverArt.networkPaused()) return false;
                if (written < 0 || sink.size() == 0) {
                    log_w("##[COVER]# response read failed: %d", written);
                    return false;
                }

                size = sink.size();
                data = sink.release();
                return data != nullptr;
            }
        }
        if (!retry) return false;
        if (coverArt.networkPaused()) return false;
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    return false;
}

bool httpGetRedirectLocation(const String& url, String& location) {
    location = "";
    if (coverArt.networkPaused()) return false;
    if (!url.startsWith("http://")) return false;

#ifdef USE_DLNA
    DlnaHttpGuard dlnaHttpLock;
#endif
    if (coverArt.networkPaused()) return false;
    CooperativeNetworkClient client;
    HTTPClient http;
    http.setConnectTimeout(COVER_CONNECT_TIMEOUT_MS);
    http.setTimeout(COVER_READ_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(HTTP_USER_AGENT);
    if (!http.begin(client, url)) return false;

    vTaskDelay(1);
    const int status = http.GET();
    vTaskDelay(1);
    if (coverArt.networkPaused()) {
        http.end();
        return false;
    }
    const bool redirect = status == HTTP_CODE_MOVED_PERMANENTLY ||
                          status == HTTP_CODE_FOUND || status == HTTP_CODE_SEE_OTHER ||
                          status == HTTP_CODE_TEMPORARY_REDIRECT || status == 308;
    if (redirect) location = http.getLocation();
    http.end();

    if (!redirect || !location.startsWith("http://")) {
        if (status == HTTP_CODE_NOT_FOUND) {
            log_d("##[COVER]# CAA has no cover: %d", status);
        } else {
            log_w("##[COVER]# CAA redirect failed host=%s status=%d",
                  requestHost(url).c_str(), status);
        }
        location = "";
        return false;
    }
    return true;
}

int lastFmImageRank(const char* size) {
    if (!size) return 0;
    if (strcmp(size, "mega") == 0) return 5;
    if (strcmp(size, "extralarge") == 0) return 4;
    if (strcmp(size, "large") == 0) return 3;
    if (strcmp(size, "medium") == 0) return 2;
    if (strcmp(size, "small") == 0) return 1;
    return 0;
}

bool isLastFmPlaceholder(const char* url) {
    if (!url) return true;
    return strstr(url, "2a96cbd8b46e442fc41c2b86b821562f") != nullptr ||
           strstr(url, "4128a6eb29f94943c9d206c08e625904") != nullptr;
}

bool extractLastFmAlbum(const uint8_t* jsonData, size_t jsonSize,
                        char* mbid, size_t mbidSize,
                        char* imageUrl, size_t imageUrlSize) {
    mbid[0] = '\0';
    imageUrl[0] = '\0';
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(jsonData), jsonSize);
    if (!root) return false;

    cJSON* track = cJSON_GetObjectItemCaseSensitive(root, "track");
    cJSON* album = cJSON_IsObject(track) ? cJSON_GetObjectItemCaseSensitive(track, "album") : nullptr;
    cJSON* albumMbid = cJSON_IsObject(album) ? cJSON_GetObjectItemCaseSensitive(album, "mbid") : nullptr;
    if (cJSON_IsString(albumMbid) && albumMbid->valuestring) {
        strlcpy(mbid, albumMbid->valuestring, mbidSize);
    }

    cJSON* images = cJSON_IsObject(album)
                        ? cJSON_GetObjectItemCaseSensitive(album, "image")
                        : nullptr;
    int bestRank = 0;
    cJSON* image = nullptr;
    cJSON_ArrayForEach(image, images) {
        cJSON* text = cJSON_GetObjectItemCaseSensitive(image, "#text");
        cJSON* size = cJSON_GetObjectItemCaseSensitive(image, "size");
        if (!cJSON_IsString(text) || !text->valuestring || text->valuestring[0] == '\0' ||
            isLastFmPlaceholder(text->valuestring)) {
            continue;
        }
        const int rank = cJSON_IsString(size) ? lastFmImageRank(size->valuestring) : 1;
        if (rank < bestRank) continue;

        const char* rawUrl = text->valuestring;
        if (strncmp(rawUrl, "https://", 8) == 0) {
            snprintf(imageUrl, imageUrlSize, "http://%s", rawUrl + 8);
        } else if (strncmp(rawUrl, "http://", 7) == 0) {
            strlcpy(imageUrl, rawUrl, imageUrlSize);
        } else {
            continue;
        }
        bestRank = rank;
    }
    cJSON_Delete(root);

    if (strlen(mbid) != 36) {
        mbid[0] = '\0';
    } else {
        for (size_t i = 0; i < 36; ++i) {
            const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
            if ((separator && mbid[i] != '-') ||
                (!separator && !isxdigit(static_cast<unsigned char>(mbid[i])))) {
                mbid[0] = '\0';
                break;
            }
        }
    }
    return mbid[0] != '\0' || imageUrl[0] != '\0';
}

bool artistMatches(const char* expected, const char* candidate) {
    char normalizedExpected[128];
    char normalizedCandidate[128];
    normalizeKeyPart(expected, normalizedExpected, sizeof(normalizedExpected));
    normalizeKeyPart(candidate, normalizedCandidate, sizeof(normalizedCandidate));
    return normalizedExpected[0] != '\0' &&
           strcmp(normalizedExpected, normalizedCandidate) == 0;
}

bool extractLastFmTrackMatch(const uint8_t* jsonData, size_t jsonSize,
                              const char* requiredArtist,
                              char* artist, size_t artistSize,
                              char* title, size_t titleSize) {
    artist[0] = '\0';
    title[0] = '\0';
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(jsonData), jsonSize);
    if (!root) return false;

    cJSON* results = cJSON_GetObjectItemCaseSensitive(root, "results");
    cJSON* matches = cJSON_IsObject(results)
                         ? cJSON_GetObjectItemCaseSensitive(results, "trackmatches")
                         : nullptr;
    cJSON* tracks = cJSON_IsObject(matches)
                        ? cJSON_GetObjectItemCaseSensitive(matches, "track")
                        : nullptr;
    const int trackCount = cJSON_IsArray(tracks) ? cJSON_GetArraySize(tracks) : 1;
    for (int index = 0; index < trackCount; ++index) {
        cJSON* track = cJSON_IsArray(tracks) ? cJSON_GetArrayItem(tracks, index) : tracks;
        cJSON* matchArtist = cJSON_IsObject(track)
                                 ? cJSON_GetObjectItemCaseSensitive(track, "artist")
                                 : nullptr;
        cJSON* matchTitle = cJSON_IsObject(track)
                                ? cJSON_GetObjectItemCaseSensitive(track, "name")
                                : nullptr;
        if (!cJSON_IsString(matchArtist) || !matchArtist->valuestring ||
            !cJSON_IsString(matchTitle) || !matchTitle->valuestring) {
            continue;
        }
        if (requiredArtist && !artistMatches(requiredArtist, matchArtist->valuestring)) {
            continue;
        }
        strlcpy(artist, matchArtist->valuestring, artistSize);
        strlcpy(title, matchTitle->valuestring, titleSize);
        break;
    }
    cJSON_Delete(root);
    return artist[0] != '\0' && title[0] != '\0';
}

bool findLastFmTrackByTitle(const char* rawTitle, const char* requiredArtist,
                             bool bluetoothTitleMode, char* artist, size_t artistSize,
                             char* title, size_t titleSize) {
    char searchTitle[192];
    if (bluetoothTitleMode) {
        makeBluetoothTrackSearch(rawTitle, searchTitle, sizeof(searchTitle));
    } else {
        strlcpy(searchTitle, rawTitle ? rawTitle : "", sizeof(searchTitle));
        char* clean = trim(searchTitle);
        if (clean != searchTitle) memmove(searchTitle, clean, strlen(clean) + 1);
    }
    if (searchTitle[0] == '\0') return false;

    String url = "http://ws.audioscrobbler.com/2.0/?method=track.search&format=json&limit=3&api_key=";
    url += LASTFM_API_KEY;
    url += "&track=";
    url += urlEncode(searchTitle);

    uint8_t* jsonData = nullptr;
    size_t jsonSize = 0;
    if (!httpGetToPsram(url, API_RESPONSE_LIMIT, jsonData, jsonSize)) return false;
    const bool found = extractLastFmTrackMatch(jsonData, jsonSize, requiredArtist,
                                                artist, artistSize, title, titleSize);
    free(jsonData);
    if (found && bluetoothTitleMode) canonicalizeBluetoothTrackTitle(title);
    if (found && title[0] != '\0') {
        log_d("##[COVER]# title fallback: '%s' -> '%s' - '%s'",
              searchTitle, artist, title);
        return true;
    }
    return false;
}

bool isValidMbid(const char* mbid) {
    if (!mbid || strlen(mbid) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        if ((separator && mbid[i] != '-') ||
            (!separator && !isxdigit(static_cast<unsigned char>(mbid[i])))) {
            return false;
        }
    }
    return true;
}

bool extractReleaseGroupMbids(const uint8_t* jsonData, size_t jsonSize,
                              MbidCandidates& candidates) {
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(jsonData), jsonSize);
    if (!root) return false;

    cJSON* recordings = cJSON_GetObjectItemCaseSensitive(root, "recordings");
    cJSON* recording = nullptr;
    cJSON_ArrayForEach(recording, recordings) {
        cJSON* score = cJSON_GetObjectItemCaseSensitive(recording, "score");
        if (!cJSON_IsNumber(score) || score->valueint < 90) continue;

        cJSON* releases = cJSON_GetObjectItemCaseSensitive(recording, "releases");
        cJSON* release = nullptr;
        cJSON_ArrayForEach(release, releases) {
            cJSON* releaseGroup = cJSON_GetObjectItemCaseSensitive(release, "release-group");
            cJSON* id = cJSON_IsObject(releaseGroup)
                            ? cJSON_GetObjectItemCaseSensitive(releaseGroup, "id")
                            : nullptr;
            if (cJSON_IsString(id) && isValidMbid(id->valuestring)) {
                candidates.add(id->valuestring);
                if (candidates.count >= MUSICBRAINZ_CANDIDATE_LIMIT) break;
            }
        }
        if (candidates.count >= MUSICBRAINZ_CANDIDATE_LIMIT) break;
    }
    cJSON_Delete(root);
    return candidates.count > 0;
}

bool findMusicBrainzReleaseGroup(const char* artist, const char* title,
                                 MbidCandidates& candidates) {
    if (coverArt.networkPaused()) return false;
    char searchArtist[128];
    char searchTitle[192];
    makeMusicBrainzSearchPart(artist, searchArtist, sizeof(searchArtist), false);
    makeMusicBrainzSearchPart(title, searchTitle, sizeof(searchTitle), true);
    if (searchArtist[0] == '\0' || searchTitle[0] == '\0') return false;

    const uint32_t now = millis();
    const uint32_t elapsed = now - lastMusicBrainzRequestMs;
    if (lastMusicBrainzRequestMs != 0 && elapsed < MUSICBRAINZ_INTERVAL_MS) {
        uint32_t waitMs = MUSICBRAINZ_INTERVAL_MS - elapsed;
        while (waitMs > 0 && !coverArt.networkPaused()) {
            const uint32_t sliceMs = waitMs > 50 ? 50 : waitMs;
            vTaskDelay(pdMS_TO_TICKS(sliceMs));
            waitMs -= sliceMs;
        }
    }
    if (coverArt.networkPaused()) return false;
    lastMusicBrainzRequestMs = millis();

    String query = "recording:\"";
    query += searchTitle;
    query += "\" AND artist:\"";
    query += searchArtist;
    query += "\"";

    String url = "http://musicbrainz.org/ws/2/recording/?fmt=json&limit=5&query=";
    url += urlEncode(query.c_str());

    uint8_t* jsonData = nullptr;
    size_t jsonSize = 0;
    if (!httpGetToPsram(url, API_RESPONSE_LIMIT, jsonData, jsonSize)) return false;
    const bool found = extractReleaseGroupMbids(jsonData, jsonSize, candidates);
    free(jsonData);
    if (found) {
        log_d("##[COVER]# MusicBrainz release-group candidates: %u",
              static_cast<unsigned>(candidates.count));
    }
    return found;
}

bool detectImage(const uint8_t* data, size_t size, bool& jpeg) {
    if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
        data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        jpeg = false;
        return true;
    }
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        jpeg = true;
        return true;
    }
    return false;
}

bool parseArchiveLocation(const String& location, String& item, String& filename) {
    constexpr char marker[] = "/download/";
    const int markerAt = location.indexOf(marker);
    if (markerAt < 0) return false;

    const int itemStart = markerAt + static_cast<int>(sizeof(marker) - 1);
    const int itemEnd = location.indexOf('/', itemStart);
    if (itemEnd <= itemStart) return false;

    int filenameEnd = location.indexOf('?', itemEnd + 1);
    if (filenameEnd < 0) filenameEnd = location.length();
    if (filenameEnd <= itemEnd + 1) return false;

    item = location.substring(itemStart, itemEnd);
    filename = location.substring(itemEnd + 1, filenameEnd);
    return item.startsWith("mbid-") && filename.length() > 0;
}

bool extractArchiveServers(const uint8_t* jsonData, size_t jsonSize,
                           String& primaryUrl, String& secondaryUrl,
                           const String& filename) {
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(jsonData), jsonSize);
    if (!root) return false;

    cJSON* d1 = cJSON_GetObjectItemCaseSensitive(root, "d1");
    cJSON* d2 = cJSON_GetObjectItemCaseSensitive(root, "d2");
    cJSON* directory = cJSON_GetObjectItemCaseSensitive(root, "dir");
    const char* dir = cJSON_IsString(directory) ? directory->valuestring : nullptr;

    if (dir && cJSON_IsString(d1) && d1->valuestring) {
        primaryUrl = "http://";
        primaryUrl += d1->valuestring;
        primaryUrl += dir;
        primaryUrl += '/';
        primaryUrl += filename;
    }
    if (dir && cJSON_IsString(d2) && d2->valuestring) {
        secondaryUrl = "http://";
        secondaryUrl += d2->valuestring;
        secondaryUrl += dir;
        secondaryUrl += '/';
        secondaryUrl += filename;
    }
    cJSON_Delete(root);
    return primaryUrl.length() > 0;
}

bool resolveArchiveUrls(const char* mbid, bool releaseGroup,
                        String& primaryUrl, String& secondaryUrl) {
    if (coverArt.networkPaused()) return false;
    String caaUrl = "http://coverartarchive.org/";
    caaUrl += releaseGroup ? "release-group/" : "release/";
    caaUrl += mbid;
    caaUrl += "/front-250";

    String location;
    if (!httpGetRedirectLocation(caaUrl, location)) return false;
    if (coverArt.networkPaused()) return false;

    String item;
    String filename;
    if (!parseArchiveLocation(location, item, filename)) {
        log_w("##[COVER]# invalid CAA archive location");
        return false;
    }

    String metadataUrl = "http://archive.org/metadata/";
    metadataUrl += item;
    uint8_t* jsonData = nullptr;
    size_t jsonSize = 0;
    if (!httpGetToPsram(metadataUrl, API_RESPONSE_LIMIT, jsonData, jsonSize)) return false;
    const bool found = extractArchiveServers(jsonData, jsonSize, primaryUrl, secondaryUrl,
                                             filename);
    free(jsonData);
    return found;
}

bool fetchCoverArchive(const char* mbid, bool releaseGroup, uint8_t*& imageData,
                       size_t& imageSize, bool& jpeg) {
    if (coverArt.networkPaused()) return false;
    String primaryUrl;
    String secondaryUrl;
    if (!resolveArchiveUrls(mbid, releaseGroup, primaryUrl, secondaryUrl)) return false;

    if (!httpGetToPsram(primaryUrl, COVER_IMAGE_LIMIT, imageData, imageSize) &&
        (secondaryUrl.length() == 0 ||
         !httpGetToPsram(secondaryUrl, COVER_IMAGE_LIMIT, imageData, imageSize))) {
        return false;
    }
    if (!detectImage(imageData, imageSize, jpeg)) {
        free(imageData);
        imageData = nullptr;
        imageSize = 0;
        return false;
    }

    // Keep only the received bytes instead of retaining the complete download limit.
    uint8_t* compact = static_cast<uint8_t*>(ps_malloc(imageSize));
    if (compact) {
        memcpy(compact, imageData, imageSize);
        free(imageData);
        imageData = compact;
    }
    return true;
}

bool fetchExactCover(const char* artist, const char* title, uint8_t*& imageData,
                     size_t& imageSize, bool& jpeg) {
    if (coverArt.networkPaused()) return false;
    String apiUrl = "http://ws.audioscrobbler.com/2.0/?method=track.getInfo&autocorrect=1&format=json&api_key=";
    apiUrl += LASTFM_API_KEY;
    apiUrl += "&artist=";
    apiUrl += urlEncode(artist);
    apiUrl += "&track=";
    apiUrl += urlEncode(title);

    uint8_t* jsonData = nullptr;
    size_t jsonSize = 0;
    char albumMbid[40];
    char lastFmImageUrl[512];
    bool haveLastFmMbid = false;
    if (httpGetToPsram(apiUrl, API_RESPONSE_LIMIT, jsonData, jsonSize)) {
        extractLastFmAlbum(jsonData, jsonSize, albumMbid, sizeof(albumMbid),
                           lastFmImageUrl, sizeof(lastFmImageUrl));
        haveLastFmMbid = albumMbid[0] != '\0';
        free(jsonData);
        jsonData = nullptr;

        if (lastFmImageUrl[0] != '\0' &&
            httpGetToPsram(lastFmImageUrl, COVER_IMAGE_LIMIT,
                           imageData, imageSize, 0)) {
            if (detectImage(imageData, imageSize, jpeg)) {
                uint8_t* compact = static_cast<uint8_t*>(ps_malloc(imageSize));
                if (compact) {
                    memcpy(compact, imageData, imageSize);
                    free(imageData);
                    imageData = compact;
                }
                log_d("##[COVER]# using Last.fm album image");
                return true;
            }
            free(imageData);
            imageData = nullptr;
            imageSize = 0;
        }
        if (coverArt.networkPaused()) return false;

        if (haveLastFmMbid &&
            fetchCoverArchive(albumMbid, false, imageData, imageSize, jpeg)) {
            return true;
        }
        if (coverArt.networkPaused()) return false;
        if (!haveLastFmMbid) {
            log_d("##[COVER]# Last.fm returned no album MBID, trying MusicBrainz");
        } else {
            log_d("##[COVER]# no release cover, trying MusicBrainz release-group");
        }
    } else {
        log_d("##[COVER]# Last.fm unavailable, trying MusicBrainz");
    }

    MbidCandidates candidates;
    if (coverArt.networkPaused()) return false;
    if (!findMusicBrainzReleaseGroup(artist, title, candidates)) {
        return false;
    }
    for (size_t i = 0; i < candidates.count; ++i) {
        if (coverArt.networkPaused()) return false;
        log_d("##[COVER]# trying release-group %u/%u: %s",
              static_cast<unsigned>(i + 1), static_cast<unsigned>(candidates.count),
              candidates.ids[i]);
        if (fetchCoverArchive(candidates.ids[i], true, imageData, imageSize, jpeg)) {
            return true;
        }
    }
    return false;
}

bool fetchTitleFallback(const char* artist, const char* title, bool bluetoothTitleMode,
                        uint8_t*& imageData, size_t& imageSize, bool& jpeg) {
    char matchedArtist[128];
    char matchedTitle[192];
    const char* requiredArtist = bluetoothTitleMode ? nullptr : artist;
    if (!findLastFmTrackByTitle(title, requiredArtist, bluetoothTitleMode,
                                matchedArtist, sizeof(matchedArtist),
                                matchedTitle, sizeof(matchedTitle))) {
        log_d("##[COVER]# title fallback found no safe track match");
        return false;
    }
    if (coverArt.networkPaused()) return false;

    char originalKey[322];
    char matchedKey[322];
    makeKey(artist, title, originalKey, sizeof(originalKey));
    makeKey(matchedArtist, matchedTitle, matchedKey, sizeof(matchedKey));
    if (strcmp(originalKey, matchedKey) == 0) return false;

    return fetchExactCover(matchedArtist, matchedTitle, imageData, imageSize, jpeg);
}

bool fetchCover(const char* artist, const char* title, bool bluetoothTitleMode,
                bool localFileMode, uint8_t*& imageData, size_t& imageSize, bool& jpeg) {
    if (fetchExactCover(artist, title, imageData, imageSize, jpeg)) return true;
    if (coverArt.networkPaused()) return false;
    if (fetchTitleFallback(artist, title, bluetoothTitleMode,
                           imageData, imageSize, jpeg)) {
        return true;
    }
    if (coverArt.networkPaused() || !localFileMode) return false;

    // Local ID3 titles often contain a mix/remix qualifier that is absent
    // from public music databases. Keep the displayed title intact and use
    // the cleaned form only as a final cover-search fallback.
    char cleanTitle[192];
    strlcpy(cleanTitle, title ? title : "", sizeof(cleanTitle));
    canonicalizeBluetoothTrackTitle(cleanTitle);
    if (cleanTitle[0] == '\0' || strcmp(cleanTitle, title) == 0) return false;

    log_i("##[SDCOVER]# retry with cleaned title: '%s' - '%s'",
          artist, cleanTitle);
    if (fetchExactCover(artist, cleanTitle, imageData, imageSize, jpeg)) return true;
    if (coverArt.networkPaused()) return false;
    return fetchTitleFallback(artist, cleanTitle, false,
                              imageData, imageSize, jpeg);
}

} // namespace

CoverArtManager coverArt;

void CoverArtManager::begin() {
    if (_queue || _task) return;
    const size_t internalLargestBefore =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!psramFound()) {
        log_e("##[COVER]# PSRAM is required");
        return;
    }

    cJSON_Hooks hooks{};
    hooks.malloc_fn = cJsonPsramMalloc;
    hooks.free_fn = cJsonPsramFree;
    cJSON_InitHooks(&hooks);

    constexpr UBaseType_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    _mutex = xSemaphoreCreateMutexWithCaps(psramCaps);
    if (!_mutex) {
        log_e("##[COVER]# mutex allocation failed");
        return;
    }
    _queue = xQueueCreateWithCaps(1, sizeof(Request), psramCaps);
    if (!_queue) {
        log_e("##[COVER]# request queue allocation failed");
        vSemaphoreDeleteWithCaps(_mutex);
        _mutex = nullptr;
        return;
    }
    constexpr BaseType_t targetCore = 0;
    constexpr UBaseType_t taskPriority = 0;
    if (xTaskCreatePinnedToCoreWithCaps(
            _taskEntry, "cover_art", 8192, this, taskPriority, &_task, targetCore,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        log_e("##[COVER]# task creation failed");
        vQueueDeleteWithCaps(_queue);
        _queue = nullptr;
        vSemaphoreDeleteWithCaps(_mutex);
        _mutex = nullptr;
        _task = nullptr;
        return;
    }
    log_i("##[COVER]# task ready (PSRAM stack/queue/JSON, HTTP transport, internal largest=%u->%u)",
          static_cast<unsigned>(internalLargestBefore),
          static_cast<unsigned>(
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void CoverArtManager::requestCombined(const char* combinedTitle, bool bluetoothTitleMode,
                                      bool localFileMode) {
    if (!_queue || !combinedTitle) return;

    Request request{};
    if (!splitCombinedTitle(combinedTitle, request.artist, sizeof(request.artist),
                            request.title, sizeof(request.title))) {
        if (localFileMode && combinedTitle[0] != '\0') {
            log_i("##[SDCOVER]# skipped, Artist - Title metadata unavailable: '%s'",
                  combinedTitle);
        }
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (_currentKey[0] != '\0') {
            _currentKey[0] = '\0';
            _currentBluetoothTitleMode = false;
            if (++_currentGeneration == 0) ++_currentGeneration;
            if (_readyData) free(_readyData);
            _readyData = nullptr;
            _readySize = 0;
            _readyGeneration = 0;
            _readyKey[0] = '\0';
        }
        xSemaphoreGive(_mutex);
        return;
    }
    request.bluetoothTitleMode = bluetoothTitleMode;
    request.localFileMode = localFileMode;
    if (!makeLookupKey(request.artist, request.title, request.bluetoothTitleMode,
                       request.key, sizeof(request.key))) {
        // Values such as "Not Provided" are temporary BT metadata, not a new
        // track. Keep the current cover and do not enqueue a meaningless job.
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (strcmp(_currentKey, request.key) == 0 &&
        _currentBluetoothTitleMode == request.bluetoothTitleMode) {
        xSemaphoreGive(_mutex);
        return;
    }
    strlcpy(_currentKey, request.key, sizeof(_currentKey));
    _currentBluetoothTitleMode = request.bluetoothTitleMode;
    request.generation = ++_currentGeneration;
    if (_currentGeneration == 0) request.generation = ++_currentGeneration;
    if (_readyData) free(_readyData);
    _readyData = nullptr;
    _readySize = 0;
    _readyGeneration = 0;
    _readyKey[0] = '\0';
    xSemaphoreGive(_mutex);

    xQueueOverwrite(_queue, &request);
    if (localFileMode) {
        log_i("##[SDCOVER]# queued: '%s' - '%s'", request.artist, request.title);
    }
}

bool CoverArtManager::pauseNetwork(uint32_t timeoutMs) {
    if (!_mutex) return true;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _networkPaused = true;
    bool active = _networkActive;
    xSemaphoreGive(_mutex);

    const uint32_t started = millis();
    while (active && millis() - started < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
        xSemaphoreTake(_mutex, portMAX_DELAY);
        active = _networkActive;
        xSemaphoreGive(_mutex);
    }
    if (active) {
        log_w("##[COVER]# network pause timed out after %lu ms",
              static_cast<unsigned long>(millis() - started));
        return false;
    }
    const uint32_t waited = millis() - started;
    if (waited >= 10) {
        log_d("##[COVER]# network paused for audio connect, waited=%lu ms",
              static_cast<unsigned long>(waited));
    }
    return true;
}

void CoverArtManager::resumeNetwork() {
    if (!_mutex) return;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _networkPaused = false;
    xSemaphoreGive(_mutex);
}

bool CoverArtManager::networkPaused() {
    if (!_mutex) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool paused = _networkPaused;
    xSemaphoreGive(_mutex);
    return paused;
}

bool CoverArtManager::copyReadyFor(const char* combinedTitle, bool bluetoothTitleMode,
                                    uint8_t*& data, size_t& size, bool& jpeg,
                                   uint32_t& generation) {
    data = nullptr;
    size = 0;
    generation = 0;
    if (!_mutex) return false;

    char artist[ARTIST_LEN];
    char title[TITLE_LEN];
    char key[KEY_LEN];
    if (!splitCombinedTitle(combinedTitle, artist, sizeof(artist), title, sizeof(title))) return false;
    if (!makeLookupKey(artist, title, bluetoothTitleMode, key, sizeof(key))) return false;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool matches = _readyData && _readySize > 0 && strcmp(_readyKey, key) == 0;
    if (matches) {
        data = static_cast<uint8_t*>(ps_malloc(_readySize));
        if (data) {
            memcpy(data, _readyData, _readySize);
            size = _readySize;
            jpeg = _readyJpeg;
            generation = _readyGeneration;
        }
    }
    xSemaphoreGive(_mutex);
    return data != nullptr;
}

void CoverArtManager::_taskEntry(void* parameter) {
    static_cast<CoverArtManager*>(parameter)->_taskLoop();
}

bool CoverArtManager::_isCurrent(const Request& request) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool current = request.generation == _currentGeneration &&
                         strcmp(request.key, _currentKey) == 0;
    xSemaphoreGive(_mutex);
    return current;
}

bool CoverArtManager::_beginNetworkRequest(const Request& request) {
    for (;;) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        if (!_networkPaused) {
            _networkActive = true;
            xSemaphoreGive(_mutex);
            return true;
        }
        xSemaphoreGive(_mutex);
        if (!_isCurrent(request)) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void CoverArtManager::_finishNetworkRequest() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _networkActive = false;
    xSemaphoreGive(_mutex);
}

void CoverArtManager::_publish(const Request& request, uint8_t* data, size_t size, bool jpeg) {
    bool published = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (request.generation == _currentGeneration && strcmp(request.key, _currentKey) == 0) {
        if (_readyData) free(_readyData);
        _readyData = data;
        _readySize = size;
        _readyJpeg = jpeg;
        _readyGeneration = request.generation;
        strlcpy(_readyKey, request.key, sizeof(_readyKey));
        published = true;
    }
    xSemaphoreGive(_mutex);

    if (published) {
        log_i("##[COVER]# ready: '%s' - '%s' (%u bytes, internal largest=%u)",
              request.artist, request.title, static_cast<unsigned>(size),
              static_cast<unsigned>(
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        display.putRequest(NEWCOVER);
    } else {
        free(data);
    }
}

void CoverArtManager::_taskLoop() {
    Request request{};
    for (;;) {
        if (xQueueReceive(_queue, &request, portMAX_DELAY) != pdTRUE) continue;

        vTaskDelay(pdMS_TO_TICKS(REQUEST_SETTLE_MS));
        Request newer{};
        while (xQueueReceive(_queue, &newer, 0) == pdTRUE) request = newer;
        if (!_isCurrent(request)) continue;

        if (WiFi.status() != WL_CONNECTED) {
            log_w("##[COVER]# skipped, Wi-Fi is not connected");
            continue;
        }
        if (!_beginNetworkRequest(request)) continue;

        uint8_t* data = nullptr;
        size_t size = 0;
        bool jpeg = false;
        if (request.localFileMode) {
            log_i("##[SDCOVER]# lookup: '%s' - '%s'", request.artist, request.title);
        }
        const bool found = fetchCover(request.artist, request.title,
                                      request.bluetoothTitleMode,
                                      request.localFileMode, data, size, jpeg);
        _finishNetworkRequest();
        if (networkPaused()) {
            if (data) free(data);
            continue;
        }
        if (!found) {
            if (request.localFileMode) {
                log_i("##[SDCOVER]# no cover: '%s' - '%s'",
                      request.artist, request.title);
            } else {
                log_d("##[COVER]# no cover: '%s' - '%s'",
                      request.artist, request.title);
            }
            continue;
        }
        _publish(request, data, size, jpeg);
    }
}

#endif
