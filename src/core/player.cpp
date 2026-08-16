#include "options.h"
#include "player.h"
#include "config.h"
#include "display.h"
#include "sdmanager.h"
#include "netserver.h"
#include "timekeeper.h"
#ifdef USE_LASTFM_COVER
  #include "coverart.h"
#endif
#ifdef USE_DLNA
  #include "../dlna/dlna_http_guard.h"
#endif
#include <esp_heap_caps.h>
#include "../displays/tools/language.h"
#include "../pluginsManager/pluginsManager.h"
#ifdef USE_NEXTION
  #include "../displays/nextion.h"
#endif
Player player;
QueueHandle_t playerQueue;

namespace {
constexpr uint8_t VOLUME_CURVE_POINTS = 22;
constexpr float   VOLUME_CURVE_MIN_DB = -60.0f;
constexpr float   VOLUME_CURVE_MAX_DB =   0.0f;
constexpr uint8_t I2S_VOLUME_STEPS = 21;
constexpr int     LEGACY_VOLUME_MAX = 254;
constexpr int     LEGACY_OVOL_LIMIT = 64;

float g_volumeCurveDbLut[VOLUME_CURVE_POINTS] = {
  -60.0f, -52.0f, -39.0f, -32.0f, -27.0f, -24.0f, -20.0f, -18.0f, -15.0f, -13.0f, -12.0f,
  -10.0f, -9.0f, -8.0f, -6.0f, -5.0f, -4.0f, -4.0f, -3.0f, -2.0f, -2.0f, -1.0f};
float g_stationVolumeScale = 1.0f;

uint16_t g_lastPlayStation = 0;
uint32_t g_lastPlayCommandMs = 0;

constexpr uint32_t DUPLICATE_PLAY_GUARD_MS = 1500;

float clampCurveDb(float db) {
  if (db < VOLUME_CURVE_MIN_DB) return VOLUME_CURVE_MIN_DB;
  if (db > VOLUME_CURVE_MAX_DB) return VOLUME_CURVE_MAX_DB;
  return db;
}

void clampCurveLut(float* lut, size_t n) {
  if (!lut || n == 0) return;
  for (size_t i = 0; i < n; ++i) { lut[i] = clampCurveDb(lut[i]); }
}

float playerVolumeCurveDb(float t) {
  t *= g_stationVolumeScale;
  if (t <= 0.0f) return g_volumeCurveDbLut[0];
  if (t >= 1.0f) return g_volumeCurveDbLut[VOLUME_CURVE_POINTS - 1];
  float pos = t * (float)(VOLUME_CURVE_POINTS - 1);
  uint8_t i = (uint8_t)pos;
  if (i >= VOLUME_CURVE_POINTS - 1) return g_volumeCurveDbLut[VOLUME_CURVE_POINTS - 1];
  float frac = pos - (float)i;
  return g_volumeCurveDbLut[i] + (g_volumeCurveDbLut[i + 1] - g_volumeCurveDbLut[i]) * frac;
}
} // namespace

  #if !I2S_INTERNAL
Player::Player() : _audioClientMutex(nullptr) {}
  #else
Player::Player() : Audio(true, I2S_DAC_CHANNEL_BOTH_EN), _audioClientMutex(nullptr) {}
  #endif
//#endif

void Player::init() {
  Serial.print("##[BOOT]#\tplayer.init\t");
  if (_audioClientMutex == nullptr) {
    _audioClientMutex = xSemaphoreCreateMutex();
    if (_audioClientMutex == nullptr) {
      log_e("##[PLAYER]# audio client mutex allocation failed");
    }
  }
  playerQueue = NULL;
  _resumeFilePos = 0;
  _hasError = false;
  _transitionInProgress = false;
  _playRequested = false;
  playerQueue = xQueueCreate(5, sizeof(playerRequestParams_t));
  if (MUTE_PIN != 255) {
    pinMode(MUTE_PIN, OUTPUT);
  }
  setOutputPins(false);
  delay(50);
#ifdef MQTT_ROOT_TOPIC
  memset(burl, 0, MQTT_BURL_SIZE);
#endif
  // FusionEdge: reduce I2S DMA buffer footprint in internal DRAM.
  // Default DMA_DESC_NUM(32) * DMA_FRAME_NUM(256) * 4 bytes/frame = 32768
  // bytes allocated from MALLOC_CAP_DMA internal RAM during setPinout()
  // below. This fragments the internal heap enough that the largest
  // contiguous free block drops to ~31.7KB, which is too small for the
  // mbedtls TLS handshake buffers (HTTPS streams fail with -32512
  // "SSL - Memory allocation failed"). 12*256*4 = 12288 bytes frees up
  // ~20KB of contiguous internal DRAM; audio buffering margin is still
  // ~64ms @ 48kHz stereo, which is ample for I2S underrun protection.
  settings.DMA_DESC_NUM = 12;
#if I2S_DOUT != 255
  #if !I2S_INTERNAL
  setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
  #endif
#endif
  setBalance(-config.store.balance);  // "audio_change"   -16 to 16 fordítás 16 to -16
  setTone(config.store.bass, config.store.middle, config.store.trebble);
  setVolumeSteps(21);
  setVolume(0, 0);
  // Volume curve alkalmazása a config-ból
  for (uint8_t i = 1; i <= 21; ++i) {
    int db = (int)config.store.volumeCurveDb[i - 1];
    if (db < -60) db = -60;
    if (db > 0)   db = 0;
    g_volumeCurveDbLut[i] = (float)db;
  }
  clampCurveLut(g_volumeCurveDbLut, VOLUME_CURVE_POINTS);
  setVolumeCurve(playerVolumeCurveDb);
  _status = STOPPED;
  _volTimer = false;
//randomSeed(analogRead(0));
#if PLAYER_FORCE_MONO
  forceMono(true);
#endif
  _loadVol();
  setConnectionTimeout(CONNECTION_TIMEOUT, CONNECTION_TIMEOUT_SSL);
  Serial.println("done");
}

/* ----- Auto On-Off Timer: percenkénti időzített start/stop ----- */
void Player::checkAutoStartStop() {
    static int lastCheckedMinute = -1;
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int curMinute = tm_now.tm_hour * 60 + tm_now.tm_min;

    if (curMinute == lastCheckedMinute) return; // percenként egyszer
    // Csak a perc első 20 másodpercében tüzelünk, hogy elkerüljük a korai/késői triggert
    if (tm_now.tm_sec > 20) return;
    lastCheckedMinute = curMinute;

    auto timeToMinutes = [](const char* hhmm) -> int {
        if (!hhmm || strlen(hhmm) != 5 || hhmm[2] != ':') return -1;
        int h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
        int m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');
        if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
        return h * 60 + m;
    };

    int startMinute = timeToMinutes(config.store.autoStartTime);
    int stopMinute  = timeToMinutes(config.store.autoStopTime);

    if (startMinute >= 0 && startMinute == curMinute && !isRunning()) {
        display.putRequest(NEWMODE, PLAYER);
        sendCommand({PR_PLAY, (int)config.lastStation()});
    }
    if (stopMinute >= 0 && stopMinute == curMinute && isRunning()) {
        sendCommand({PR_STOP, 0});
    }
}
/* ----- Auto On-Off Timer ----- */

void Player::sendCommand(playerRequestParams_t request) {
  if (playerQueue == NULL) {
    return;
  }
  xQueueSend(playerQueue, &request, PLQ_SEND_DELAY);
}

bool Player::sendCommandNoWait(playerRequestParams_t request) {
  if (playerQueue == NULL) {
    return false;
  }
  return xQueueSend(playerQueue, &request, 0) == pdTRUE;
}

bool Player::readyForWebStation() const {
  return playerQueue != NULL && !_transitionInProgress && uxQueueMessagesWaiting(playerQueue) == 0;
}

void Player::resetQueue() {
  if (playerQueue != NULL) {
    xQueueReset(playerQueue);
  }
}

void Player::stopInfo() {
  config.setSmartStart(0);
  netserver.requestOnChange(MODE, 0);
}

void Player::setError() {
  _hasError = true;
  config.setTitle(config.tmpBuf);
  Serial.printf("##ERROR#:\t%s\r\n", config.tmpBuf);
}

void Player::setError(const char *e) {
  strlcpy(config.tmpBuf, e, sizeof(config.tmpBuf));
  setError();
}

/* Ha az alreadyStopped true akkor a STOP művelet már le lett kezelve.*/
void Player::_stop(bool alreadyStopped) {
  log_i("%s called", __func__);
  acceptStreamMeta = false;
  if (config.getMode() == PM_SDCARD && !alreadyStopped) {
    config.sdResumePos = player.getAudioFilePosition();
    config.stopedSdStationId = config.lastStation();
  }
  _status = STOPPED;
  setOutputPins(false);
  if (!_hasError) {
    config.setTitle((display.mode() == LOST || display.mode() == UPDATING) ? "" : LANG::const_PlStopped);
  }
  config.station.bitrate = 0;
  config.setBitrateFormat(BF_UNKNOWN);
#ifdef USE_NEXTION
  nextion.bitrate(config.station.bitrate);
#endif
  if (!alreadyStopped) {
    stopSong();
  }
  netserver.requestOnChange(BITRATE, 0);
  display.putRequest(DBITRATE);
  display.putRequest(PSTOP);
  if (!lockOutput) {
    stopInfo();
  }
  if (player_on_stop_play) {
    player_on_stop_play();
  }
  pm.on_stop_play();
}

void Player::initHeaders(const char *file) {
  if (strlen(file) == 0 || true) {
    return;  //TODO Read TAGs
  }
  connecttoFS(sdman, file);
  //eofHeader = false; // megszűnt az Audio.h ban.
  //while(!eofHeader) Audio::loop(); // megszűnt az Audio.h ban.
  //netserver.requestOnChange(SDPOS, 0);
  //setDefaults(); // free buffers and set defaults átkerült a privat függvényekhez az Audio.h ban.
}
void resetPlayer() {
  if (!config.store.watchdog) {
    return;
  }
  player.resetQueue();
  player.sendCommand({PR_STOP, 0});
  player.loop();
}

#ifndef PL_QUEUE_TICKS
  #define PL_QUEUE_TICKS 0
#endif
#ifndef PL_QUEUE_TICKS_ST
  #define PL_QUEUE_TICKS_ST 15
#endif
void Player::loop() {
  if (playerQueue == NULL) {
    return;
  }
  if (_audioClientMutex != nullptr) {
    xSemaphoreTake(_audioClientMutex, portMAX_DELAY);
  }
  playerRequestParams_t requestP;
  if (xQueueReceive(playerQueue, &requestP, isRunning() ? PL_QUEUE_TICKS : PL_QUEUE_TICKS_ST)) {
    switch (requestP.type) {
      case PR_STOP:
      {
        _transitionInProgress = true;
        playerRequestParams_t peek;
        if (xQueuePeek(playerQueue, &peek, 0) == pdTRUE && peek.type == PR_PLAY) {
          acceptStreamMeta = false;
          if (config.getMode() == PM_SDCARD) {
            config.sdResumePos = player.getAudioFilePosition();
            config.stopedSdStationId = config.lastStation();
          }
          _status = STOPPED;
          setOutputPins(false);
          stopSong();
          _transitionInProgress = false;
          break;
        }
        _playRequested = false;
        _stop();
        _transitionInProgress = false;
        break;
      }
      case PR_PLAY:
      {
        _transitionInProgress = true;
        _playRequested = true;
        uint16_t st = (uint16_t)abs(requestP.payload);

        // Ha gyorsan egymás után több PR_PLAY parancs érkezik, csak a legutolsót
        // hajtsuk végre – ez megelőzi a fehér/invertált képernyőt, ami az SPI
        // busz versengéséből adódik az audio driver újraindítása közben.
        {
          playerRequestParams_t peek;
          while (xQueuePeek(playerQueue, &peek, 0) == pdTRUE && peek.type == PR_PLAY) {
            xQueueReceive(playerQueue, &requestP, 0);
            st = (uint16_t)abs(requestP.payload);
          }
        }

        const uint32_t nowMs = millis();
        if (st == g_lastPlayStation && (uint32_t)(nowMs - g_lastPlayCommandMs) < DUPLICATE_PLAY_GUARD_MS) {
          _transitionInProgress = false;
          break;
        }
        g_lastPlayStation = st;
        g_lastPlayCommandMs = nowMs;

#ifdef USE_DLNA
        if (config.store.playlistSource == PL_SRC_DLNA) {
          config.store.lastDlnaStation = st;
          config.saveValue(&config.store.lastDlnaStation, (uint16_t)st);
          config.sdResumePos = 0;
          config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_DLNA);
          _play(st);
          netserver.requestOnChange(GETINDEX, 0);
          _transitionInProgress = false;
          break;
        }
#endif

        // ==== EREDETI VISSELKEDÉS (WEB / SD) ====
        if (requestP.payload > 0) {
          config.setLastStation(st);
        }
#ifdef USE_DLNA
        config.saveValue(&config.store.lastPlayedSource, (uint8_t)PL_SRC_WEB);
#endif
        _play(st);
        if (player_on_station_change) {
          player_on_station_change();
        }
        pm.on_station_change();
        _transitionInProgress = false;
        break;
      }
      case PR_TOGGLE:
      {
        toggle();
        break;
      }
      case PR_VOL:
      {
        config.setVolume(requestP.payload);
        Audio::setVolume(volToI2S(requestP.payload), 0);
        break;
      }
      #ifdef USE_SD
      case PR_CHECKSD:
      {
        if (config.getMode() == PM_SDCARD) {
          display.waitDMA();
          if (!sdman.cardPresent()) {
            sdman.stop();
            config.changeMode(PM_WEB);
          }
        }
        break;
      }
#endif
      case PR_BURL:
      {
#ifdef MQTT_ROOT_TOPIC
        if (strlen(burl) > 0) {
          browseUrl();
        }
#endif
        break;
      }
      case PR_URL:
      {
        _transitionInProgress = true;
        _playRequested = true;
        // Play arbitrary URL (presets)
        _hasError = false;
        acceptStreamMeta = false;
        remoteStationName = false;
        config.setDspOn(1);

        if (_status == PLAYING) {
          _stop();
        }

        // Set station label for UI
        if (_nameBuf[0] != '\0') {
          strlcpy(config.station.name, _nameBuf, sizeof(config.station.name));
        } else {
          strlcpy(config.station.name, "URL", sizeof(config.station.name));
        }
        // Preserve the original preset name as the stable map.csv key. ICY
        // metadata may replace station.name later without losing its icon.
        const char* iconName = config.station.name[0] == '.'
                                 ? config.station.name + 1
                                 : config.station.name;
        strlcpy(config.station.iconName, iconName, sizeof(config.station.iconName));
        config.station.genre[0] = '\0';
        strlcpy(config.station.url, _urlBuf, sizeof(config.station.url));
        config.station.title[0] = '\0';

        // Always WEB mode
        config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));

        display.putRequest(PSTOP);
        setOutputPins(false);
        config.setTitle(LANG::const_PlConnect);

        if (_connectToHostProtected(config.station.url)) {
          _status = PLAYING;
          config.setTitle("");
          netserver.requestOnChange(MODE, 0);
          setOutputPins(true);
          display.putRequest(PSTART);
          acceptStreamMeta = true;
          if (player_on_start_play) {
            player_on_start_play();
          }
          pm.on_start_play();
        } else {
          Serial.printf("##ERROR#:	Error connecting to %.128s\n", config.station.url);
          snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", config.station.url);
          setError();
          _stop(true);
        }
        _transitionInProgress = false;
        break;
      }
      default: break;
    }
  }
  // SD módban a megosztott SPI buszon az LGFX DMA transzfert be kell fejezni
  // mielőtt az Audio::loop() SD olvasást végez (csíkozás / kép szétesés megelőzése)
  if (config.getMode() == PM_SDCARD) display.waitDMA();
  Audio::loop();
  checkAutoStartStop(); /* ----- Auto On-Off Timer ----- */

  // FusionEdge: connecttohost() succeeded (so PR_URL/_play reported PLAYING),
  // but the stream's response had no usable codec/playlist and Audio quietly
  // stopped itself. Surface this as a normal player error instead of staying
  // silently "PLAYING" with an empty buffer.
  if (config.getMode() == PM_WEB && _status == PLAYING && !_hasError && !isRunning() && hasUnsupportedContent()) {
    Serial.printf("##ERROR#:\tUnsupported stream content from %.128s\n", config.station.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Unsupported stream content from %.128s", config.station.url);
    setError();
    _stop(true);
  }

#ifdef USE_SD
if (
  config.getMode() == PM_SDCARD &&
  !isRunning() &&
  _status == PLAYING &&
  player.getAudioFilePosition() == 0
) {
  Serial.println("[SD] EOF -> next()");
  next();
  if (_audioClientMutex != nullptr) {
    xSemaphoreGive(_audioClientMutex);
  }
  return;
}
#endif

  if (_volTimer) {
    if ((millis() - _volTicks) > 3000) {
      config.saveVolume();
      _volTimer = false;
    }
  }
  /*
#ifdef MQTT_ROOT_TOPIC
  if(strlen(burl)>0){
    browseUrl();
  }
#endif*/
  if (_audioClientMutex != nullptr) {
    xSemaphoreGive(_audioClientMutex);
  }
}

bool Player::connecttospeech(const char* speech, const char* lang) {
  if (_audioClientMutex == nullptr) {
    return Audio::connecttospeech(speech, lang);
  }

  const uint32_t waitStarted = millis();
  xSemaphoreTake(_audioClientMutex, portMAX_DELAY);
  const uint32_t waitedMs = millis() - waitStarted;
  if (waitedMs >= 10) {
    log_i("##[TTSDIAG]# waited %lu ms for audio client", (unsigned long)waitedMs);
  }
#ifdef USE_LASTFM_COVER
  const bool coverIdle = coverArt.pauseNetwork();
#else
  constexpr bool coverIdle = true;
#endif
  bool connected = false;
  if (coverIdle) {
#ifdef USE_DLNA
    DlnaHttpGuard networkLock;
#endif
    connected = Audio::connecttospeech(speech, lang);
  }
#ifdef USE_LASTFM_COVER
  coverArt.resumeNetwork();
#endif
  xSemaphoreGive(_audioClientMutex);
  return connected;
}

bool Player::_connectToHostProtected(const char* url) {
#ifdef USE_LASTFM_COVER
  const bool coverIdle = coverArt.pauseNetwork();
#else
  constexpr bool coverIdle = true;
#endif
  bool connected = false;
  if (coverIdle) {
#ifdef USE_DLNA
    DlnaHttpGuard networkLock;
#endif
    // Let destroyed HTTPClient/NetworkClient objects coalesce their internal
    // heap blocks before the TLS handshake asks for a large contiguous block.
    vTaskDelay(1);
    const size_t largestBefore =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    connected = Audio::connecttohost(url);
    log_i("##[PLAYER]# connect result=%d internalLargest=%u->%u", connected,
          static_cast<unsigned>(largestBefore),
          static_cast<unsigned>(
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  } else {
    log_w("##[PLAYER]# connect deferred: cover network did not stop");
  }
#ifdef USE_LASTFM_COVER
  coverArt.resumeNetwork();
#endif
  return connected;
}

void Player::setOutputPins(bool isPlaying) {
  if (REAL_LEDBUILTIN != 255) {
    digitalWrite(REAL_LEDBUILTIN, LED_INVERT ? !isPlaying : isPlaying);
  }
  bool _ml = MUTE_LOCK ? !MUTE_VAL : (isPlaying ? !MUTE_VAL : MUTE_VAL);
  if (MUTE_PIN != 255) {
    digitalWrite(MUTE_PIN, _ml);
  }
}

void Player::playUrl(const char *url, const char *name) {
  if (!url || url[0] == '\0') {
    return;
  }
  // Copy into buffers (used by PR_URL in player task)
  strlcpy(_urlBuf, url, sizeof(_urlBuf));
  if (name && name[0] != '\0') {
    strlcpy(_nameBuf, name, sizeof(_nameBuf));
  } else {
    _nameBuf[0] = '\0';
  }
  sendCommand({PR_URL, 0});
}

void Player::_play(uint16_t stationId) {
  _hasError = false;
  acceptStreamMeta = false;
  _status = STOPPED;
  setOutputPins(false);
  remoteStationName = false;
  // Kijelző + metaadat alaphelyzet
  if (!config.prepareForPlaying(stationId)) {
    return;
  }
  _loadVol();
  bool isConnected = false;
  // ----- SD MODE -----
  if (config.getMode() == PM_SDCARD && SDC_CS != 255) {
    display.waitDMA(); // DMA befejezése SD fájl megnyitás előtt
    // A connecttoFS NEM támogat start offsetet SD-n → -1, indítás pozícionálás nélkül.
    isConnected = connecttoFS(sdman, config.station.url, -1);
  } else {
#ifdef USE_DLNA //DLNA mod
  // DLNA is WEB engine, de nem írjuk felül a mode-ot
    if (config.store.playlistSource != PL_SRC_DLNA)
#endif
    {
      config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));
    }
  }
  if (config.getMode() == PM_WEB) {
    isConnected = _connectToHostProtected(config.station.url);
  }
  connproc = true;
  // ----- START PLAYING -----
  if (isConnected) {
    _status = PLAYING;
    config.configPostPlaying(stationId);
    acceptStreamMeta = true;
    setOutputPins(true);
    if (player_on_start_play) player_on_start_play();
    pm.on_start_play();
  } else {
    Serial.printf("##ERROR#:\tError connecting to %.128s\n", config.station.url);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", config.station.url);
    setError();
    _stop(true);
  }
}

#ifdef MQTT_ROOT_TOPIC
void Player::browseUrl() {
  _hasError = false;
  acceptStreamMeta = false;
  remoteStationName = true;
  config.setDspOn(1);
  resumeAfterUrl = _status == PLAYING;
  display.putRequest(PSTOP);
  setOutputPins(false);
  config.setTitle(LANG::const_PlConnect);
  if (_connectToHostProtected(burl)) {
    _status = PLAYING;
    config.setTitle("");
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    display.putRequest(PSTART);
    acceptStreamMeta = true;
    if (player_on_start_play) {
      player_on_start_play();
    }
    pm.on_start_play();
  } else {
    Serial.printf("##ERROR#:\tError connecting to %.128s\r\n", burl);
    snprintf(config.tmpBuf, sizeof(config.tmpBuf), "Error connecting to %.128s", burl);
    setError();
    _stop(true);
  }
  //memset(burl, 0, MQTT_BURL_SIZE);
}
#endif

void Player::prev() {
  uint16_t lastStation = config.lastStation();
  if (config.getMode() != PM_SDCARD) {
    // WEB + DLNA: always sequential wrap
    if (lastStation <= 1) config.lastStation(config.playlistLength());
    else config.lastStation(lastStation - 1);
  } else {
    // SD: sequential unless snuffle enabled (SD-only feature)
    if (!config.store.sdsnuffle) {
      if (lastStation <= 1) config.lastStation(config.playlistLength());
      else config.lastStation(lastStation - 1);
    } else {
      config.lastStation(random(1, config.playlistLength() + 1));
    }
  }
  config.stopedSdStationId = -1;  // Reseteli a seek hez mentett SD fájl sorszámát.
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::next() {
  uint16_t lastStation = config.lastStation();
  if (config.getMode() != PM_SDCARD) {
    // WEB + DLNA: always sequential wrap
    if (lastStation >= config.playlistLength()) config.lastStation(1);
    else config.lastStation(lastStation + 1);
  } else {
    // SD: sequential unless snuffle enabled
    if (!config.store.sdsnuffle) {
      if (lastStation >= config.playlistLength()) config.lastStation(1);
      else config.lastStation(lastStation + 1);
    } else {
      config.lastStation(random(1, config.playlistLength() + 1));
    }
  }
  config.stopedSdStationId = -1;  // Reseteli a seek hez mentett SD fájl sorszámát.
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::toggle() {
  if (_status == PLAYING) {
    sendCommand({PR_STOP, 0});
  } else {
    sendCommand({PR_PLAY, config.lastStation()});
  }
}

void Player::stepVol(bool up) {
  if (up) {
    if (config.store.volume <= 20) {
      setVol(config.store.volume + 1);
    } else {
      setVol(21);
    }
  } else {
    if (config.store.volume >= 1) {
      setVol(config.store.volume - 1);
    } else {
      setVol(0);
    }
  }
}

/* Preserve the legacy 0..254 OVOL meaning while Audio uses 21 volume steps.
   Scaling the curve position keeps small corrections between integer steps. */
uint8_t Player::volToI2S(uint8_t volume) {
  int ovol = config.station.ovol;
  if (ovol > LEGACY_OVOL_LIMIT) ovol = LEGACY_OVOL_LIMIT;
  if (ovol < -LEGACY_OVOL_LIMIT) ovol = -LEGACY_OVOL_LIMIT;

  const int legacyRange = LEGACY_VOLUME_MAX - ovol * 2;
  g_stationVolumeScale = (float)LEGACY_VOLUME_MAX / (float)legacyRange;
  return volume > I2S_VOLUME_STEPS ? I2S_VOLUME_STEPS : volume;
}

void Player::_loadVol() {
  const uint8_t baseVolume = volToI2S(config.store.volume);
  const float effectiveVolume = fminf(
      (float)I2S_VOLUME_STEPS, (float)baseVolume * g_stationVolumeScale);
  setVolume(baseVolume, 0);
}

void Player::setVol(uint8_t volume) {
  _volTicks = millis();
  _volTimer = true;
  player.sendCommand({PR_VOL, volume});
}

// A WEB UI a hangszínt -16 - +16 között adja, de az Audio osztály setTone()
// függvénye -40 és +6 (dB) közötti értéket kér, ezért mepelni kell.
int8_t Player::uiToDb(int8_t uiVal) {
  if (uiVal == 0) {
    return 0;
  }
  if (uiVal > 0) {
    // 0..+16  →  0..+6 dB
    float db = (uiVal / 16.0f) * 6.0f;
    return (int8_t)roundf(db);
  } else {
    // -16..0  →  -20..0 dB
    float db = (uiVal / 16.0f) * 20.0f;  // uiVal negatív!
    return (int8_t)roundf(db);
  }
}

void Player::setTone(int8_t bass, int8_t mid, int8_t treble) {
  // Serial.printf("EQ UI: %d %d %d  →  DSP: %d %d %d\n", bass, mid, treble, uiToDb(bass), uiToDb(mid), uiToDb(treble));
  Audio::setTone(uiToDb(bass), uiToDb(mid), uiToDb(treble));
}

void Player::setVolumeCurveDbLut(const float *dbValues, size_t count) {
  if (!dbValues || count != VOLUME_CURVE_POINTS) return;
  for (size_t i = 0; i < VOLUME_CURVE_POINTS; ++i) {
    g_volumeCurveDbLut[i] = dbValues[i];
  }
  clampCurveLut(g_volumeCurveDbLut, VOLUME_CURVE_POINTS);
  setVolumeCurve(playerVolumeCurveDb);
}

void Player::setVolumeCurveDbPoint(uint8_t index, float dbValue) {
  if (index >= VOLUME_CURVE_POINTS) return;
  g_volumeCurveDbLut[index] = dbValue;
  clampCurveLut(g_volumeCurveDbLut, VOLUME_CURVE_POINTS);
  setVolumeCurve(playerVolumeCurveDb);
}

float Player::getVolumeCurveDbPoint(uint8_t index) const {
  if (index >= VOLUME_CURVE_POINTS) return 0.0f;
  return g_volumeCurveDbLut[index];
}

void Player::resetVolumeCurveDbLut() {
  static const float kDefault[VOLUME_CURVE_POINTS] = {
      -60.0f, -52.0f, -39.0f, -32.0f, -27.0f, -24.0f, -20.0f, -18.0f, -15.0f, -13.0f, -12.0f,
      -10.0f, -9.0f, -8.0f, -6.0f, -5.0f, -4.0f, -4.0f, -3.0f, -2.0f, -2.0f, -1.0f};
  setVolumeCurveDbLut(kDefault, VOLUME_CURVE_POINTS);
}
