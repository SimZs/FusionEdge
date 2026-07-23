#pragma once

#include <Arduino.h>
#include <climits>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lgfx/v1/Bus.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

#ifndef AXS_FLUSH_TASK_DELAY_MS
#define AXS_FLUSH_TASK_DELAY_MS 1
#endif

#ifndef AXS_SMOOTH_SCROLL
#define AXS_SMOOTH_SCROLL 1
#endif

// Very light driver-side coalescing: wait only one RTOS tick so that
// multiple sprite pushes from the same UI loop can collapse into one
// physical full-frame flush. No fixed 60 Hz pacing here, because that made
// scrolling visibly slow on JC3248W535C.
#ifndef AXS_FLUSH_COALESCE_MS
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_COALESCE_MS 1
#else
#define AXS_FLUSH_COALESCE_MS 5
#endif
#endif

#ifndef AXS_SCAN_BUILD_YIELD_COLS
#define AXS_SCAN_BUILD_YIELD_COLS 8
#endif

#ifndef AXS_FLUSH_TASK_PRIORITY
#define AXS_FLUSH_TASK_PRIORITY 1
#endif

#ifndef AXS_FLUSH_TASK_CORE
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_TASK_CORE 0
#else
#define AXS_FLUSH_TASK_CORE 1
#endif
#endif

#ifndef AXS_FLUSH_CHUNK_DELAY_EVERY
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_CHUNK_DELAY_EVERY 0
#else
#define AXS_FLUSH_CHUNK_DELAY_EVERY 2
#endif
#endif

#ifndef AXS_FLUSH_CHUNK_DELAY_MS
#if AXS_SMOOTH_SCROLL
#define AXS_FLUSH_CHUNK_DELAY_MS 0
#else
#define AXS_FLUSH_CHUNK_DELAY_MS 1
#endif
#endif

#ifndef AXS_STABLE_BOOT_FLUSHES
#define AXS_STABLE_BOOT_FLUSHES 80
#endif

#ifndef AXS_SCAN_SMALL_DIRTY_PIXELS
#define AXS_SCAN_SMALL_DIRTY_PIXELS 8000
#endif

#ifndef AXS_ENABLE_SCAN_FLUSH
#define AXS_ENABLE_SCAN_FLUSH 0
#endif

#ifndef AXS_FLUSH_DIAG
#define AXS_FLUSH_DIAG 0
#endif

#ifndef AXS_FLUSH_DIAG_INTERVAL_MS
#define AXS_FLUSH_DIAG_INTERVAL_MS 1000UL
#endif

namespace lgfx {
inline namespace v1 {

struct Panel_AXS15231B : public Panel_FrameBufferBase {
    static constexpr uint8_t CMD_SWRESET = 0x01;
    static constexpr uint8_t CMD_SLPIN = 0x10;
    static constexpr uint8_t CMD_SLPOUT = 0x11;
    static constexpr uint8_t CMD_INVOFF = 0x20;
    static constexpr uint8_t CMD_INVON = 0x21;
    static constexpr uint8_t CMD_PIXELS_OFF = 0x22;
    static constexpr uint8_t CMD_CASET = 0x2A;
    static constexpr uint8_t CMD_RAMWR = 0x2C;
    static constexpr uint8_t SEND_PIXELS = 0x32;

    Panel_AXS15231B(void) {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 320;
        _cfg.readable = false;
        _cfg.bus_shared = false;
        _write_depth = rgb565_2Byte;
        _read_depth = rgb565_2Byte;
    }

    bool init(bool use_reset) override {
        if (!allocateFrameBuffer()) {
            Serial.println("##[AXS display driver]#: Out of PSRAM !");
            return false;
        }

        if (!Panel_FrameBufferBase::init(use_reset)) {
            return false;
        }
        _busReady = true;
        _busMutex = xSemaphoreCreateMutex();
        _frameMutex = xSemaphoreCreateRecursiveMutex();

        if (use_reset && _cfg.pin_rst < 0) {
            sendCommand(CMD_SWRESET);
        }
        delay(250);

        sendCommand(CMD_PIXELS_OFF);
        sendCommand(0x13);
        sendCommand(CMD_SLPOUT);
        delay(200);
        sendCommand(0x29);
        delay(200);

        const uint8_t zeroes[] = {0x00, 0x00, 0x00, 0x00};
        sendCommandData(CMD_RAMWR, zeroes, sizeof(zeroes));

        const uint16_t wh = _cfg.panel_height - 1;
        const uint8_t col[] = {0x00, 0x00, static_cast<uint8_t>(wh >> 8), static_cast<uint8_t>(wh & 0xFF)};
        sendCommandData(CMD_CASET, col, sizeof(col));

        memset(_framebuffer, 0, _bufferBytes);
        markFullDirty();
        display(0, 0, _width, _height);
        startFlushTask();
        return true;
    }

    void releaseBus(void) override {
        stopFlushTask();
        freeFrameBuffer();
        if (_busMutex) {
            vSemaphoreDelete(_busMutex);
            _busMutex = nullptr;
        }
        if (_frameMutex) {
            vSemaphoreDelete(_frameMutex);
            _frameMutex = nullptr;
        }
        Panel_FrameBufferBase::releaseBus();
    }

    void beginTransaction(void) override {
        lockFrame();
    }

    void endTransaction(void) override {
        requestFlush();
        unlockFrame();
    }

    void display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h) override {
        (void)x;
        (void)y;
        (void)w;
        (void)h;
#if AXS_FLUSH_DIAG
        const uint32_t diagStartUs = micros();
#endif

        if (_framebuffer == nullptr || _flushLine == nullptr || _range_mod.empty()) {
            return;
        }

        lockBus();
        const bool ownTransaction = !_inBusTransaction;
        if (ownTransaction) {
            _bus->beginTransaction();
            _inBusTransaction = true;
        }

        const bool useStablePath = shouldUseStablePath();

        _flushInProgress = true;
        if (useStablePath) {
            lockFrame();
            sendFrameDirect();
            unlockFrame();
            if (_stableBootFlushes) {
                --_stableBootFlushes;
            }
        } else {
            lockFrame();
            updateScanBufferFromFrame();
            unlockFrame();
            sendScanBuffer();
        }

        const bool hadDirtyDuring = _dirtyDuringFlush;
        if (_dirtyDuringFlush) {
            markFullDirty();
            _flushRequested = true;
            _dirtyDuringFlush = false;
        } else {
            clearDirtyRange();
        }
        _flushInProgress = false;

        if (ownTransaction) {
            _bus->endTransaction();
            _inBusTransaction = false;
        }
        unlockBus();
#if AXS_FLUSH_DIAG
        const uint32_t nowMs = millis();
        const uint32_t flushUs = micros() - diagStartUs;
        if (_diagFlushWindowMs == 0) {
            _diagFlushWindowMs = nowMs;
        }
        _diagFlushSamples++;
        _diagFlushSumUs += flushUs;
        if (flushUs > _diagFlushMaxUs) _diagFlushMaxUs = flushUs;
        if (hadDirtyDuring) _diagFlushDirtyDuring++;
        if (nowMs - _diagFlushWindowMs >= AXS_FLUSH_DIAG_INTERVAL_MS) {
            Serial.printf(
                "AXS_FLUSH core=%d samples=%lu avg_us=%lu max_us=%lu dirty_during=%lu req=%u stable=%u free_heap=%u psram=%u\n",
                xPortGetCoreID(),
                (unsigned long)_diagFlushSamples,
                (unsigned long)(_diagFlushSamples ? _diagFlushSumUs / _diagFlushSamples : 0),
                (unsigned long)_diagFlushMaxUs,
                (unsigned long)_diagFlushDirtyDuring,
                _flushRequested ? 1 : 0,
                _stableBootFlushes,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram()
            );
            _diagFlushWindowMs = nowMs;
            _diagFlushSamples = 0;
            _diagFlushSumUs = 0;
            _diagFlushMaxUs = 0;
            _diagFlushDirtyDuring = 0;
        }
#endif
    }

    void setInvert(bool invert) override {
        _invert = invert;
        _softwareInvert = invert;
        if (!_busReady) {
            return;
        }
        sendCommand(CMD_INVOFF);
        _stableBootFlushes = AXS_STABLE_BOOT_FLUSHES;
        markFullDirty();
        display(0, 0, _width, _height);
    }

    void setSleep(bool sleep) override {
        if (!_busReady) {
            return;
        }
        _sleeping = sleep;
        sendCommand(sleep ? CMD_SLPIN : CMD_SLPOUT);
        delay(200);
        if (!sleep) {
            _stableBootFlushes = AXS_STABLE_BOOT_FLUSHES;
            markFullDirty();
            _flushRequested = true;
        }
    }

    bool isFlushBusy() const {
        return _flushInProgress;
    }

    bool isFrameBusy() const {
        if (!_frameMutex) {
            return false;
        }
        if (xSemaphoreTakeRecursive(_frameMutex, 0) == pdTRUE) {
            xSemaphoreGiveRecursive(_frameMutex);
            return false;
        }
        return true;
    }

    bool tryBeginFrameAccess() {
        if (!_frameMutex) {
            return true;
        }
        return xSemaphoreTakeRecursive(_frameMutex, 0) == pdTRUE;
    }

    void endFrameAccess() {
        if (_frameMutex) {
            xSemaphoreGiveRecursive(_frameMutex);
        }
    }

    bool blitFrameBlock(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
        return blitFrameBlockImpl(x, y, w, h, pixels, true);
    }

    bool blitFrameBlockDeferred(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
        return blitFrameBlockImpl(x, y, w, h, pixels, false);
    }

    bool fillFrameBlockDeferred(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
        return fillFrameBlockImpl(x, y, w, h, color, false);
    }

private:
    bool fillFrameBlockImpl(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color, bool requestDisplayFlush) {
        if (!_framebuffer || !_lineTable || w <= 0 || h <= 0) {
            return false;
        }

        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > _cfg.memory_width) {
            w = _cfg.memory_width - x;
        }
        if (y + h > _cfg.memory_height) {
            h = _cfg.memory_height - y;
        }
        if (w <= 0 || h <= 0) {
            return false;
        }

        lockFrame();
        for (int32_t row = 0; row < h; ++row) {
            uint16_t* dst = reinterpret_cast<uint16_t*>(_lineTable[y + row] + (x * sizeof(uint16_t)));
            for (int32_t col = 0; col < w; ++col) {
                dst[col] = color;
            }
        }
        markFullDirty();
        if (requestDisplayFlush) {
            requestFlush();
        }
        unlockFrame();
        return true;
    }

    bool blitFrameBlockImpl(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels, bool requestDisplayFlush) {
        if (!_framebuffer || !_lineTable || !pixels || w <= 0 || h <= 0) {
            return false;
        }

        const int32_t srcStride = w;
        int32_t srcX = 0;
        int32_t srcY = 0;
        if (x < 0) {
            srcX = -x;
            w += x;
            x = 0;
        }
        if (y < 0) {
            srcY = -y;
            h += y;
            y = 0;
        }
        if (x + w > _cfg.memory_width) {
            w = _cfg.memory_width - x;
        }
        if (y + h > _cfg.memory_height) {
            h = _cfg.memory_height - y;
        }
        if (w <= 0 || h <= 0) {
            return false;
        }

        lockFrame();
        for (int32_t row = 0; row < h; ++row) {
            const uint16_t* src = pixels + static_cast<size_t>(srcY + row) * static_cast<size_t>(srcStride) + srcX;
            uint8_t* dst = _lineTable[y + row] + (x * sizeof(uint16_t));
            memcpy(dst, src, static_cast<size_t>(w) * sizeof(uint16_t));
        }
        markFullDirty();
        if (requestDisplayFlush) {
            requestFlush();
        }
        unlockFrame();
        return true;
    }

    static constexpr uint16_t FLUSH_PIXELS = 12800;
    uint8_t** _lineTable = nullptr;
    uint8_t* _framebuffer = nullptr;
    uint16_t* _scanBuffer = nullptr;
    uint16_t* _flushLine = nullptr;
    size_t _bufferBytes = 0;
    bool _busReady = false;
    bool _inBusTransaction = false;
    bool _softwareInvert = false;
    volatile bool _flushRequested = false;
    volatile bool _flushInProgress = false;
    volatile bool _dirtyDuringFlush = false;
    volatile bool _flushTaskStop = false;
    volatile bool _sleeping = false;
    volatile TickType_t _flushDueTick = 0;
    uint8_t _stableBootFlushes = AXS_STABLE_BOOT_FLUSHES;
    TaskHandle_t _flushTaskHandle = nullptr;
    SemaphoreHandle_t _busMutex = nullptr;
    SemaphoreHandle_t _frameMutex = nullptr;
#if AXS_FLUSH_DIAG
    uint32_t _diagFlushWindowMs = 0;
    uint32_t _diagFlushSamples = 0;
    uint32_t _diagFlushSumUs = 0;
    uint32_t _diagFlushMaxUs = 0;
    uint32_t _diagFlushDirtyDuring = 0;
#endif

    bool allocateFrameBuffer() {
        if (_framebuffer) {
            return true;
        }

        const size_t lineBytes = _cfg.memory_width * sizeof(uint16_t);
        _bufferBytes = lineBytes * _cfg.memory_height;
        _framebuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, _bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#if AXS_ENABLE_SCAN_FLUSH
        _scanBuffer = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, _bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
        _lineTable = static_cast<uint8_t**>(heap_caps_calloc(_cfg.memory_height, sizeof(uint8_t*), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        _flushLine = static_cast<uint16_t*>(heap_caps_aligned_alloc(16, FLUSH_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

        if (!_framebuffer || !_lineTable || !_flushLine) {
            freeFrameBuffer();
            return false;
        }
#if AXS_ENABLE_SCAN_FLUSH
        if (!_scanBuffer) {
            freeFrameBuffer();
            return false;
        }
#endif

        for (uint_fast16_t y = 0; y < _cfg.memory_height; ++y) {
            _lineTable[y] = _framebuffer + (y * lineBytes);
        }
        _lines_buffer = _lineTable;
#if AXS_ENABLE_SCAN_FLUSH
        memset(_scanBuffer, 0, _bufferBytes);
#endif
        return true;
    }

    void freeFrameBuffer() {
        if (_framebuffer) {
            heap_caps_free(_framebuffer);
            _framebuffer = nullptr;
        }
        if (_lineTable) {
            heap_caps_free(_lineTable);
            _lineTable = nullptr;
        }
        if (_scanBuffer) {
            heap_caps_free(_scanBuffer);
            _scanBuffer = nullptr;
        }
        if (_flushLine) {
            heap_caps_free(_flushLine);
            _flushLine = nullptr;
        }
        _lines_buffer = nullptr;
    }

    void markFullDirty() {
        _range_mod.top = 0;
        _range_mod.left = 0;
        _range_mod.right = _cfg.memory_width - 1;
        _range_mod.bottom = _cfg.memory_height - 1;
    }

    void clearDirtyRange() {
        _range_mod.top = INT16_MAX;
        _range_mod.left = INT16_MAX;
        _range_mod.right = 0;
        _range_mod.bottom = 0;
    }

    bool shouldUseStablePath() const {
        if (!AXS_ENABLE_SCAN_FLUSH) {
            return true;
        }
        if (_stableBootFlushes) {
            return true;
        }
        if (_range_mod.empty()) {
            return true;
        }
        const uint32_t dirtyHeight = static_cast<uint32_t>(_range_mod.bottom - _range_mod.top + 1);
        const uint32_t dirtyWidth = static_cast<uint32_t>(_range_mod.right - _range_mod.left + 1);
        return (dirtyWidth * dirtyHeight) > AXS_SCAN_SMALL_DIRTY_PIXELS;
    }

    void sendFrameDirect() {
        startQspiMemoryWrite();
        uint_fast16_t outCount = 0;
        uint8_t chunkCount = 0;

        for (int_fast16_t sx = _cfg.memory_width - 1; sx >= 0; --sx) {
            for (uint_fast16_t sy = 0; sy < _cfg.memory_height; ++sy) {
                uint16_t raw;
                memcpy(&raw, &_lines_buffer[sy][sx * sizeof(uint16_t)], sizeof(raw));
                if (_softwareInvert) {
                    raw ^= 0xFFFF;
                }
                _flushLine[outCount++] = raw;

                if (outCount == FLUSH_PIXELS) {
                    _bus->writeBytes(reinterpret_cast<const uint8_t*>(_flushLine), FLUSH_PIXELS * sizeof(uint16_t), true, true);
                    outCount = 0;
                    yieldAfterFlushChunk(++chunkCount);
                }
            }
        }

        if (outCount) {
            _bus->writeBytes(reinterpret_cast<const uint8_t*>(_flushLine), outCount * sizeof(uint16_t), true, true);
        }

        _bus->wait();
        cs_control(true);
    }

    void updateScanBufferFromFrame() {
        if (_scanBuffer == nullptr) {
            return;
        }

        for (uint_fast16_t x = 0; x < _cfg.memory_width; ++x) {
            const uint_fast16_t sx = _cfg.memory_width - 1 - x;
            uint16_t* out = &_scanBuffer[static_cast<size_t>(x) * _cfg.memory_height];
            for (uint_fast16_t y = 0; y < _cfg.memory_height; ++y) {
                uint16_t raw;
                memcpy(&raw, &_lines_buffer[y][sx * sizeof(uint16_t)], sizeof(raw));
                out[y] = raw;
            }
            if (AXS_SCAN_BUILD_YIELD_COLS && (x % AXS_SCAN_BUILD_YIELD_COLS) == 0) {
                taskYIELD();
            }
        }
    }

    void sendScanBuffer() {
        if (_scanBuffer == nullptr) {
            return;
        }

        startQspiMemoryWrite();
        const size_t totalPixels = static_cast<size_t>(_cfg.memory_width) * _cfg.memory_height;
        uint8_t chunkCount = 0;

        for (size_t offset = 0; offset < totalPixels; offset += FLUSH_PIXELS) {
            const size_t count = ((totalPixels - offset) > FLUSH_PIXELS) ? FLUSH_PIXELS : (totalPixels - offset);

            if (_softwareInvert) {
                for (size_t i = 0; i < count; ++i) {
                    _flushLine[i] = _scanBuffer[offset + i] ^ 0xFFFF;
                }
            } else {
                memcpy(_flushLine, &_scanBuffer[offset], count * sizeof(uint16_t));
            }

            _bus->writeBytes(reinterpret_cast<const uint8_t*>(_flushLine), count * sizeof(uint16_t), true, true);
            yieldAfterFlushChunk(++chunkCount);
        }

        _bus->wait();
        cs_control(true);
    }

    void scheduleFlush() {
        const TickType_t due = xTaskGetTickCount() + pdMS_TO_TICKS(AXS_FLUSH_COALESCE_MS);
        if (!_flushRequested || _flushDueTick == 0 || due < _flushDueTick) {
            _flushDueTick = due;
        }
        _flushRequested = true;
    }

    void requestFlush() {
        if (_flushInProgress) {
            _dirtyDuringFlush = true;
            scheduleFlush();
            return;
        }
        if (!_range_mod.empty()) {
            scheduleFlush();
        }
    }

    void startFlushTask() {
        if (_flushTaskHandle == nullptr) {
            _flushTaskStop = false;
            xTaskCreatePinnedToCore(flushTaskEntry, "AXSFlush", 4096, this, AXS_FLUSH_TASK_PRIORITY, &_flushTaskHandle, AXS_FLUSH_TASK_CORE);
        }
    }

    void stopFlushTask() {
        _flushTaskStop = true;
        if (_flushTaskHandle != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(20));
            _flushTaskHandle = nullptr;
        }
    }

    static void flushTaskEntry(void* arg) {
        static_cast<Panel_AXS15231B*>(arg)->flushTaskLoop();
    }

    void flushTaskLoop() {
        while (!_flushTaskStop) {
            if (_flushRequested && !_sleeping) {
                const TickType_t now = xTaskGetTickCount();
                if (_flushDueTick && now < _flushDueTick) {
                    vTaskDelay(_flushDueTick - now);
                    continue;
                }

                _flushRequested = false;
                _dirtyDuringFlush = false;
                display(0, 0, _width, _height);
            }
            vTaskDelay(pdMS_TO_TICKS(AXS_FLUSH_TASK_DELAY_MS));
        }
        vTaskDelete(nullptr);
    }

    void lockBus() {
        if (_busMutex) {
            xSemaphoreTake(_busMutex, portMAX_DELAY);
        }
    }

    void unlockBus() {
        if (_busMutex) {
            xSemaphoreGive(_busMutex);
        }
    }

    void lockFrame() {
        if (_frameMutex) {
            xSemaphoreTakeRecursive(_frameMutex, portMAX_DELAY);
        }
    }

    void unlockFrame() {
        if (_frameMutex) {
            xSemaphoreGiveRecursive(_frameMutex);
        }
    }

    void writeQspiCommandPrefix(uint8_t cmd) {
        _bus->writeCommand(0x02, 8);
        _bus->writeCommand(0x00, 8);
        _bus->writeCommand(cmd, 8);
        _bus->writeCommand(0x00, 8);
    }

    void yieldAfterFlushChunk(uint8_t chunkCount) {
        if (AXS_FLUSH_CHUNK_DELAY_EVERY && (chunkCount % AXS_FLUSH_CHUNK_DELAY_EVERY) == 0) {
            vTaskDelay(pdMS_TO_TICKS(AXS_FLUSH_CHUNK_DELAY_MS));
        } else {
            taskYIELD();
        }
    }

    void sendCommand(uint8_t cmd) {
        lockBus();
        const bool ownTransaction = !_inBusTransaction;
        if (ownTransaction) {
            _bus->beginTransaction();
            _inBusTransaction = true;
        }
        cs_control(false);
        writeQspiCommandPrefix(cmd);
        _bus->wait();
        cs_control(true);
        if (ownTransaction) {
            _bus->endTransaction();
            _inBusTransaction = false;
        }
        unlockBus();
    }

    void sendCommandData(uint8_t cmd, const uint8_t* data, size_t len) {
        lockBus();
        const bool ownTransaction = !_inBusTransaction;
        if (ownTransaction) {
            _bus->beginTransaction();
            _inBusTransaction = true;
        }
        cs_control(false);
        writeQspiCommandPrefix(cmd);
        for (size_t i = 0; i < len; ++i) {
            _bus->writeCommand(data[i], 8);
        }
        _bus->wait();
        cs_control(true);
        if (ownTransaction) {
            _bus->endTransaction();
            _inBusTransaction = false;
        }
        unlockBus();
    }

    void startQspiMemoryWrite() {
        cs_control(false);
        _bus->writeCommand(SEND_PIXELS, 8);
        _bus->writeCommand(0x00, 8);
        _bus->writeCommand(CMD_RAMWR, 8);
        _bus->writeCommand(0x00, 8);
        _bus->wait();
    }
};

} // namespace v1
} // namespace lgfx
