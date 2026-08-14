#pragma once

#include "options.h"

#ifdef USE_LASTFM_COVER

#include <Arduino.h>

class CoverArtManager {
  public:
    void begin();
    void requestCombined(const char* combinedTitle, bool allowTitleSearch = false);
    bool copyReadyFor(const char* combinedTitle, bool allowTitleSearch,
                      uint8_t*& data, size_t& size, bool& jpeg,
                      uint32_t& generation);

  private:
    static constexpr size_t ARTIST_LEN = 128;
    static constexpr size_t TITLE_LEN  = 192;
    static constexpr size_t KEY_LEN    = ARTIST_LEN + TITLE_LEN + 2;

    struct Request {
        uint32_t generation;
        char     artist[ARTIST_LEN];
        char     title[TITLE_LEN];
        char     key[KEY_LEN];
        bool     allowTitleSearch;
    };

    QueueHandle_t _queue = nullptr;
    TaskHandle_t  _task  = nullptr;
    SemaphoreHandle_t _mutex = nullptr;

    uint32_t _currentGeneration = 0;
    char     _currentKey[KEY_LEN] = {0};
    bool     _currentAllowTitleSearch = false;

    uint8_t* _readyData = nullptr;
    size_t   _readySize = 0;
    bool     _readyJpeg = false;
    uint32_t _readyGeneration = 0;
    char     _readyKey[KEY_LEN] = {0};

    static void _taskEntry(void* parameter);
    void        _taskLoop();
    bool        _isCurrent(const Request& request);
    void        _publish(const Request& request, uint8_t* data, size_t size, bool jpeg);
};

extern CoverArtManager coverArt;

#endif
