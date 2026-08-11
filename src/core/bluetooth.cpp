#include "options.h"

#ifdef USE_BLUETOOTH

#include "bluetooth.h"
#include "../../myoptions.h"
#include "bluetooth_config.h"
#include "player.h"
#include "config.h"
#include "display.h"
#include "netserver.h"
#include "spectrumAnalyzer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(BT_UART_TX) || !defined(BT_UART_RX) || !defined(BT_UART_BAUD) || \
    !defined(BT_I2S_BCK) || !defined(BT_I2S_LRCK) || !defined(BT_I2S_DATA)
#    error "USE_BLUETOOTH requires BT UART and I2S pin definitions in myoptions.h"
#endif

// Player : public Audio (see player.h) - the global `player` IS the Audio
// instance that owns the DAC TX channel, so we drive the bridge through it.

BluetoothPlayer bluetooth;

#ifndef BT_I2S_SAMPLE_RATE
#define BT_I2S_SAMPLE_RATE 44100
#endif

#ifndef BT_I2S_BITS
#define BT_I2S_BITS 16
#endif

#ifndef BT_I2S_RX_MASTER
#define BT_I2S_RX_MASTER true
#endif

#ifndef BT_I2S_USE_MCLK
#define BT_I2S_USE_MCLK true
#endif

#ifndef BT_I2S_FORMAT
#define BT_I2S_FORMAT 0
#endif

#ifndef BT_I2S_BCLK_INV
#define BT_I2S_BCLK_INV false
#endif

#ifndef BT_I2S_WS_INV
#define BT_I2S_WS_INV false
#endif

#ifndef BT_I2S_32_SHIFT
#define BT_I2S_32_SHIFT 16
#endif

#ifndef BT_SPECTRUM_GAIN
#define BT_SPECTRUM_GAIN 1
#endif

#if BT_I2S_BITS != 16 && BT_I2S_BITS != 24 && BT_I2S_BITS != 32
#error "BT_I2S_BITS must be 16, 24 or 32"
#endif

#if BT_I2S_BITS == 24
#define BT_I2S_DATA_WIDTH I2S_DATA_BIT_WIDTH_24BIT
#elif BT_I2S_BITS == 32
#define BT_I2S_DATA_WIDTH I2S_DATA_BIT_WIDTH_32BIT
#else
#define BT_I2S_DATA_WIDTH I2S_DATA_BIT_WIDTH_16BIT
#endif

#if BT_I2S_FORMAT < 0 || BT_I2S_FORMAT > 2
#error "BT_I2S_FORMAT must be 0=Philips, 1=MSB, or 2=PCM"
#endif

static int32_t scaleSpectrumSample(int32_t sample) {
    int64_t scaled = (int64_t)sample * BT_SPECTRUM_GAIN;
    if (scaled > INT32_MAX) return INT32_MAX;
    if (scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

static uint32_t parseUnsignedValue(const char* line) {
    const char* p = strchr(line, '=');
    return p ? (uint32_t)strtoul(p + 1, nullptr, 10) : 0;
}

static bool isSupportedSampleRate(uint32_t sampleRate) {
    return sampleRate == 44100 || sampleRate == 48000 || sampleRate == 96000;
}

// =====================================================================
//  UART AT driver
// =====================================================================

void BluetoothPlayer::begin() {
    _uart.begin(BT_UART_BAUD, SERIAL_8N1, BT_UART_RX, BT_UART_TX);
    _lineLen = 0;
    delay(200);
    // sendAT("AT+SAOUT=I2S");         // appban beállítva
    // sendAT("AT+BOOT");              // nem kell
    // delay(2000);                    // nem kell
    sendAT("AT+SMAUTOI");
    sendAT("AT+SMTIMEON");

    // I2S RX csatorna létrehozása (idle, engedélyezés nélkül — az csak
    // startBridge()-ben történik). Az I2S újrakonfigurálását a keskeny
    // display DMA guard védi; ez nem nyit SPI tranzakciót és nem lockolja
    // tartósan a widgeteket.
    if (initI2SRx()) {
        log_i("##[BT]# UART2 initialized, I2S RX bridge ready");
    } else {
        log_e("##[BT]# I2S RX init failed - audio bridge unavailable (UART/meta still works)");
    }
}

void BluetoothPlayer::end() {
    stopBridge();
    deinitI2SRx();
    _uart.end();
    _state = BT_STATE_UNKNOWN;
}

void BluetoothPlayer::sendAT(const char* cmd) {
    _uart.print(cmd);
    _uart.print("\r\n");
    log_d("##[BT]# TX: %s", cmd);
}

void BluetoothPlayer::play() { sendAT("AT+PP"); }
void BluetoothPlayer::pause() { sendAT("AT+PU"); }
void BluetoothPlayer::toggle() { sendAT("AT+PP"); } // module toggles play/pause on AT+PP
void BluetoothPlayer::next() { sendAT("AT+PN"); }
void BluetoothPlayer::prev() { sendAT("AT+PV"); }
void BluetoothPlayer::volUp() { sendAT("AT+VP"); }
void BluetoothPlayer::volDown() { sendAT("AT+VD"); }

void BluetoothPlayer::setVol(uint8_t step0to32) {
    char buf[24];
    snprintf(buf, sizeof(buf), "AT+SVOL=%u", step0to32);
    sendAT(buf);
}

// --- non-blocking line reader, called from main loop() --------------
void BluetoothPlayer::loop() {
    // Periodikus ping: AT+IQ (BT státusz)
    // Ha erre sem jön válasz, az UART RX bekötés vagy baud rate hibás.
    static uint32_t lastPing = 0;
    uint32_t now = millis();
    if (now - lastPing > 5000) {
        lastPing = now;
        sendAT("AT+IQ");
    }

    while (_uart.available()) {
        char c = (char)_uart.read();
        if (c == '\n' || c == '\r') {
            if (_lineLen > 0) {
                _lineBuf[_lineLen] = '\0';
                parseLine(_lineBuf);
                _lineLen = 0;
            }
            continue;
        }
        if (_lineLen < sizeof(_lineBuf) - 1) { _lineBuf[_lineLen++] = c; }
    }

    // Ha érkezett új cím/előadó, küldjük a display-re.
    // Feltétel: connected legyen (BT_PA vagy BT_STOP is jó — kézi trackváltásnál
    // a metaadat BT_STOP állapotban érkezik, BT_PA csak utána jön)
    if (_metaUpdated && _state >= BT_STATE_CONNECTED) {
        _metaUpdated = false;
        String info;
        if (_artist.length() > 0 && _title.length() > 0) {
            info = _artist + " - " + _title;
        } else if (_title.length() > 0) {
            info = _title;
        } else if (_artist.length() > 0) {
            info = _artist;
        }
        if (info.length() > 0) {
            config.setTitle(info.c_str());
            display.putRequest(NEWTITLE);
            netserver.requestOnChange(TITLE, 0);
        }
    }
}

void BluetoothPlayer::clearMeta() {
    _title      = "";
    _artist     = "";
    _album      = "";
    _genre      = "";
    _trackNum   = 0;
    _trackTotal = 0;
    _trackMs    = 0;
    _streamCodec[0] = '\0';
    _streamBits = 0;
}

void BluetoothPlayer::setFallbackTitle(const char* title) {
    if (config.getMode() != PM_BLUETOOTH) return;
    if (_title.length() > 0 || _artist.length() > 0) return;
    config.setTitle(title);
}

// --- parse minden ismert modul-válasz formátumot ------------------
// Forrás: V108 AT doc + tényleges modul output (2026.07.09)
//
// AT+IQ válaszsorok (állapot lekérdezés):
//   BT_CN   → connected, nem játszik
//   BT_PA   → playing
//   BT_STOP → paused
//   BT_DISC → disconnected
//   +SRC=BT1/BT2/AUX/USB → aktív forrás
//   AUTO:INFO / AUTO:OFF  → metaadat auto-küldés állapota
//   TIME:ON / TIME:OFF    → időküldés állapota
//
// Automatikus metaadat sorok (lejátszás közben):
//   +TITL=  → dal cím
//   +ARTS=  → előadó
//   +ALBM=  → album
//   +GENR=  → műfaj
//   +NMBR=  → sorszám a listában
//   +TNUM=  → összes szám száma
//   +PYTM=NNNNms → teljes hossz milliszekundumban
//
// Egyéb automatikus események:
//   OK      → AT parancs visszaigazolás
//   ERROR   → AT parancs hiba
// ------------------------------------------------------------------
void BluetoothPlayer::requestBridgeSampleRate(uint32_t sampleRate) {
    if (!isSupportedSampleRate(sampleRate)) return;
    _requestedSampleRate = sampleRate;
}

void BluetoothPlayer::requestStreamInfo(bool force) {
    uint32_t now = millis();
    if (!force && _streamInfoRequested && now - _lastStreamInfoRequestMs < 3000) return;
    _streamInfoRequested = true;
    _lastStreamInfoRequestMs = now;
    sendAT("AT+CODE");
    sendAT("AT+GARATE");
}

void BluetoothPlayer::parseLine(char* line) {
    log_d("##[BT]# RX: %s", line);

    // --- metaadat sorok ---
    if (!strncmp(line, "+TITL=", 6)) { _title  = String(line + 6); _metaUpdated = true; return; }
    if (!strncmp(line, "+ARTS=", 6)) { _artist = String(line + 6); _metaUpdated = true; return; }
    if (!strncmp(line, "+ALBM=", 6)) { _album  = String(line + 6); return; }
    if (!strncmp(line, "+GENR=", 6)) { _genre  = String(line + 6); return; }
    if (!strncmp(line, "+NMBR=", 6)) { _trackNum   = (uint8_t)atoi(line + 6); return; }
    if (!strncmp(line, "+TNUM=", 6)) { _trackTotal = (uint8_t)atoi(line + 6); return; }
    if (!strncmp(line, "+PYTM=", 6)) { _trackMs = (uint32_t)atol(line + 6);   return; }
    if (!strncmp(line, "+PYPS=", 6)) { _posMs   = (uint32_t)atol(line + 6) * 1000; return; } // "19s" → 19000ms // "198405ms" → atol megáll az 'm'-nél

    // --- kapcsolat / lejátszás állapot ---
    if (!strncmp(line, "+Rate1=", 7) || !strncmp(line, "+ARATE=", 7)) {
        uint32_t sampleRate = parseUnsignedValue(line);
        requestBridgeSampleRate(sampleRate);
        log_i("##[BT]# stream info: %s", line);
        return;
    }

    if (!strncmp(line, "+Code1=", 7) || !strncmp(line, "+CODE=", 6)) {
        const char* value = strchr(line, '=');
        if (value && value[1]) { strlcpy(_streamCodec, value + 1, sizeof(_streamCodec)); }
        log_i("##[BT]# stream info: %s", line);
        return;
    }

    if (!strncmp(line, "+ABIT=", 6)) {
        const uint32_t bits = parseUnsignedValue(line);
        if (bits >= 8U && bits <= 32U) { _streamBits = (uint8_t)bits; }
        log_i("##[BT]# stream info: %s", line);
        return;
    }

    if (!strcmp(line, "BT_PA")) {
        _state = BT_STATE_PLAYING;
        setFallbackTitle("Playing");
        requestStreamInfo();
        return;
    }
    if (!strcmp(line, "BT_CN")) {
        _state = BT_STATE_CONNECTED;
        _streamInfoRequested = false;
        return;
    }
    if (!strcmp(line, "BT_STOP")) {
        _state = BT_STATE_PAUSED;
        _streamInfoRequested = false;
        return;
    }
    if (!strcmp(line, "BT_DISC")) {
        _state = BT_STATE_DISC;
        _streamInfoRequested = false;
        _requestedSampleRate = 0;
        clearMeta();
        return;
    }

    // --- forrás váltás (+SRC=BT1 stb.) ---
    if (!strncmp(line, "+SRC=", 5)) {
        _streamInfoRequested = false;
        if (!strcmp(line + 5, "NONE")) _requestedSampleRate = 0;
        log_i("##[BT]# forrás: %s", line + 5);
        return;
    }

    // --- AT+IQ válasz info sorok (nem kritikus, csak logoljuk) ---
    if (!strcmp(line, "OK") || !strcmp(line, "ERROR") ||
        !strncmp(line, "AUTO:", 5) || !strncmp(line, "TIME:", 5)) {
        return; // már a log_d fent kiírta
    }
}

// =====================================================================
//  I2S RX (module -> ESP32) on the second I2S peripheral
// =====================================================================

bool BluetoothPlayer::initI2SRx() {
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_1, BT_I2S_RX_MASTER ? I2S_ROLE_MASTER : I2S_ROLE_SLAVE);
    // The tested QCC5124 breakout drives I2S as master output. The ESP RX side
    // should normally be slave and only sample BCK/LRCK/DATA from the module.
    esp_err_t err = i2s_new_channel(&chanCfg, nullptr, &_rxHandle);
    if (err != ESP_OK) {
        log_e("##[BT][ERROR]# i2s_new_channel rx failed: %d", err);
        return false;
    }

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BT_I2S_SAMPLE_RATE),
#if BT_I2S_FORMAT == 1
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            BT_I2S_DATA_WIDTH,
            I2S_SLOT_MODE_STEREO),
#elif BT_I2S_FORMAT == 2
        .slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(
            BT_I2S_DATA_WIDTH,
            I2S_SLOT_MODE_STEREO),
#else
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            BT_I2S_DATA_WIDTH,
            I2S_SLOT_MODE_STEREO),
#endif
        .gpio_cfg =
            {
//                .mclk = I2S_GPIO_UNUSED,
#if defined(BT_I2S_MCLK) && BT_I2S_USE_MCLK
                .mclk = (gpio_num_t)BT_I2S_MCLK,
#else
                .mclk = I2S_GPIO_UNUSED,
#endif
                .bclk = (gpio_num_t)BT_I2S_BCK,
                .ws = (gpio_num_t)BT_I2S_LRCK,
                .dout = I2S_GPIO_UNUSED,
                .din = (gpio_num_t)BT_I2S_DATA,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = BT_I2S_BCLK_INV,
                        .ws_inv = BT_I2S_WS_INV,
                    },
            },
    };

#if BT_I2S_BITS == 24
    stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
#endif

    log_i("##[BT]# RX config: role=%s rate=%u bits=%u fmt=%u mclk=%u bclkInv=%u wsInv=%u shift=%u",
          BT_I2S_RX_MASTER ? "master" : "slave",
          (unsigned)BT_I2S_SAMPLE_RATE,
          (unsigned)BT_I2S_BITS,
          (unsigned)BT_I2S_FORMAT,
          (unsigned)BT_I2S_USE_MCLK,
          (unsigned)BT_I2S_BCLK_INV,
          (unsigned)BT_I2S_WS_INV,
          (unsigned)BT_I2S_32_SHIFT);

    err = i2s_channel_init_std_mode(_rxHandle, &stdCfg);
    if (err != ESP_OK) {
        log_e("##[BT][ERROR]# i2s_channel_init_std_mode rx failed: %d", err);
        i2s_del_channel(_rxHandle);
        _rxHandle = nullptr;
        return false;
    }
    return true;
}

void BluetoothPlayer::deinitI2SRx() {
    if (!_rxHandle) return;
    i2s_channel_disable(_rxHandle);
    i2s_del_channel(_rxHandle);
    _rxHandle = nullptr;
}

// =====================================================================
//  Bridge task: BT I2S RX -> existing DAC I2S TX (audio.i2sWriteExt)
// =====================================================================

bool BluetoothPlayer::startBridge() {
    if (_bridgeTaskHandle || !_rxHandle) return false;

    // Force the QCC module to the same I2S sample rate as the ESP bridge.
    sendAT("AT+SARATE=CONF");
    delay(50);
    sendAT("AT+CODE");
    sendAT("AT+GARATE");

    // Aktuális kimenet lekérdezése (diagnosztika)
    sendAT("AT+AOUT");

    if (_bridgeTaskHandle || !_rxHandle) return false;

    // A DAC TX csatornát a BT modul I2S órajeléhez (48kHz, lásd initI2SRx)
    // igazítjuk, különben az utoljára streamelt rádió mintavételi rátáján
    // menne ki a hang (rossz hangmagasság/sebesség). setOutputSampleRate()
    // belül reconfigI2S()-t hív, ami disable→reconfig→enable ciklust csinál
    // a TX csatornán. A rövid display DMA guard ezt BT módban is védi anélkül,
    // hogy nyitott SPI tranzakciót tartana az I2S művelet alatt.
    // player.setOutputSampleRate(Audio::SR_48000);  // ← töröld
    // player.i2sEnableExt();                         // ← töröld
    uint32_t sampleRate = _requestedSampleRate;
    if (!isSupportedSampleRate(sampleRate)) sampleRate = BT_I2S_SAMPLE_RATE;
    _bridgeSampleRate = sampleRate;

    player.setSampleRate(sampleRate);
    player.i2sEnableExt();
    spectrumAnalyzer.setSampleRate(sampleRate);

    if (i2s_channel_enable(_rxHandle) != ESP_OK) return false;
    _bridgeStopRequest = false;
    // Core 0 → elkülönül a display tasktól (core 1), prioritás 2 (alacsony)
    xTaskCreatePinnedToCore(bridgeTaskWrapper, "bt_i2s_bridge", 4096, this, 3, &_bridgeTaskHandle, 0);
    return true;
}

void BluetoothPlayer::stopBridge() {
    if (!_bridgeTaskHandle) return;
    _bridgeStopRequest = true;
    // task deletes itself and clears the handle on the way out
    while (_bridgeTaskHandle) { delay(2); }
    if (_rxHandle) i2s_channel_disable(_rxHandle);

    // Vissza az eredeti (forrás szerinti) mintavételi rátára, hogy a
    // következő WEB/SD lejátszás a szokásos módon, felesleges resample
    // nélkül induljon.
    player.setOutputSampleRate(Audio::SR_ORIGIN);
}

void BluetoothPlayer::bridgeTaskWrapper(void* param) { static_cast<BluetoothPlayer*>(param)->bridgeTask(); }

void BluetoothPlayer::bridgeTask() {
    static uint8_t rxBuf[2048];
    static int32_t tx32Buf[2048 / 2];
    static int32_t spectrumBuf[2048 / 2];
    size_t         bytesRead    = 0;
    size_t         bytesWritten = 0;

    uint32_t lastErrorLogMs = 0;

    log_i("##[BT]# bridge task started (core %d, rxBits=%u, rate=%u)",
          xPortGetCoreID(), (unsigned)BT_I2S_BITS, (unsigned)_bridgeSampleRate);

    while (!_bridgeStopRequest) {
        uint32_t requestedRate = _requestedSampleRate;
        if (isSupportedSampleRate(requestedRate) && requestedRate != _bridgeSampleRate) {
            player.setSampleRate(requestedRate);
            spectrumAnalyzer.setSampleRate(requestedRate);
            _bridgeSampleRate = requestedRate;
            log_i("##[BT]# bridge sample rate changed to %u", (unsigned)requestedRate);
        }

        esp_err_t err = i2s_channel_read(_rxHandle, rxBuf, sizeof(rxBuf), &bytesRead, pdMS_TO_TICKS(50));
        if (err == ESP_OK && bytesRead > 0) {
            size_t frames = 0;
            size_t txBytes = 0;
            const uint8_t* txData = nullptr;

            if (BT_I2S_BITS == 24 || BT_I2S_BITS == 32) {
                const int32_t* src = reinterpret_cast<const int32_t*>(rxBuf);
                frames = bytesRead / (2 * sizeof(int32_t));
                size_t samples = frames * 2;
                for (size_t i = 0; i < samples; i++) {
                    int32_t raw = src[i];
                    int16_t s = (int16_t)(raw >> BT_I2S_32_SHIFT);
                    int32_t s32 = (int32_t)s << 16;
                    tx32Buf[i] = s32;
                    spectrumBuf[i] = scaleSpectrumSample(s32);
                }
                txData = reinterpret_cast<const uint8_t*>(tx32Buf);
                txBytes = samples * sizeof(int32_t);
            } else {
                const int16_t* src = reinterpret_cast<const int16_t*>(rxBuf);
                frames = bytesRead / (2 * sizeof(int16_t));
                size_t samples = frames * 2;
                for (size_t i = 0; i < samples; i++) {
                    int16_t s = src[i];
                    int32_t s32 = (int32_t)s << 16;
                    tx32Buf[i] = s32;
                    spectrumBuf[i] = scaleSpectrumSample(s32);
                }
                txData = reinterpret_cast<const uint8_t*>(tx32Buf);
                txBytes = samples * sizeof(int32_t);
            }

            bool ok = player.i2sWriteExt(txData, txBytes, &bytesWritten, 50);
            if ((!ok || bytesWritten != txBytes) && millis() - lastErrorLogMs >= 2000) {
                log_w("##[BT]# I2S TX write failed: expected=%u written=%u",
                      (unsigned)txBytes, (unsigned)bytesWritten);
                lastErrorLogMs = millis();
            }

            spectrumAnalyzer.pushSamples(spectrumBuf, (int16_t)frames);
        } else if (err != ESP_OK && err != ESP_ERR_TIMEOUT && millis() - lastErrorLogMs >= 2000) {
            log_w("##[BT]# I2S RX read failed: %d", err);
            lastErrorLogMs = millis();
        }
    }

    _bridgeTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

#endif // USE_BLUETOOTH
