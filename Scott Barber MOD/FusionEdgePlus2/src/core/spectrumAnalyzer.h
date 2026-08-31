#pragma once
#ifndef SPECTRUM_ANALYZER_H
#define SPECTRUM_ANALYZER_H

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "esp_dsp.h"

#define SA_FFT_SIZE    512
#define SA_BANDS_MAX   64
#define SA_BIN_LOW     2      // ~172 Hz @ 44100/512
#define SA_BIN_HIGH    186    // ~16 kHz
#define SA_UPDATE_MS   100

// Sample kiolvasás: a dekóder int16 értékeket ír int32* bufferbe (packed, alsó 16 bit).
// >> 16 / 32768.0f  → [-1.0 .. +1.0] tartomány
// Ha a spektrum túl gyenge: próbáld >> 12 / 4096.0f értékkel (erősebb jel)
// Ha a spektrum saturál: >> 16 marad, csak DB_MAX-ot emeld (-3 → +3)
#define SA_SHIFT       16
#define SA_SCALE       32768.0f

class SpectrumAnalyzer {
public:
    SpectrumAnalyzer() {}

    void begin(uint8_t bands = 16) {
        _bands = (bands > 0 && bands <= SA_BANDS_MAX) ? bands : 16;

        for (int i = 0; i < SA_FFT_SIZE; i++)
            _window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (SA_FFT_SIZE - 1)));

        float logLow  = log10f((float)SA_BIN_LOW);
        float logHigh = log10f((float)SA_BIN_HIGH);
        float step    = (logHigh - logLow) / _bands;
        for (int i = 0; i <= _bands; i++)
            _edgeBin[i] = (int)roundf(powf(10.0f, logLow + i * step));

        esp_err_t err = dsps_fft2r_init_fc32(nullptr, SA_FFT_SIZE);
        _initialized = (err == ESP_OK);

        memset(_circBuf, 0, sizeof(_circBuf));
        _writeIdx   = 0;
        _sampleRate = 44100;
        _lastMs     = 0;
        resetSmooth();
    }

    uint8_t bands() const { return _bands; }

    void IRAM_ATTR pushSamples(const int32_t* buf, int16_t frames) {
        if (!_initialized) return;
        for (int16_t i = 0; i < frames; i++) {
            float l = (float)(buf[i * 2]     >> SA_SHIFT) / SA_SCALE;
            float r = (float)(buf[i * 2 + 1] >> SA_SHIFT) / SA_SCALE;
            _circBuf[_writeIdx] = 0.5f * (l + r);
            _writeIdx = (uint16_t)(_writeIdx + 1);
            if (_writeIdx >= SA_FFT_SIZE) _writeIdx = 0;
            if (buf[i*2] > _rawMax) _rawMax = buf[i*2];
            if (buf[i*2] < _rawMin) _rawMin = buf[i*2];
        }
    }

    void setSampleRate(uint32_t sr) { if (sr > 0) _sampleRate = sr; }

    bool process() {
        if (!_initialized) return false;
        uint32_t now = millis();
        if (now - _lastMs < SA_UPDATE_MS) return false;
        _lastMs = now;

        uint16_t wi = _writeIdx;
        for (int i = 0; i < SA_FFT_SIZE; i++) {
            int src = (wi + i) % SA_FFT_SIZE;
            _work[2 * i]     = _circBuf[src] * _window[i];
            _work[2 * i + 1] = 0.0f;
        }

        dsps_fft2r_fc32(_work, SA_FFT_SIZE);
        dsps_bit_rev_fc32(_work, SA_FFT_SIZE);
        dsps_cplx2reC_fc32(_work, SA_FFT_SIZE);

        const float norm = 2.0f / SA_FFT_SIZE;

        float bandPeak[SA_BANDS_MAX] = {0};
        for (int b = 0; b < _bands; b++) {
            int binLo = _edgeBin[b];
            int binHi = _edgeBin[b + 1];
            if (binHi <= binLo) binHi = binLo + 1;
            if (binHi >= SA_FFT_SIZE / 2) binHi = SA_FFT_SIZE / 2 - 1;
            for (int i = binLo; i < binHi; i++) {
                float re  = _work[2 * i];
                float im  = _work[2 * i + 1];
                float mag = sqrtf(re * re + im * im) * norm;
                if (mag > bandPeak[b]) bandPeak[b] = mag;
            }
        }

        // Envelope follower
        constexpr float ATTACK_MS   = 30.0f;
        constexpr float RELEASE_MS  = 150.0f;   // gyorsabb esés → mirror sem lesz statikus
        constexpr float NOISE_FLOOR = 0.0008f;  // ~-62 dB, DB_MIN alatt → valódi csend

        const float dt      = SA_UPDATE_MS / 1000.0f;
        const float attCoef = 1.0f - expf(-dt / (ATTACK_MS  / 1000.0f));
        const float relCoef = 1.0f - expf(-dt / (RELEASE_MS / 1000.0f));

        // Per-sáv boost – a mért átlagos peak értékekből számolva,
        // hogy minden sáv kb. egyforma "átlagos" szinten mozogjon.
        // B1 (basszus) természetesen erős → visszafogva
        // B6-B8 (közép-magas) és B12-B14 (magasak) gyengék → emelve
        static const float bandBoost[SA_BANDS_MAX] = {
            0.55f, 1.00f, 0.85f, 1.90f, 1.90f, 2.40f, 2.40f,
            2.40f, 1.85f, 2.00f, 2.20f, 2.50f, 2.50f, 2.50f,
            // a 14-nél több sávra: lineárisan 2.5x marad
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
            2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f, 2.50f,
        };

        // DB_MAX=-6: a mért max peak ~0.566 (-4.9dB), ez felett saturál – rendben
        // DB_MIN=-52: mag=0.002 (-54dB) → ~0% – csend valóban csend lesz
        constexpr float DB_MIN = -52.0f;
        constexpr float DB_MAX =  -6.0f;

        for (int i = 0; i < _bands; i++) {
            float boost = bandBoost[i < SA_BANDS_MAX ? i : SA_BANDS_MAX-1];
            float mag = bandPeak[i] * boost;

            float v;
            if (mag < NOISE_FLOOR) {
                v = 0.0f;
            } else {
                float db = 20.0f * log10f(mag);
                v = (db - DB_MIN) / (DB_MAX - DB_MIN);
                if (v < 0.0f) v = 0.0f;
                if (v > 1.0f) v = 1.0f;
            }

            float old = _smooth[i];
            _smooth[i] = old + (v > old ? attCoef : relCoef) * (v - old);
            if (_smooth[i] < 0.0f) _smooth[i] = 0.0f;
            if (_smooth[i] > 1.0f) _smooth[i] = 1.0f;

            _spectrum[i] = (uint8_t)(_smooth[i] * 255.0f);
        }

        constexpr uint8_t HOLD_TICKS = 10;
        constexpr uint8_t FALL_STEP  = 8;
        for (int i = 0; i < _bands; i++) {
            if (_spectrum[i] >= _peak[i]) {
                _peak[i]     = _spectrum[i];
                _peakHold[i] = HOLD_TICKS;
            } else if (_peakHold[i] > 0) {
                _peakHold[i]--;
            } else {
                _peak[i] = (_peak[i] > FALL_STEP) ? _peak[i] - FALL_STEP : 0;
            }
        }

        return true;
    }

    void getData(uint8_t* specOut, uint8_t* peakOut) const {
        memcpy(specOut, _spectrum, _bands);
        if (peakOut) memcpy(peakOut, _peak, _bands);
    }

    float getWaveform(float* waveOut, uint16_t points) const {
        if (!waveOut || points == 0 || !_initialized) return 0.0f;
        if (points > SA_FFT_SIZE) points = SA_FFT_SIZE;

        const uint16_t wi = _writeIdx;
        uint16_t startOffset = 0;
        const uint16_t triggerSearch = 64;
        for (uint16_t i = 1; i < triggerSearch; i++) {
            const float prev = _circBuf[(wi + i - 1) % SA_FFT_SIZE];
            const float curr = _circBuf[(wi + i) % SA_FFT_SIZE];
            if (prev <= 0.0f && curr > 0.0f) {
                startOffset = i;
                break;
            }
        }

        const uint16_t span = SA_FFT_SIZE - startOffset;
        for (uint16_t i = 0; i < points; i++) {
            const uint16_t offset = startOffset +
                (uint16_t)((uint32_t)i * (span - 1) / max((uint16_t)1, (uint16_t)(points - 1)));
            waveOut[i] = _circBuf[(wi + offset) % SA_FFT_SIZE];
        }

        float sumSquares = 0.0f;
        for (uint16_t i = 0; i < SA_FFT_SIZE; i++) {
            const float sample = _circBuf[i];
            sumSquares += sample * sample;
        }
        return sqrtf(sumSquares / SA_FFT_SIZE);
    }

    void resetSmooth() {
        for (int i = 0; i < SA_BANDS_MAX; i++) {
            _smooth[i]   = 0.0f;
            _spectrum[i] = 0;
            _peak[i]     = 0;
            _peakHold[i] = 0;
        }
    }

    bool isRunning() const { return _initialized; }

private:
    bool     _initialized              = false;
    uint8_t  _bands                    = 16;
    int      _edgeBin[SA_BANDS_MAX+1]  = {0};
    float    _window[SA_FFT_SIZE]      = {0};
    float    _circBuf[SA_FFT_SIZE]     = {0};
    float    _work[SA_FFT_SIZE * 2]    = {0};
    volatile uint16_t _writeIdx        = 0;
    uint32_t _sampleRate               = 44100;
    uint32_t _lastMs                   = 0;
    float    _smooth[SA_BANDS_MAX]     = {0};
    uint8_t  _spectrum[SA_BANDS_MAX]   = {0};
    uint8_t  _peak[SA_BANDS_MAX]       = {0};
    uint8_t  _peakHold[SA_BANDS_MAX]   = {0};
    volatile int32_t _rawMin           =  2147483647L;
    volatile int32_t _rawMax           = -2147483648L;
};

extern SpectrumAnalyzer spectrumAnalyzer;

#endif
