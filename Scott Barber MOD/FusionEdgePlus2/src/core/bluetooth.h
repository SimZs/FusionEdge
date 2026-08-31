#pragma once
#include "options.h"

#ifdef USE_BLUETOOTH
/*
  FusionEdge - Bluetooth (QCC5124EL) AT driver + I2S RX bridge
  --------------------------------------------------------------
  UART: HardwareSerial(2) on BT_UART_TX / BT_UART_RX (myoptions.h)
  I2S:  Second peripheral (I2S_NUM_1) RX-only, bridge task -> DAC TX
*/
#include <Arduino.h>
#include <driver/i2s_std.h>

enum btState_e : uint8_t {
    BT_STATE_UNKNOWN    = 0,
    BT_STATE_CONNECTED  = 1,  // BT_CN
    BT_STATE_PLAYING    = 2,  // BT_PA
    BT_STATE_PAUSED     = 3,  // BT_STOP
    BT_STATE_DISC       = 4,  // BT_DISC - disconnected
};

class BluetoothPlayer {
  public:
    void begin();
    void end();
    void loop();

    // --- transport controls ---
    void play();
    void pause();
    void toggle();
    void next();
    void prev();
    void volUp();
    void volDown();
    void setVol(uint8_t step0to32);

    // --- status / metadata ---
    bool       connected()  const {
        return _state == BT_STATE_CONNECTED || _state == BT_STATE_PLAYING ||
               _state == BT_STATE_PAUSED;
    }
    bool       playing()    const { return _state == BT_STATE_PLAYING; }
    btState_e  state()      const { return _state; }
    String     title()      const { return _title; }
    String     artist()     const { return _artist; }
    String     album()      const { return _album; }
    String     genre()      const { return _genre; }
    uint8_t    trackNum()   const { return _trackNum; }
    uint8_t    trackTotal() const { return _trackTotal; }
    uint32_t   trackMs()    const { return _trackMs; }
    uint32_t   posMs()      const { return _posMs; }
    const char* streamCodec() const { return _streamCodec; }
    uint32_t   streamSampleRate() const { return _bridgeSampleRate ? _bridgeSampleRate : _requestedSampleRate; }
    uint8_t    streamBits() const { return _streamBits; }

    // --- I2S RX bridge ---
    bool startBridge();
    void stopBridge();
    bool bridgeRunning() const { return _bridgeTaskHandle != nullptr; }

  private:
    void sendAT(const char* cmd);
    void parseLine(char* line);
    bool initI2SRx();
    void deinitI2SRx();
    void clearMeta();
    void setFallbackTitle(const char* title);
    void requestBridgeSampleRate(uint32_t sampleRate);
    void requestStreamInfo(bool force = false);

    static void bridgeTaskWrapper(void* param);
    void        bridgeTask();

    HardwareSerial _uart{2};
    char           _lineBuf[256] = {0};
    uint16_t       _lineLen = 0;

    btState_e _state      = BT_STATE_UNKNOWN;
    String    _title;
    String    _artist;
    String    _album;
    String    _genre;
    uint8_t   _trackNum   = 0;
    uint8_t   _trackTotal = 0;
    uint32_t  _trackMs    = 0;
    uint32_t  _posMs      = 0;  // aktuális lejátszási pozíció (+PYPS=Ns → másodpercből ms)
    bool      _metaUpdated = false; // igaz, ha a loop()-ban új +TITL/ARTS érkezett
    bool      _streamInfoRequested = false;
    uint32_t  _lastStreamInfoRequestMs = 0;
    char      _streamCodec[16] = {};
    uint8_t   _streamBits = 0;

    i2s_chan_handle_t _rxHandle          = nullptr;
    TaskHandle_t      _bridgeTaskHandle  = nullptr;
    volatile bool     _bridgeStopRequest = false;
    volatile uint32_t _requestedSampleRate = 0;
    uint32_t          _bridgeSampleRate = 0;
};

extern BluetoothPlayer bluetooth;
#endif
