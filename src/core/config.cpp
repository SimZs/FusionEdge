#include "options.h"
#include <Wire.h>
#include <algorithm>
#include "config.h"
#include "display.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#include "controls.h"
#include "timekeeper.h"
#ifdef USE_LASTFM_COVER
#    include "coverart.h"
#endif
#include "rtcsupport.h"
#include "../displays/tools/language.h"
#include "driver/rtc_io.h"
#ifdef USE_BLUETOOTH
#    include "bluetooth.h"
#endif
#ifdef USE_SD
#    include "sdmanager.h"
#endif
#ifdef USE_NEXTION
#    include "../displays/nextion.h"
#endif
#include <cstddef>
#include <cctype>

#if DSP_MODEL == DSP_DUMMY
#    define DUMMYDISPLAY
#endif

Config config;
#if IR_PIN != 255
QueueHandle_t irQueue = nullptr;
#endif

namespace {

bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) { return false; }
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) { return false; }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

char* trimInPlace(char* s) {
    if (!s) { return s; }
    while (*s && isspace(static_cast<unsigned char>(*s))) { ++s; }
    if (*s == '\0') { return s; }
    char* end = s + strlen(s) - 1;
    while (end > s && isspace(static_cast<unsigned char>(*end))) {
        *end = '\0';
        --end;
    }
    return s;
}

void color565ToRgb(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
    g = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
    b = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
}

uint8_t clampChannel(long value) {
    if (value < 0) { return 0; }
    if (value > 255) { return 255; }
    return static_cast<uint8_t>(value);
}

void parseVersionTriplet(const char* ver, uint8_t& a, uint8_t& b, uint8_t& c) {
    a = 0;
    b = 0;
    c = 0;
    if (!ver || !*ver) { return; }

    char tmp[32];
    strlcpy(tmp, ver, sizeof(tmp));

    char* saveptr = nullptr;
    char* tok = strtok_r(tmp, ".", &saveptr);
    if (!tok) { return; }
    a = clampChannel(strtol(tok, nullptr, 10));

    tok = strtok_r(nullptr, ".", &saveptr);
    if (!tok) { return; }
    b = clampChannel(strtol(tok, nullptr, 10));

    tok = strtok_r(nullptr, ".", &saveptr);
    if (!tok) { return; }
    c = clampChannel(strtol(tok, nullptr, 10));
}

constexpr int8_t kDefaultVolumeCurveDb[21] = {
    -52, -39, -32, -27, -24, -20, -18, -15, -13, -12, -10,
    -9, -8, -6, -5, -4, -4, -3, -2, -2, -1};

bool isVolumeCurveInvalid(const config_t& s) {
    bool allMinusOne = true;
    bool allZero = true;
    for (size_t i = 0; i < 21; ++i) {
        const int8_t v = s.volumeCurveDb[i];
        if (v != -1) { allMinusOne = false; }
        if (v != 0) { allZero = false; }
        if (v < -60 || v > 0) { return true; }
    }
    return allMinusOne || allZero;
}

} // namespace

void u8fix(char* src) { // Ha az utolsó tőbbájtos karakter (ékezetes) utolsó bájtja hiányzik akkor az elejét levágja.
    if (!src) { return; }
    const size_t len = strlen(src);
    if (len == 0) { return; }
    char last = src[len - 1];
    if ((uint8_t)last >= 0xC2) { src[len - 1] = '\0'; }
}

bool Config::_isFSempty() {
    // Base names without .gz — accepts both compressed and plain uploads
    const char*   reqiredFiles[] = {"dragpl.js",   "ir.css",    "irrecord.html", "ir.js",        "logo.svg",      "options.html",
                                    "player.html", "script.js", "style.css",     "updform.html", "theme.css",     "theme-editor.html",
                                    "volcurve.html"};
    const uint8_t reqiredFilesSize = 13;
    char          fullpath[32];
    if (LittleFS.exists("/www/settings.html")) { LittleFS.remove("/www/settings.html"); }
    if (LittleFS.exists("/www/update.html")) { LittleFS.remove("/www/update.html"); }
    if (LittleFS.exists("/www/index.html")) { LittleFS.remove("/www/index.html"); }
    if (LittleFS.exists("/www/ir.html")) { LittleFS.remove("/www/ir.html"); }
    if (LittleFS.exists("/www/elogo.png")) { LittleFS.remove("/www/elogo.png"); }
    if (LittleFS.exists("/www/elogo84.png")) { LittleFS.remove("/www/elogo84.png"); }
    for (uint8_t i = 0; i < reqiredFilesSize; i++) {
        snprintf(fullpath, sizeof(fullpath), "/www/%s", reqiredFiles[i]);
        bool plain = LittleFS.exists(fullpath);
        snprintf(fullpath, sizeof(fullpath), "/www/%s.gz", reqiredFiles[i]);
        bool gz = LittleFS.exists(fullpath);
        if (!plain && !gz) {
            Serial.printf("[FS] Missing: %s(.gz)\n", fullpath);
            return true;
        }
    }
    return false;
}

void Config::init() {
    sdResumePos = 0;
    /*----- I2C init -----*/
#if (RTC_MODULE == DS3231) || (TS_MODEL == TS_MODEL_FT6X36) || (TS_MODEL == TS_MODEL_GT911) || (TS_MODEL == TS_MODEL_AXS15231B)
    Serial.println("\n[INIT] Initializing I2C...");
#    if (TS_MODEL == TS_MODEL_FT6X36) && (TS_RST != 255)
    // FT6336U needs a hardware reset before it will respond on I2C
    Serial.printf("[INIT] FT6X36 reset on GPIO %d\n", TS_RST);
    pinMode(TS_RST, OUTPUT);
    digitalWrite(TS_RST, LOW);
    delay(20);
    digitalWrite(TS_RST, HIGH);
    delay(200);
#    endif
    Wire.begin(TS_SDA, TS_SCL);
    Wire.setClock(400000);
    Serial.println("[INIT] Scanning I2C @400kHz...");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  Found: 0x%02X\n", addr);
            ++found;
        }
    }
    if (found == 0) { Serial.println("[INIT] I2C scan found no devices."); }
    Serial.println("[INIT] I2C scan complete.");
#endif
    // DLNA modplus
#ifdef USE_DLNA
    isBooting = true;
    resumeAfterModeChange = false;
#endif
    // DLNA modplus
    screensaverTicks = 0;
    screensaverPlayingTicks = 0;
    newConfigMode = 0;
    isScreensaver = false;
    memset(tmpBuf, 0, BUFLEN);
#if RTCSUPPORTED
    _rtcFound = false;
    BOOTLOG("RTC begin(SDA=%d,SCL=%d)", RTC_SDA, RTC_SCL);
    if (rtc.init()) {
        BOOTLOG("done");
        _rtcFound = true;
    } else {
        BOOTLOG("[ERROR] - Couldn't find RTC");
    }
#endif
    emptyFS = true;
#if IR_PIN != 255
    irBtnId = -1;
#endif
#if defined(SD_SPIPINS)
#    if !defined(SD_SPIPINS)
    SDSPI.begin();
#    else
    SDSPI.begin(SD_SPIPINS); // SCK, MISO, MOSI, CS
#    endif
#endif
    eepromRead(EEPROM_START, store);

#ifdef USE_DLNA
    if (store.lastPlayedSource == PL_SRC_DLNA) {
        store.playlistSource = PL_SRC_DLNA;
    } else {
        store.playlistSource = PL_SRC_WEB;
    }
#endif
    bootInfo();
    if (store.config_set != 0) { setDefaults(); }
    if (store.version > CONFIG_VERSION) {
        saveValue(&store.version, (uint16_t)CONFIG_VERSION, true, true);
    } else {
        while (store.version != CONFIG_VERSION) { _setupVersion(); }
    }
    if (store.clockFontStyle > CLOCKFONT_STYLE_SQUAREFONT) { saveValue(&store.clockFontStyle, static_cast<uint8_t>(CLOCKFONT_STYLE)); }
    if (store.clockFontStyle != CLOCKFONT_STYLE_DIGI7 && store.clockFontMono) { saveValue(&store.clockFontMono, false); }
    if (store.dateFormat > 4) { saveValue(&store.dateFormat, static_cast<uint8_t>(0)); }
    // Older EEPROM layouts may leave the fade fields at zero/0xFF. A zero
    // step makes the state machine run forever without changing brightness.
    if (store.fadeEnabled > 1) { saveValue(&store.fadeEnabled, static_cast<uint8_t>(FADE_ENABLED)); }
    if (store.fadeStartDelay < 5 || store.fadeStartDelay > 3600) {
        saveValue(&store.fadeStartDelay, static_cast<uint16_t>(FADE_START_DELAY));
    }
    if (store.fadeTarget > 100) { saveValue(&store.fadeTarget, static_cast<uint8_t>(FADE_TARGET)); }
    if (store.fadeStep == 0 || store.fadeStep > 100) {
        saveValue(&store.fadeStep, static_cast<uint8_t>(FADE_STEP));
    }
    if (store.screensaverIdleBrightness > 100) {
        saveValue(&store.screensaverIdleBrightness, static_cast<uint8_t>(100));
    }
    BOOTLOG("CONFIG_VERSION\t%d", store.version);

    store.play_mode = store.play_mode & 0b11;
#ifndef USE_SD
    if (store.play_mode == PM_SDCARD) { store.play_mode = PM_WEB; }
#endif
#ifndef USE_BLUETOOTH
    if (store.play_mode == PM_BLUETOOTH) { store.play_mode = PM_WEB; }
#endif
    // DLNA modplus
#ifdef USE_DLNA
#else
    if (store.play_mode > 2) { store.play_mode = PM_WEB; }
#endif
    // DLNA modplus
    _initHW();
    if (!LittleFS.begin(false)) {
        Serial.println("##[ERROR]# LittleFS Mount Failed, formatting...");

        LittleFS.format();

        if (!LittleFS.begin()) {
            Serial.println("##[FATAL]# LittleFS still failed!");
            return;
        }
    }
    BOOTLOG("LittleFS mounted");
    bool themeLoaded = loadThemeFromFile();
    BOOTLOG("Theme file %s", themeLoaded ? "loaded" : "not found or invalid, using defaults");

    // Volume curve validálás és betöltés
    if (isVolumeCurveInvalid(store)) {
        setDefaultVolumeCurve();
        for (size_t i = 0; i < 21; ++i) {
            saveValue(&store.volumeCurveDb[i], store.volumeCurveDb[i], false, true);
        }
        EEPROM.commit();
    }
    bool curveLoaded = loadVolumeCurveFromFile();
    BOOTLOG("Volume curve file %s", curveLoaded ? "loaded" : "not found or invalid, using EEPROM/defaults");

    emptyFS = _isFSempty();
    if (emptyFS) {
        BOOTLOG("LittleFS is empty!");
    } else {

        Serial.println("---- CHECK ----");
        File f = LittleFS.open("/logo2.png");
        Serial.println(f ? "OPEN OK" : "OPEN FAIL");
        // DEBUG: Fájlok listázása
        File root = LittleFS.open("/");
        File file = root.openNextFile();

        while (file) {
            Serial.print("FILE RAW: [");
            Serial.print(file.name());
            Serial.println("]");
            file = root.openNextFile();
        }
    }

    ssidsCount = 0;
#ifdef USE_SD
    _SDplaylistFS = getMode() == PM_SDCARD ? &sdman : (true ? &LittleFS : _SDplaylistFS);
#else
    _SDplaylistFS = &LittleFS;
#endif
    _bootDone = false;
    setTimeConf();

#ifdef USE_DLNA
    isBooting = false;
#endif

#if PWR_AMP != 255 // "PWR_AMP"
    pinMode(PWR_AMP, OUTPUT);
    digitalWrite(PWR_AMP, HIGH);
#endif
}

void Config::_setupVersion() {
    uint16_t currentVersion = store.version;
    switch (currentVersion) {
        case 0: saveValue(&store.playlistMovingCursor, false); break;
        case 1: saveValue(&store.encodersIndependent, false); break;
        case 2: saveValue(&store.rssiAsText, false); break;
        case 3: break;
        case 4: saveValue(&store.serialLittlefsEnabled, true); break;
        case 5:
            saveValue(&store.lsEnabled,    (uint8_t)0);
            saveValue(&store.lsSsEnabled,  (uint8_t)0);
            saveValue(&store.lsModel,      (uint8_t)0);
            saveValue(&store.lsBrightness, (uint8_t)60);
            saveValue(&store.lsCount,      (uint8_t)24);
            break;
        case 6: saveValue(&store.autoEqEnabled, false); break;
        case 7: { /* Auto On-Off Timer fields + screensaver timeout unit migration */
            strlcpy(store.autoStartTime, "", sizeof(store.autoStartTime));
            strlcpy(store.autoStopTime,  "", sizeof(store.autoStopTime));
            const uint32_t timeoutSeconds = (uint32_t)store.screensaverPlayingTimeout * 60U;
            store.screensaverPlayingTimeout = timeoutSeconds < 5U
                                                   ? 5U
                                                   : timeoutSeconds > 65520U ? 65520U : (uint16_t)timeoutSeconds;
            eepromWrite(EEPROM_START, store);
            break;
        }
        case 8: saveValue(&store.screensaverIdleBrightness, (uint8_t)100); break;
    }
    currentVersion++;
    saveValue(&store.version, currentVersion);
}

void Config::toggleMode() {

#ifdef USE_DLNA

    // --- WEB módban vagyunk ---
    if (getMode() == PM_WEB) {

        if (store.playlistSource == PL_SRC_WEB) {

            // WEB → DLNA
            bool pir = player.isRunning();
            uint8_t oldSrc = store.playlistSource;
            store.playlistSource = (uint8_t)PL_SRC_DLNA;

            if (playlistLength() == 0) {
                // Nincs DLNA playlist még – maradunk WEB-en, nem ugrunk SD-re
                store.playlistSource = oldSrc;
                Serial.println("[MODE] WEB->DLNA: no DLNA playlist yet, staying on WEB");
                return;
            }

            saveValue(&store.playlistSource, (uint8_t)PL_SRC_DLNA, true, true);

            initPlaylistMode();

            if (pir) { player.sendCommand({PR_PLAY, (int)store.lastDlnaStation}); }

            display.purgeQueuedRequestType(NEWMODE);
            display.purgeQueuedRequestType(NEWSTATION);
            display.purgeQueuedRequestType(DBITRATE);
            display.putRequest(NEWMODE, PLAYER);
            display.putRequest(NEWSTATION);
            display.putRequest(DBITRATE);
            return;
        }
        else {
            // DLNA → SD
            changeMode(PM_SDCARD);
            return;
        }
    }

    // --- SD → WEB ---
    store.playlistSource = PL_SRC_WEB;
    saveValue(&store.playlistSource, (uint8_t)PL_SRC_WEB, true, true);
    changeMode(PM_WEB);

#else

    // DLNA nincs → sima toggle
    changeMode(getMode() == PM_SDCARD ? PM_WEB : PM_SDCARD);

#endif
}

void Config::changeMode(int newmode) { // DLNA mod
    // Serial.printf(\"Config.cpp-->changeMode() newmode: %d\", newmode);
#if defined(USE_SD) || defined(USE_BLUETOOTH)
    // Encoder dupla klikk (paraméter nélküli hívás):
    // BT módból kilépés → WEB-re, egyébként WEB↔SD toggle
    if (newmode == -1) {
#ifdef USE_BLUETOOTH
        if (getMode() == PM_BLUETOOTH) {
            newmode = PM_WEB;
        } else
#endif
#ifdef USE_SD
        {
            newmode = (getMode() == PM_SDCARD) ? PM_WEB : PM_SDCARD;
        }
#else
        {
            newmode = PM_BLUETOOTH;
        }
#endif
    }

    // 🔒 biztonsági ellenőrzés
    if (newmode < 0 || newmode >= 3) { // 0 --> radio; 1 --> SD; 2 --> BLUETOOTH
        Serial.printf("##[ERROR]# changeMode invalid newmode: %d\\n", newmode);
        return;
    }
#ifndef USE_BLUETOOTH
    if (newmode == PM_BLUETOOTH) { return; }
#endif

    bool pir = player.isRunning();

#ifdef USE_SD
    if (SDC_CS == 255 && newmode == PM_SDCARD) { return; }
#else
    if (newmode == PM_SDCARD) { return; }
#endif

#ifdef USE_SD
    if (network.status == SOFT_AP || display.mode() == LOST) {
        saveValue(&store.play_mode, (uint8_t)PM_SDCARD);
        delay(50);
        ESP.restart();
    }
#endif

    /* === BT módból kilépés: híd leállítása === */
#ifdef USE_BLUETOOTH
    if (getMode() == PM_BLUETOOTH && newmode != PM_BLUETOOTH) {
        bluetooth.stopBridge();
        log_i("##[BT]# bridge stopped (mode change -> %d)", newmode);
    }
#endif

    /* === SD only when explicitly requested === */
#ifdef USE_SD
    if (newmode == PM_SDCARD) {
        if (!sdman.ready) {
            if (!sdman.start()) {
                Serial.println("##[ERROR]# SD Not Found");
                netserver.requestOnChange(GETPLAYERMODE, 0);
                return;
            }
        }
    }
#endif

    /* === set mode === */
    store.play_mode = (playMode_e)newmode;
    saveValue(&store.play_mode, (uint8_t)store.play_mode, true, true);

#ifdef USE_SD
    /* === filesystem binding === */
    if (getMode() == PM_SDCARD) {
        _SDplaylistFS = &sdman;
    } else {
        _SDplaylistFS = &LittleFS; // WEB + BT + DLNA
    }

    /* === SD specific actions === */
    if (getMode() == PM_SDCARD) {
        if (pir) { player.sendCommand({PR_STOP, 0}); }
        display.putRequest(NEWMODE, SDCHANGE);
        delay(50);
    } else {
        sdman.stop(); // WEB + BT + DLNA → SD off
    }

    /* === BT módba lépés: decoder leállítása === */
#else
    _SDplaylistFS = &LittleFS;
#endif

#ifdef USE_BLUETOOTH
    if (getMode() == PM_BLUETOOTH) {
        if (pir) {
            // spi_lock_callback BT módban automatikusan no-op (lásd main.cpp),
            // tehát PR_STOP alatt nincs display.lock() → nem fagy be a display.
            player.sendCommand({PR_STOP, 0});
            delay(300);
        }
    }
#endif

    if (!_bootDone) { return; }

    initPlaylistMode();

    /* === WEB/SD módban lejátszás indítása === */
    if (pir
#ifdef USE_BLUETOOTH
        && getMode() != PM_BLUETOOTH
#endif
    ) {
        player.sendCommand({PR_PLAY, (int)lastStation()});
    }

    /* === BT módban: állomásnév + cím beállítása (initPlaylistMode UTÁN!) === */
#ifdef USE_BLUETOOTH
    if (getMode() == PM_BLUETOOTH) {
        config.setStation("Bluetooth");
        config.setTitle("");
    }
#endif

    netserver.resetQueue();
    netserver.requestOnChange(GETINDEX, 0);
    netserver.requestOnChange(GETPLAYERMODE, 0);

    display.purgeQueuedRequestType(NEWMODE);
    display.purgeQueuedRequestType(NEWSTATION);
    display.purgeQueuedRequestType(NEWTITLE);
    display.purgeQueuedRequestType(DBITRATE);
    display.purgeQueuedRequestType(PSTART);
    display.purgeQueuedRequestType(PSTOP);
#ifdef USE_BLUETOOTH
    if (getMode() == PM_BLUETOOTH) { display.putRequest(PSTOP); }
#endif
    display.putRequest(NEWMODE, PLAYER);
    display.putRequest(NEWSTATION);
    display.putRequest(NEWTITLE);  // BT módba lépéskor a cím törlése (setTitle("") resetQueue előtt fut, ezt pótolja)
    display.putRequest(DBITRATE);

    /* === BT: I2S audiohíd indítása === */
#ifdef USE_BLUETOOTH
    if (getMode() == PM_BLUETOOTH) {
        if (!bluetooth.startBridge()) {
            log_e("##[BT]# startBridge() failed!");
        } else {
            log_i("##[BT]# bridge started");
        }
        log_i("##[BT]# mode active (audio via BT module DAC output)");
    }
#endif
#endif
}

void Config::initSDPlaylist() {
#ifdef USE_SD
    // Rebuild the index, but preserve the last selected SD track whenever it
    // is still valid for the current card contents.
    sdman.indexSDPlaylist();
    if (SDPLFS()->exists(INDEX_SD_PATH)) {
        File index = SDPLFS()->open(INDEX_SD_PATH, "r");
        const uint16_t stationCount = index ? index.size() / 4 : 0;
        if (stationCount == 0) {
            if (store.lastSdStation != 0) {
                saveValue(&store.lastSdStation, static_cast<uint16_t>(0));
            }
        } else if (store.lastSdStation == 0 || store.lastSdStation > stationCount) {
            saveValue(&store.lastSdStation, static_cast<uint16_t>(1));
        }
        log_i("##[SD]# playlist tracks=%u resume=%u",
              static_cast<unsigned>(stationCount),
              static_cast<unsigned>(store.lastSdStation));
        sdResumePos = 0;
        index.close();
    }
#endif // #ifdef USE_SD
}

bool Config::littlefsCleanup() {
    bool ret = (LittleFS.exists(PLAYLIST_SD_PATH)) || (LittleFS.exists(INDEX_SD_PATH)) || (LittleFS.exists(INDEX_PATH));
    if (LittleFS.exists(PLAYLIST_SD_PATH)) { LittleFS.remove(PLAYLIST_SD_PATH); }
    if (LittleFS.exists(INDEX_SD_PATH)) { LittleFS.remove(INDEX_SD_PATH); }
    if (LittleFS.exists(INDEX_PATH)) { LittleFS.remove(INDEX_PATH); }
    return ret;
}

void Config::waitConnection() {
#if I2S_DOUT == 255
    return;
#endif
    while (!player.connproc) { vTaskDelay(50); }
    vTaskDelay(500);
}

char* Config::ipToStr(IPAddress ip) {
    snprintf(ipBuf, 20, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return ipBuf;
}

bool Config::prepareForPlaying(uint16_t stationId) {
    setDspOn(1);
    screensaverTicks = SCREENSAVERSTARTUPDELAY;
    screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
    display.beginContentChange();
    // A stale PSTART from the previous station can otherwise re-enable the
    // player layout while the new station is still preparing.
    display.purgeQueuedRequestType(PSTART);
    if (!loadStation(stationId)) { return false; }
    setTitle(LANG::const_PlConnect); // inen van a connect felirat a kijelzőn
    station.bitrate = 0;
    setBitrateFormat(BF_UNKNOWN);
    netserver.requestOnChange(BITRATE, 0);
    display.putRequest(PLAYERREBUILD, stationId);
    netserver.requestOnChange(STATION, 0);
    netserver.requestOnChange(MODE, 0);
    if (store.smartstart != 2) { setSmartStart(0); }
    return true;
}

void Config::configPostPlaying(uint16_t stationId) { // DLNA mod
    if (getMode() == PM_SDCARD) {
        saveValue(&store.lastSdStation, stationId);

        // ID3 nélküli fájlnál is legyen értelmes cím és CoverArt keresési
        // alap. A később érkező Artist/Title ID3 mezők ezt felülírják.
        if (strcmp(station.title, LANG::const_PlConnect) == 0 && station.name[0] != '\0') {
            char localTitle[BUFLEN];
            strlcpy(localTitle, station.name, sizeof(localTitle));
            char* extension = strrchr(localTitle, '.');
            if (extension && extension != localTitle) { *extension = '\0'; }
            log_i("##[SDMETA]# filename fallback='%s'", localTitle);
            setTitle(localTitle);
        }
    }
#ifdef USE_DLNA
    else if (store.playlistSource == PL_SRC_DLNA) {
        saveValue(&store.lastDlnaStation, stationId);
    }
#endif
    else {
        saveValue(&store.lastStation, stationId);
    }

    if (store.smartstart != 2) { setSmartStart(1); }

    // FusionEdge: Auto EQ – ha a felhasználó bekapcsolta, és a playlist
    // CSV-bejegyzésnek van genre mezője (4. tab-elválasztott mező), és
    // ez egyezik valamelyik EQ preset nevével (case-insensitive), akkor
    // automatikusan beállítjuk azt a presetet a stream indulásakor.
    // Ha nincs genre, vagy nincs egyező preset, az aktuális EQ beállítás
    // változatlan marad (nem esik vissza "Flat"-ra).
    if (store.autoEqEnabled && station.genre[0] != '\0') {
        int8_t bass, mid, treb;
        if (findEqPresetByGenre(station.genre, bass, mid, treb)) {
            setTone(bass, mid, treb);
        }
    }

    netserver.requestOnChange(MODE, 0);
    display.waitQueueEmpty(350);
    display.putRequest(PSTART, stationId);
    display.waitContentReady(350);
}

void Config::setSDpos(uint32_t val) {
    if (getMode() == PM_SDCARD) {
        sdResumePos = 0; // ha kézzel állítasz pozíciót, ne legyen régi resume
        if (!player.isRunning()) {
            config.sdResumePos = val - player.sd_min;
        } else {
            player.setAudioFilePosition(val - player.sd_min); // futó lejátszásnál seek webről
        }
    }
}

void Config::initPlaylistMode() {
    // BT módban nincs playlist — kihagyjuk az egészet
#ifdef USE_BLUETOOTH
    if (getMode() == PM_BLUETOOTH) {
        _bootDone = true;
        // Boot közben BT módban: station nevet beállítjuk, hogy
        // a display.ready() után küldött NEWSTATION/DBITRATE helyes adatot mutasson
        memset(station.name, 0, BUFLEN);
        strlcpy(station.name, "Bluetooth", BUFLEN);
        memset(station.title, 0, BUFLEN); // title ürítése
        return;
    }
#endif

    uint16_t _lastStation = 0;

#ifdef USE_SD
    if (getMode() == PM_SDCARD) {
        if (!sdman.start()) {
            changeMode(PM_WEB);
            return;
        }
        initSDPlaylist();
        uint16_t cs = playlistLength();
        _lastStation = store.lastSdStation;
        if (_lastStation == 0 && cs > 0) { _lastStation = _randomStation(); }
    } else
#endif
    {

#ifdef USE_DLNA
        if (store.playlistSource == PL_SRC_DLNA) {

            if (LittleFS.exists(PLAYLIST_DLNA_PATH)) { initDLNAPlaylist(); }

            uint16_t cs = playlistLength();

            // ⬇️ DLNA indexet CSAK innen vesszük
            _lastStation = store.lastDlnaStation;
            if (_lastStation == 0 && cs > 0) { _lastStation = 1; }

        } else
#endif
        {
            initPlaylist();
            uint16_t cs = playlistLength();
            _lastStation = store.lastStation;
            if (_lastStation == 0 && cs > 0) { _lastStation = 1; }
#if defined(ALWAYS_START_FROM_FIRST)
            if (cs > 0) _lastStation = 1;
#endif
        }
    }

    // ⬇️ EGYSZER
    lastStation(_lastStation);
    loadStation(_lastStation);

    _bootDone = true;
}

void Config::_initHW() {
    loadTheme();
#if IR_PIN != 255
    eepromRead(EEPROM_START_IR, ircodes);
#endif
#if BRIGHTNESS_PIN != 255
    gpio_hold_dis((gpio_num_t)BRIGHTNESS_PIN); // ← add (MB)
    pinMode(BRIGHTNESS_PIN, OUTPUT);
    // Keep backlight off during display controller init to avoid boot flash.
    analogWrite(BRIGHTNESS_PIN, 0);
#endif
}

uint16_t Config::color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Config::loadTheme() {
    _setDefaultTheme();
}

void Config::_setDefaultTheme() {
    theme.background = color565(COLOR_BACKGROUND);
    theme.meta = color565(COLOR_STATION_NAME);
    theme.metabg = color565(COLOR_STATION_BG);
    theme.metafill = color565(COLOR_STATION_FILL);
    theme.title1 = color565(COLOR_SNG_TITLE_1);
    theme.title2 = color565(COLOR_SNG_TITLE_2);
    theme.digit = color565(COLOR_DIGITS);
    theme.div = color565(COLOR_DIVIDER);
    /*----- WEATHER -----*/
    theme.weather        = color565(COLOR_WEATHER);
    theme.weatherIconTxt  = color565(COLOR_WEATHER_ICON_TXT);
    /*----- STATUS WIDGET -----*/
    theme.status_active   = color565(COLOR_STATUS_ACTIVE);
    theme.status_inactive = color565(COLOR_STATUS_INACTIVE);
    /*----- CLOCK -----*/
    theme.clock = color565(COLOR_CLOCK);
    theme.clockbg = color565(COLOR_CLOCK_BG);
    theme.seconds = color565(COLOR_SECONDS);
    /*----- DATE & DAY -----*/
    theme.date = color565(COLOR_DATE);
    /*----- FOOTER -----*/
    theme.buffer = color565(COLOR_BUFFER);
    theme.ip = color565(COLOR_IP);
    theme.ip_bg = color565(COLOR_IP_BG);
    theme.ip_border = color565(COLOR_IP_BORDER);
    /*----- AUDIO BUFFER WIDGET -----*/
    theme.buff = color565(COLOR_BUFF);
    theme.buff_bg = color565(COLOR_BUFF_BG);
    theme.buff_border = color565(COLOR_BUFF_BORDER);
    theme.buff_inactive = color565(COLOR_BUFF_INACTIVE);
    theme.buff_icon = color565(COLOR_BUFF_ICON);
    theme.rssi = color565(COLOR_RSSI);
    theme.rssi_bg = color565(COLOR_RSSI_BG);
    theme.rssi_border = color565(COLOR_RSSI_BORDER);
    /*----- BITRATE WIDGET -----*/
    theme.bitrate = color565(COLOR_BITRATE);
    /*----- PLAYMODE WIDGET -----*/
    theme.pmode = color565(COLOR_PMODE);
    /*----- SPECTRUM WIDGET -----*/
    theme.vumax = color565(COLOR_VU_MAX);
    theme.vumid = color565(COLOR_VU_MID);
    theme.vumin = color565(COLOR_VU_MIN);
    /*----- PLAYLIST WIDGET-----*/
    theme.plcurrent = color565(COLOR_PL_CURRENT);
    theme.plcurrentbg = color565(COLOR_PL_CURRENT_BG);
    theme.plcurrentfill = color565(COLOR_PL_CURRENT_FILL);
    theme.playlist[0] = color565(COLOR_PLAYLIST_0);
    theme.playlist[1] = color565(COLOR_PLAYLIST_1);
    theme.playlist[2] = color565(COLOR_PLAYLIST_2);
    theme.playlist[3] = color565(COLOR_PLAYLIST_3);
    theme.playlist[4] = color565(COLOR_PLAYLIST_4);
    /*----- PRESETS -----*/
    theme.prst_button = color565(COLOR_PRST_BUTTON);
    theme.prst_card = color565(COLOR_PRST_CARD);
    theme.prst_accent = color565(COLOR_PRST_ACCENT);
    theme.prst_fav = color565(COLOR_PRST_FAV);
    theme.prst_title1 = color565(COLOR_PRST_TITLE_1);
    theme.prst_title2 = color565(COLOR_PRST_TITLE_2);
    theme.prst_title3 = color565(COLOR_PRST_TITLE_3);
    theme.prst_line = color565(COLOR_PRST_LINE);
    /*----- VOLUME WIDGET -----*/
    theme.vol_bg = color565(COLOR_VOL_BG);
    theme.vol_border = color565(COLOR_VOL_BORDER);
    theme.vol_inactive = color565(COLOR_VOL_INACTIVE);
    theme.vol_low = color565(COLOR_VOL_LOW);
    theme.vol_mid = color565(COLOR_VOL_MID);
    theme.vol_high = color565(COLOR_VOL_HIGH);
    theme.vol_icon = color565(COLOR_VOL_ICON);
    /*----- WIFI WIDGET -----*/
    theme.wifi_bg = color565(COLOR_WIFI_BG);
    theme.wifi_border = color565(COLOR_WIFI_BORDER);
    theme.wifi_inactive = color565(COLOR_WIFI_INACTIVE);
    theme.wifi_low = color565(COLOR_WIFI_LOW);
    theme.wifi_low_mid = color565(COLOR_WIFI_LOW_MID);
    theme.wifi_mid = color565(COLOR_WIFI_MID);
    theme.wifi_high = color565(COLOR_WIFI_HIGH);
}

bool Config::setThemeColorByName(const char* name, uint8_t r, uint8_t g, uint8_t b) {
    if (!name || !*name) { return false; }
    const uint16_t c = color565(r, g, b);

#define SET_THEME_COLOR(_name, _field)   \
    if (equalsIgnoreCase(name, _name)) { \
        theme._field = c;                \
        return true;                     \
    }

    SET_THEME_COLOR("background", background);
    SET_THEME_COLOR("meta", meta);
    SET_THEME_COLOR("station_name", meta);
    SET_THEME_COLOR("metabg", metabg);
    SET_THEME_COLOR("station_bg", metabg);
    SET_THEME_COLOR("metafill", metafill);
    SET_THEME_COLOR("station_fill", metafill);
    SET_THEME_COLOR("title1", title1);
    SET_THEME_COLOR("sng_title_1", title1);
    SET_THEME_COLOR("title2", title2);
    SET_THEME_COLOR("sng_title_2", title2);
    SET_THEME_COLOR("bitrate", bitrate);
    SET_THEME_COLOR("weather", weather);
    SET_THEME_COLOR("weathericontxt", weatherIconTxt);
    SET_THEME_COLOR("status_active", status_active);
    SET_THEME_COLOR("status_inactive", status_inactive);
    SET_THEME_COLOR("digit", digit);
    SET_THEME_COLOR("digits", digit);
    SET_THEME_COLOR("clock", clock);
    SET_THEME_COLOR("clockbg", clockbg);
    SET_THEME_COLOR("clock_bg", clockbg);
    SET_THEME_COLOR("seconds", seconds);
    SET_THEME_COLOR("div", div);
    SET_THEME_COLOR("divider", div);
    SET_THEME_COLOR("date", date);
    SET_THEME_COLOR("vumax", vumax);
    SET_THEME_COLOR("vu_max", vumax);
    SET_THEME_COLOR("vumid", vumid);
    SET_THEME_COLOR("vu_mid", vumid);
    SET_THEME_COLOR("vumin", vumin);
    SET_THEME_COLOR("vu_min", vumin);
    SET_THEME_COLOR("vol_bg", vol_bg);
    SET_THEME_COLOR("vol_border", vol_border);
    SET_THEME_COLOR("vol_inactive", vol_inactive);
    SET_THEME_COLOR("vol_low", vol_low);
    SET_THEME_COLOR("vol_mid", vol_mid);
    SET_THEME_COLOR("vol_high", vol_high);
    SET_THEME_COLOR("vol_icon", vol_icon);
    SET_THEME_COLOR("wifi_bg", wifi_bg);
    SET_THEME_COLOR("wifi_border", wifi_border);
    SET_THEME_COLOR("wifi_inactive", wifi_inactive);
    SET_THEME_COLOR("wifi_low", wifi_low);
    SET_THEME_COLOR("wifi_low_mid", wifi_low_mid);
    SET_THEME_COLOR("wifi_mid", wifi_mid);
    SET_THEME_COLOR("wifi_high", wifi_high);
    SET_THEME_COLOR("ip_text", ip);
    SET_THEME_COLOR("ip", ip);
    SET_THEME_COLOR("ip_bg", ip_bg);
    SET_THEME_COLOR("ip_border", ip_border);
    SET_THEME_COLOR("buff", buff);
    SET_THEME_COLOR("buff_bg", buff_bg);
    SET_THEME_COLOR("buff_border", buff_border);
    SET_THEME_COLOR("buff_inactive", buff_inactive);
    SET_THEME_COLOR("buff_icon", buff_icon);
    SET_THEME_COLOR("rssi_text", rssi);
    SET_THEME_COLOR("rssi", rssi);
    SET_THEME_COLOR("rssi_bg", rssi_bg);
    SET_THEME_COLOR("rssi_border", rssi_border);
    SET_THEME_COLOR("buffer", buffer);
    SET_THEME_COLOR("pl_current", plcurrent);
    SET_THEME_COLOR("plcurrent", plcurrent);
    SET_THEME_COLOR("pl_current_bg", plcurrentbg);
    SET_THEME_COLOR("plcurrentbg", plcurrentbg);
    SET_THEME_COLOR("pl_current_fill", plcurrentfill);
    SET_THEME_COLOR("plcurrentfill", plcurrentfill);
    SET_THEME_COLOR("prst_button", prst_button);
    SET_THEME_COLOR("prst_card", prst_card);
    SET_THEME_COLOR("prst_accent", prst_accent);
    SET_THEME_COLOR("prst_fav", prst_fav);
    SET_THEME_COLOR("prst_title1", prst_title1);
    SET_THEME_COLOR("prst_title2", prst_title2);
    SET_THEME_COLOR("prst_title3", prst_title3);
    SET_THEME_COLOR("prst_line", prst_line);
    SET_THEME_COLOR("pmode", pmode);

#undef SET_THEME_COLOR

    if (equalsIgnoreCase(name, "playlist_0") || equalsIgnoreCase(name, "playlist0")) {
        theme.playlist[0] = c;
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_1") || equalsIgnoreCase(name, "playlist1")) {
        theme.playlist[1] = c;
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_2") || equalsIgnoreCase(name, "playlist2")) {
        theme.playlist[2] = c;
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_3") || equalsIgnoreCase(name, "playlist3")) {
        theme.playlist[3] = c;
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_4") || equalsIgnoreCase(name, "playlist4")) {
        theme.playlist[4] = c;
        return true;
    }

    return false;
}

bool Config::getThemeColorByName(const char* name, uint16_t& color) const {
    if (!name || !*name) { return false; }

#define GET_THEME_COLOR(_name, _field)   \
    if (equalsIgnoreCase(name, _name)) { \
        color = theme._field;            \
        return true;                     \
    }

    GET_THEME_COLOR("background", background);
    GET_THEME_COLOR("meta", meta);
    GET_THEME_COLOR("station_name", meta);
    GET_THEME_COLOR("metabg", metabg);
    GET_THEME_COLOR("station_bg", metabg);
    GET_THEME_COLOR("metafill", metafill);
    GET_THEME_COLOR("station_fill", metafill);
    GET_THEME_COLOR("title1", title1);
    GET_THEME_COLOR("sng_title_1", title1);
    GET_THEME_COLOR("title2", title2);
    GET_THEME_COLOR("sng_title_2", title2);
    GET_THEME_COLOR("bitrate", bitrate);
    GET_THEME_COLOR("weather", weather);
    GET_THEME_COLOR("weathericontxt", weatherIconTxt);
    GET_THEME_COLOR("status_active", status_active);
    GET_THEME_COLOR("status_inactive", status_inactive);
    GET_THEME_COLOR("digit", digit);
    GET_THEME_COLOR("digits", digit);
    GET_THEME_COLOR("clock", clock);
    GET_THEME_COLOR("clockbg", clockbg);
    GET_THEME_COLOR("clock_bg", clockbg);
    GET_THEME_COLOR("seconds", seconds);
    GET_THEME_COLOR("div", div);
    GET_THEME_COLOR("divider", div);
    GET_THEME_COLOR("date", date);
    GET_THEME_COLOR("vumax", vumax);
    GET_THEME_COLOR("vu_max", vumax);
    GET_THEME_COLOR("vumid", vumid);
    GET_THEME_COLOR("vu_mid", vumid);
    GET_THEME_COLOR("vumin", vumin);
    GET_THEME_COLOR("vu_min", vumin);
    GET_THEME_COLOR("vol_bg", vol_bg);
    GET_THEME_COLOR("vol_border", vol_border);
    GET_THEME_COLOR("vol_inactive", vol_inactive);
    GET_THEME_COLOR("vol_low", vol_low);
    GET_THEME_COLOR("vol_mid", vol_mid);
    GET_THEME_COLOR("vol_high", vol_high);
    GET_THEME_COLOR("vol_icon", vol_icon);
    GET_THEME_COLOR("wifi_bg", wifi_bg);
    GET_THEME_COLOR("wifi_border", wifi_border);
    GET_THEME_COLOR("wifi_inactive", wifi_inactive);
    GET_THEME_COLOR("wifi_low", wifi_low);
    GET_THEME_COLOR("wifi_low_mid", wifi_low_mid);
    GET_THEME_COLOR("wifi_mid", wifi_mid);
    GET_THEME_COLOR("wifi_high", wifi_high);
    GET_THEME_COLOR("ip_text", ip);
    GET_THEME_COLOR("ip", ip);
    GET_THEME_COLOR("ip_bg", ip_bg);
    GET_THEME_COLOR("ip_border", ip_border);
    GET_THEME_COLOR("buff", buff);
    GET_THEME_COLOR("buff_bg", buff_bg);
    GET_THEME_COLOR("buff_border", buff_border);
    GET_THEME_COLOR("buff_inactive", buff_inactive);
    GET_THEME_COLOR("buff_icon", buff_icon);
    GET_THEME_COLOR("rssi_text", rssi);
    GET_THEME_COLOR("rssi", rssi);
    GET_THEME_COLOR("rssi_bg", rssi_bg);
    GET_THEME_COLOR("rssi_border", rssi_border);
    GET_THEME_COLOR("buffer", buffer);
    GET_THEME_COLOR("pl_current", plcurrent);
    GET_THEME_COLOR("plcurrent", plcurrent);
    GET_THEME_COLOR("pl_current_bg", plcurrentbg);
    GET_THEME_COLOR("plcurrentbg", plcurrentbg);
    GET_THEME_COLOR("pl_current_fill", plcurrentfill);
    GET_THEME_COLOR("plcurrentfill", plcurrentfill);
    GET_THEME_COLOR("prst_button", prst_button);
    GET_THEME_COLOR("prst_card", prst_card);
    GET_THEME_COLOR("prst_accent", prst_accent);
    GET_THEME_COLOR("prst_fav", prst_fav);
    GET_THEME_COLOR("prst_title1", prst_title1);
    GET_THEME_COLOR("prst_title2", prst_title2);
    GET_THEME_COLOR("prst_title3", prst_title3);
    GET_THEME_COLOR("prst_line", prst_line);

#undef GET_THEME_COLOR

    if (equalsIgnoreCase(name, "playlist_0") || equalsIgnoreCase(name, "playlist0")) {
        color = theme.playlist[0];
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_1") || equalsIgnoreCase(name, "playlist1")) {
        color = theme.playlist[1];
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_2") || equalsIgnoreCase(name, "playlist2")) {
        color = theme.playlist[2];
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_3") || equalsIgnoreCase(name, "playlist3")) {
        color = theme.playlist[3];
        return true;
    }
    if (equalsIgnoreCase(name, "playlist_4") || equalsIgnoreCase(name, "playlist4")) {
        color = theme.playlist[4];
        return true;
    }

    return false;
}

bool Config::applyThemeCsv(const char* csvData) {
    if (!csvData) { return false; }
    bool        changed = false;
    char        line[160];
    const char* p = csvData;

    while (*p) {
        size_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1) {
            line[n++] = *p;
            ++p;
        }
        if (*p == '\n') { ++p; }
        line[n] = '\0';
        if (n > 0 && line[n - 1] == '\r') { line[n - 1] = '\0'; }

        char* row = trimInPlace(line);
        if (*row == '\0' || *row == '#') { continue; }

        char* saveptr = nullptr;
        char* name = strtok_r(row, ",", &saveptr);
        char* rs = strtok_r(nullptr, ",", &saveptr);
        char* gs = strtok_r(nullptr, ",", &saveptr);
        char* bs = strtok_r(nullptr, ",", &saveptr);

        if (!name || !rs || !gs || !bs) { continue; }

        name = trimInPlace(name);
        rs = trimInPlace(rs);
        gs = trimInPlace(gs);
        bs = trimInPlace(bs);

        if (equalsIgnoreCase(name, "version")) { continue; }

        char* end = nullptr;
        long  rv = strtol(rs, &end, 10);
        if (end == rs) { continue; }
        long gv = strtol(gs, &end, 10);
        if (end == gs) { continue; }
        long bv = strtol(bs, &end, 10);
        if (end == bs) { continue; }

        if (setThemeColorByName(name, clampChannel(rv), clampChannel(gv), clampChannel(bv))) { changed = true; }
    }

    return changed;
}

bool Config::loadThemeFromFile(const char* path) {
    if (!path || !LittleFS.exists(path)) { return false; }
    File file = LittleFS.open(path, "r");
    if (!file) { return false; }
    String content;
    content.reserve(file.size() + 1);
    while (file.available()) {
        content += file.readStringUntil('\n');
        content += '\n';
    }
    file.close();
    return applyThemeCsv(content.c_str());
}

bool Config::saveThemeToFile() {
    if (!LittleFS.exists("/data")) { LittleFS.mkdir("/data"); }

    File file = LittleFS.open(THEME_PATH, "w");
    if (!file) { return false; }

    uint8_t v1, v2, v3;
    parseVersionTriplet(THEME_CSV_VERSION, v1, v2, v3);
    file.printf("version,%u,%u,%u\n", v1, v2, v3);

    auto writeColor = [&](const char* key, uint16_t color) {
        uint8_t r, g, b;
        color565ToRgb(color, r, g, b);
        file.printf("%s,%u,%u,%u\n", key, r, g, b);
    };

    writeColor("background", theme.background);
    writeColor("meta", theme.meta);
    writeColor("metabg", theme.metabg);
    writeColor("metafill", theme.metafill);
    writeColor("title1", theme.title1);
    writeColor("title2", theme.title2);
    writeColor("bitrate", theme.bitrate);
    writeColor("pmode", theme.pmode);
    writeColor("weather", theme.weather);
    writeColor("weathericontxt", theme.weatherIconTxt);
    writeColor("status_active", theme.status_active);
    writeColor("status_inactive", theme.status_inactive);
    writeColor("digit", theme.digit);
    writeColor("clock", theme.clock);
    writeColor("clockbg", theme.clockbg);
    writeColor("seconds", theme.seconds);
    writeColor("div", theme.div);
    writeColor("date", theme.date);
    writeColor("vumax", theme.vumax);
    writeColor("vumid", theme.vumid);
    writeColor("vumin", theme.vumin);
    writeColor("vol_bg", theme.vol_bg);
    writeColor("vol_border", theme.vol_border);
    writeColor("vol_inactive", theme.vol_inactive);
    writeColor("vol_low", theme.vol_low);
    writeColor("vol_mid", theme.vol_mid);
    writeColor("vol_high", theme.vol_high);
    writeColor("vol_icon", theme.vol_icon);
    writeColor("wifi_bg", theme.wifi_bg);
    writeColor("wifi_border", theme.wifi_border);
    writeColor("wifi_inactive", theme.wifi_inactive);
    writeColor("wifi_low", theme.wifi_low);
    writeColor("wifi_low_mid", theme.wifi_low_mid);
    writeColor("wifi_mid", theme.wifi_mid);
    writeColor("wifi_high", theme.wifi_high);
    writeColor("ip", theme.ip);
    writeColor("ip_bg", theme.ip_bg);
    writeColor("ip_border", theme.ip_border);
    writeColor("buff", theme.buff);
    writeColor("buff_bg", theme.buff_bg);
    writeColor("buff_border", theme.buff_border);
    writeColor("buff_inactive", theme.buff_inactive);
    writeColor("buff_icon", theme.buff_icon);
    writeColor("rssi", theme.rssi);
    writeColor("rssi_bg", theme.rssi_bg);
    writeColor("rssi_border", theme.rssi_border);
    writeColor("buffer", theme.buffer);
    writeColor("pl_current", theme.plcurrent);
    writeColor("pl_current_bg", theme.plcurrentbg);
    writeColor("pl_current_fill", theme.plcurrentfill);
    writeColor("playlist_0", theme.playlist[0]);
    writeColor("playlist_1", theme.playlist[1]);
    writeColor("playlist_2", theme.playlist[2]);
    writeColor("playlist_3", theme.playlist[3]);
    writeColor("playlist_4", theme.playlist[4]);
    writeColor("prst_button", theme.prst_button);
    writeColor("prst_card", theme.prst_card);
    writeColor("prst_accent", theme.prst_accent);
    writeColor("prst_fav", theme.prst_fav);
    writeColor("prst_title1", theme.prst_title1);
    writeColor("prst_title2", theme.prst_title2);
    writeColor("prst_title3", theme.prst_title3);
    writeColor("prst_line", theme.prst_line);

    file.close();
    return true;
}

String Config::themeToJson() const {
    String out = "{";
    bool   first = true;

    auto appendColor = [&](const char* key, uint16_t color) {
        uint8_t r, g, b;
        color565ToRgb(color, r, g, b);
        char colorHex[8];
        snprintf(colorHex, sizeof(colorHex), "#%02X%02X%02X", r, g, b);
        if (!first) { out += ","; }
        first = false;
        out += "\"";
        out += key;
        out += "\":\"";
        out += colorHex;
        out += "\"";
    };

    appendColor("background", theme.background);
    appendColor("meta", theme.meta);
    appendColor("metabg", theme.metabg);
    appendColor("metafill", theme.metafill);
    appendColor("title1", theme.title1);
    appendColor("title2", theme.title2);
    appendColor("bitrate", theme.bitrate);
    appendColor("pmode", theme.pmode);
    appendColor("weather", theme.weather);
    appendColor("weathericontxt", theme.weatherIconTxt);
    appendColor("status_active", theme.status_active);
    appendColor("status_inactive", theme.status_inactive);
    appendColor("digit", theme.digit);
    appendColor("clock", theme.clock);
    appendColor("clockbg", theme.clockbg);
    appendColor("seconds", theme.seconds);
    appendColor("div", theme.div);
    appendColor("date", theme.date);
    appendColor("vumax", theme.vumax);
    appendColor("vumid", theme.vumid);
    appendColor("vumin", theme.vumin);
    appendColor("vol_bg", theme.vol_bg);
    appendColor("vol_border", theme.vol_border);
    appendColor("vol_inactive", theme.vol_inactive);
    appendColor("vol_low", theme.vol_low);
    appendColor("vol_mid", theme.vol_mid);
    appendColor("vol_high", theme.vol_high);
    appendColor("vol_icon", theme.vol_icon);
    appendColor("wifi_bg", theme.wifi_bg);
    appendColor("wifi_border", theme.wifi_border);
    appendColor("wifi_inactive", theme.wifi_inactive);
    appendColor("wifi_low", theme.wifi_low);
    appendColor("wifi_low_mid", theme.wifi_low_mid);
    appendColor("wifi_mid", theme.wifi_mid);
    appendColor("wifi_high", theme.wifi_high);
    appendColor("ip", theme.ip);
    appendColor("ip_bg", theme.ip_bg);
    appendColor("ip_border", theme.ip_border);
    appendColor("buff", theme.buff);
    appendColor("buff_bg", theme.buff_bg);
    appendColor("buff_border", theme.buff_border);
    appendColor("buff_inactive", theme.buff_inactive);
    appendColor("buff_icon", theme.buff_icon);
    appendColor("rssi", theme.rssi);
    appendColor("rssi_bg", theme.rssi_bg);
    appendColor("rssi_border", theme.rssi_border);
    appendColor("buffer", theme.buffer);
    appendColor("pl_current", theme.plcurrent);
    appendColor("pl_current_bg", theme.plcurrentbg);
    appendColor("pl_current_fill", theme.plcurrentfill);
    appendColor("playlist_0", theme.playlist[0]);
    appendColor("playlist_1", theme.playlist[1]);
    appendColor("playlist_2", theme.playlist[2]);
    appendColor("playlist_3", theme.playlist[3]);
    appendColor("playlist_4", theme.playlist[4]);
    appendColor("prst_button", theme.prst_button);
    appendColor("prst_card", theme.prst_card);
    appendColor("prst_accent", theme.prst_accent);
    appendColor("prst_fav", theme.prst_fav);
    appendColor("prst_title1", theme.prst_title1);
    appendColor("prst_title2", theme.prst_title2);
    appendColor("prst_title3", theme.prst_title3);
    appendColor("prst_line", theme.prst_line);

    out += "}";
    return out;
}

void Config::reset() {
    setDefaults();
    delay(500);
    ESP.restart();
}
void Config::enableScreensaver(bool val) {
    saveValue(&store.screensaverEnabled, val);
    display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverTimeout(uint16_t val) {
    val = constrain(val, 5, 65520);
    saveValue(&store.screensaverTimeout, val);
    display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverBlank(bool val) {
    saveValue(&store.screensaverBlank, val);
    display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverIdleBrightness(uint8_t val) {
    val = constrain(val, 0, 100);
    saveValue(&store.screensaverIdleBrightness, val);
    if (display.clockScreensaverBrightnessActive()) {
        display.setBrightnessPercent(display.effectiveBrightnessPercent(store.brightness));
    }
}
void Config::setScreensaverPlayingEnabled(bool val) {
    saveValue(&store.screensaverPlayingEnabled, val);
    display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverPlayingTimeout(uint16_t val) {
    val = constrain(val, 5, 65520);
    config.saveValue(&config.store.screensaverPlayingTimeout, val);
    display.putRequest(NEWMODE, PLAYER);
}
void Config::setScreensaverPlayingBlank(bool val) {
    saveValue(&store.screensaverPlayingBlank, val);
    display.putRequest(NEWMODE, PLAYER);
}
void Config::setSntpOne(const char* val) {
    bool tzdone = false;
    if (strlen(val) > 0 && strlen(store.sntp2) > 0) {
        configTime(store.tzHour * 3600 + store.tzMin * 60, getTimezoneOffset(), val, store.sntp2);
        tzdone = true;
    } else if (strlen(val) > 0) {
        configTime(store.tzHour * 3600 + store.tzMin * 60, getTimezoneOffset(), val);
        tzdone = true;
    }
    if (tzdone) {
        timekeeper.forceTimeSync = true;
        saveValue(config.store.sntp1, val, 35);
    }
}
void Config::setShowweather(bool val) {
    config.saveValue(&config.store.showweather, val);
    timekeeper.forceWeather = true;
    display.putRequest(SHOWWEATHER);
}
void Config::setWeatherKey(const char* val) {
    saveValue(store.weatherkey, val, WEATHERKEY_LENGTH);
    display.putRequest(NEWMODE, CLEAR);
    display.putRequest(NEWMODE, PLAYER);
}

#if IR_PIN != 255
void Config::setIrBtn(int val) {
    irBtnId = val;
    netserver.irRecordEnable = (irBtnId >= 0);
    irBankId = 0;
    netserver.irValsToWs(); // kiküldi a három mentett gombot a webszervernek
    IRCommand ircmd;
    if (val >= 0) {
        ircmd.irBtnId = val;   // a gombhoz tartozó index, -1 a mentéséshez
        ircmd.hasBtnId = true; // mentés engedélyezése
        ircmd.irBankId = 0;    // 0, 1, 2
        ircmd.hasBank = true;  // mentés engedélyezése
        xQueueSend(irQueue, &ircmd, 0);
        Serial.printf("config.cpp--> setIrBtn--> xQueueSend\n");
    } else {
        saveIR();
        Serial.println("config.cpp--> setIrBtn--> val: -1 (save)");
    }
}
#endif

void Config::resetSystem(const char* val, uint8_t clientId) {
    BOOTLOG("***************** RESET SYSTEM *****************");
    if (strcmp(val, "system") == 0) {
        saveValue(&store.smartstart, (uint8_t)2, false);
        saveValue(&store.audioinfo, false, false);
        saveValue(&store.vumeter, false, false);
        saveValue(&store.reservedVuPeak, true, false);
        saveValue(&store.reservedVuPeakInitMarker, static_cast<uint8_t>(0xA5), false);
        saveValue(&store.vuMidOn,      (uint8_t)1, false);
        saveValue(&store.vuPeakOn,     (uint8_t)1, false);
        saveValue(&store.vuMidPctDef,  (uint8_t)60, false);
        saveValue(&store.vuHighPctDef, (uint8_t)85, false);
        saveValue(&store.vuSpecMode,   (uint8_t)0, false);
        saveValue(&store.reservedVuBidirectional, false, false);
        saveValue(&store.softapdelay, (uint8_t)0, false);
        saveValue(&store.watchdog, true);
        saveValue(&store.stallWatchdog, true, false);
        saveValue(&store.serialLittlefsEnabled, true, false);
        saveValue(&store.nameday, true);
        saveValue(&store.clockTtsEnabled, false, false);
        saveValue(store.clockTtsLanguage, "HU", sizeof(store.clockTtsLanguage), false);
        saveValue(&store.clockTtsIntervalMinutes, static_cast<uint16_t>(15));
        saveValue(&store.clockTtsOnlyWhenNoStream, false, false);
        saveValue(&store.clockTtsQuietHoursEnabled, false, false);
        saveValue(&store.clockTtsQuietFromMinutes, static_cast<uint16_t>(23 * 60), false);
        saveValue(&store.clockTtsQuietToMinutes, static_cast<uint16_t>(7 * 60));
        saveValue(&store.clockFontStyle, static_cast<uint8_t>(CLOCKFONT_STYLE));
        saveValue(&store.clockFontMono, static_cast<bool>(CLOCKFONT_MONO_DEFAULT));
        saveValue(&store.clockAmPmStyle, static_cast<bool>(CLOCK_AM_PM_STYLE_DEFAULT));
        snprintf(store.mdnsname, MDNS_LENGTH, "FusionEdge-%x", (unsigned int)getChipId());
        saveValue(store.mdnsname, store.mdnsname, MDNS_LENGTH, true, true);
        display.putRequest(NEWMODE, CLEAR);
        display.putRequest(NEWMODE, PLAYER);
        netserver.requestOnChange(GETSYSTEM, clientId);
        return;
    }
    if (strcmp(val, "screen") == 0) {
        saveValue(&store.flipscreen, false, false);
        display.flip();
        saveValue(&store.invertdisplay, false, false);
        display.invert();
        display.flip();
        saveValue(&store.dspon, true, false);
        store.brightness = 100;
        setBrightness(false);
        saveValue(&store.contrast, (uint8_t)55, false);
        display.setContrast();
        saveValue(&store.numplaylist, false);
        saveValue(&store.playlistMovingCursor, false);
        saveValue(&store.directChannelChange, false);
        saveValue(&store.stationsListReturnTime, (uint8_t)3);
        saveValue(&store.screensaverEnabled, false);
        saveValue(&store.screensaverTimeout, (uint16_t)20);
        saveValue(&store.screensaverBlank, false);
        saveValue(&store.screensaverIdleBrightness, (uint8_t)100);
        saveValue(&store.screensaverPlayingEnabled, false);
        saveValue(&store.screensaverPlayingTimeout, (uint16_t)300);
        saveValue(&store.screensaverPlayingBlank, false);
        saveValue(&store.fadeEnabled, (uint8_t)FADE_ENABLED, true);
        saveValue(&store.fadeStartDelay, (uint16_t)FADE_START_DELAY, true);
        saveValue(&store.fadeTarget, (uint8_t)FADE_TARGET, true);
        saveValue(&store.fadeStep, (uint8_t)FADE_STEP, true);
        display.putRequest(NEWMODE, CLEAR);
        display.putRequest(NEWMODE, PLAYER);
        netserver.requestOnChange(GETSCREEN, clientId);
        return;
    }
    if (strcmp(val, "timezone") == 0) {
        saveValue(&store.tzHour, (int8_t)3, false);
        saveValue(&store.tzMin, (int8_t)0, false);
        saveValue(store.sntp1, "hu.pool.ntp.org", 35, false);
        saveValue(store.sntp2, "time.google.com", 35);
        saveValue(&store.timeSyncInterval, (uint16_t)60);
        saveValue(&store.timeSyncIntervalRTC, (uint16_t)24);
        configTime(store.tzHour * 3600 + store.tzMin * 60, getTimezoneOffset(), store.sntp1, store.sntp2);
        timekeeper.forceTimeSync = true;
        netserver.requestOnChange(GETTIMEZONE, clientId);
        return;
    }
    if (strcmp(val, "weather") == 0) {
        saveValue(&store.showweather, false, false);
        saveValue(store.weatherlat, "46.3873", 10, false);
        saveValue(store.weatherlon, "18.1513", 10, false);
        saveValue(store.weatherkey, "", WEATHERKEY_LENGTH);
        saveValue(&store.weatherSyncInterval, (uint16_t)30);
        // network.trueWeather=false;
        display.putRequest(NEWMODE, CLEAR);
        display.putRequest(NEWMODE, PLAYER);
        netserver.requestOnChange(GETWEATHER, clientId);
        return;
    }
    if (strcmp(val, "controls") == 0) {
        saveValue(&store.fliptouch, false, false);
        saveValue(&store.dbgtouch, false, false);
#if TS_MODEL == TS_MODEL_FT6X36
        saveValue(&store.xTouchMirroring, false, false);
        saveValue(&store.yTouchMirroring, false, false);
#else
        saveValue(&store.xTouchMirroring, true, false);
        saveValue(&store.yTouchMirroring, true, false);
#endif
        saveValue(&store.skipPlaylistUpDown, false);
        saveValue(&store.encodersIndependent, false);
        setIRTolerance(40);
        netserver.requestOnChange(GETCONTROLS, clientId);
        return;
    }
    if (strcmp(val, "1") == 0) {
        config.reset();
        return;
    }
}

void Config::setDefaults() {
    BOOTLOG("***************** SET DEFAULT *****************");
    store.config_set = 0;
    store.version = CONFIG_VERSION;
    store.volume = 6;
    store.balance = 0;
    store.trebble = 0;
    store.middle = 0;
    store.bass = 0;
    store.lastStation = 0;
    store.countStation = 0;
    store.lastSSID = 0;
    store.audioinfo = true;
    store.smartstart = 2;
    store.tzHour = 2;
    store.tzMin = 0;
    store.timezoneOffset = 0;
    store.vumeter = true;
    store.reservedVuBidirectional = false;
    store.softapdelay = 0;
    store.flipscreen = false;
    store.invertdisplay = false;
    store.numplaylist = true;
    store.fliptouch = false;
    store.dbgtouch = false;
    store.dspon = true;
    store.brightness = 100;
    store.contrast = 55;
    strlcpy(store.sntp1, "hu.pool.ntp.org", 35);
    strlcpy(store.sntp2, "time.google.com", 35);
    store.showweather  = false;
    store.lsEnabled    = 0;
    store.lsSsEnabled  = 0;
    store.lsModel      = 0;
    store.lsBrightness = 60;
    store.lsCount      = 24;
    store.shortWeather = false;
    strlcpy(store.weatherlat, "46.3873", 10);
    strlcpy(store.weatherlon, "18.1513", 10);
    strlcpy(store.weatherkey, "", WEATHERKEY_LENGTH);
    store.reservedVuPeak = true;
    store.reservedVuPeakInitMarker = 0xA5;
    store.vuMidOn        = 1;
    store.vuPeakOn       = 1;
    store.vuMidPctDef    = 60;
    store.vuHighPctDef   = 85;
    store.vuSpecMode     = 0;
    store.lastSdStation = 0;
    store.lastDlnaStation = 0; // DLNA mod
    store.sdsnuffle = false;
    store.play_mode = 0;
    store.irtlp = 35;
    store.btnpressticks = 500;
    store.screensaverEnabled = false;
    store.screensaverTimeout = 20;
    store.screensaverBlank = false;
    store.screensaverIdleBrightness = 100;
    snprintf(store.mdnsname, MDNS_LENGTH, "radio-%x", (unsigned int)getChipId());
    store.skipPlaylistUpDown = false;
    store.encodersIndependent = false;
    store.autoEqEnabled = false; // FusionEdge: Auto EQ alapból kikapcsolva
    store.screensaverPlayingEnabled = false;
    store.screensaverPlayingTimeout = 300;
    store.screensaverPlayingBlank = false;
    // store.abuff = VS1053_CS == 255 ? 7 : 10;
    store.watchdog = true;
    store.nameday = true;
    store.clockTtsEnabled = false;
    strlcpy(store.clockTtsLanguage, "HU", sizeof(store.clockTtsLanguage));
    store.clockTtsIntervalMinutes = 15;
    store.clockTtsOnlyWhenNoStream = false;
    store.clockTtsQuietHoursEnabled = false;
    store.clockTtsQuietFromMinutes = 23 * 60;
    store.clockTtsQuietToMinutes = 7 * 60;
    store.clockFontStyle = CLOCKFONT_STYLE;
    store.clockFontMono = CLOCKFONT_MONO_DEFAULT;
    store.clockAmPmStyle = CLOCK_AM_PM_STYLE_DEFAULT;
    store.timeSyncInterval = 60;    // min
    store.timeSyncIntervalRTC = 24; // hour
    store.weatherSyncInterval = 30; // min
    store.fadeEnabled = FADE_ENABLED;
    store.fadeStartDelay = FADE_START_DELAY;
    store.fadeTarget = FADE_TARGET;
    store.fadeStep = FADE_STEP;
    store.playlistSource = PL_SRC_WEB;
    store.dateFormat = 0;
    store.playlistMovingCursor = true;
    store.directChannelChange = true;
    store.stationsListReturnTime = 3;
    store.stallWatchdog = true;
    store.serialLittlefsEnabled = true;
#if TS_MODEL == TS_MODEL_FT6X36
    store.xTouchMirroring = false;
    store.yTouchMirroring = false;
#else
    store.xTouchMirroring = true;
    store.yTouchMirroring = false;
#endif

    store.rssiAsText = false;
    for (size_t i = 0; i < 21; ++i) {
        store.volumeCurveDb[i] = kDefaultVolumeCurveDb[i];
    }
    strlcpy(store.autoStartTime, "", sizeof(store.autoStartTime)); /* Auto On-Off Timer: empty = disabled */
    strlcpy(store.autoStopTime,  "", sizeof(store.autoStopTime));
    eepromWrite(EEPROM_START, store);
}

void Config::setTimezone(int8_t tzh, int8_t tzm) {
    saveValue(&store.tzHour, tzh, false);
    saveValue(&store.tzMin, tzm);
}

void Config::setTimezoneOffset(uint16_t tzo) {
    saveValue(&store.timezoneOffset, tzo);
}

uint16_t Config::getTimezoneOffset() {
    return 0; // TODO
}
// Véletlen lejátszás beállítása.
void Config::setSnuffle(bool sn) {
    saveValue(&store.sdsnuffle, sn);
    // if(store.sdsnuffle) player.next(); //Továbbléptette egy másik fájlra, ezért kivettem.
}

#if IR_PIN != 255
void Config::saveIR() {
    eepromWrite(EEPROM_START_IR, ircodes);
    Serial.println("IR codes saved to EEPROM");
}
#endif

void Config::saveVolume() {
    saveValue(&store.volume, store.volume, true, true);
}

uint8_t Config::setVolume(uint8_t val) {
    store.volume = val;
    display.putRequest(DRAWVOL);
    netserver.requestOnChange(VOLUME, 0);
    return store.volume;
}

void Config::setTone(int8_t bass, int8_t middle, int8_t trebble) {
    saveValue(&store.bass, bass, false);
    saveValue(&store.middle, middle, false);
    saveValue(&store.trebble, trebble);
    player.setTone(store.bass, store.middle, store.trebble);
    netserver.requestOnChange(EQUALIZER, 0);
}

void Config::setSmartStart(uint8_t ss) {
    saveValue(&store.smartstart, ss);
}

void Config::setBalance(int8_t balance) {
    saveValue(&store.balance, balance);
    player.setBalance(-store.balance); // "audio_change"  -16 to 16 fordítás 16 to -16
    netserver.requestOnChange(BALANCE, 0);
}

uint8_t Config::setLastStation(uint16_t val) {
    // Make "current item" persistent per mode
    if (getMode() == PM_SDCARD) {
        saveValue(&store.lastSdStation, val);
        return store.lastSdStation;
    }
#ifdef USE_DLNA
    if (store.playlistSource == PL_SRC_DLNA) {
        saveValue(&store.lastDlnaStation, val);
        return store.lastDlnaStation;
    }
#endif
    saveValue(&store.lastStation, val);
    return store.lastStation;
}

uint8_t Config::setCountStation(uint16_t val) {
    saveValue(&store.countStation, val);
    return store.countStation;
}

uint8_t Config::setLastSSID(uint8_t val) {
    saveValue(&store.lastSSID, val);
    return store.lastSSID;
}

void Config::setTitle(const char* title) {
    memset(config.station.title, 0, BUFLEN);
    strlcpy(config.station.title, title, BUFLEN);
    u8fix(config.station.title);
#ifdef USE_LASTFM_COVER
    coverArt.requestCombined(strcmp(config.station.title, config.station.name) == 0
                                 ? ""
                                 : config.station.title,
                             getMode() == PM_BLUETOOTH,
                             getMode() == PM_SDCARD);
#endif
    netserver.requestOnChange(TITLE, 0);
    // netserver.loop() szándékosan NINCS itt hívva — az azonnali WebSocket
    // flush WiFi DMA forgalmat generál miközben a display task SPI DMA-n
    // rajzol, ami GDMA konfliktust okoz → display megfagy. A requestOnChange
    // sorban vár, a következő netserver.loop() iteráció (main loop-ban) küldi.
    display.putRequest(NEWTITLE);
}

void Config::setStation(const char* station) {
    memset(config.station.name, 0, BUFLEN);
    strlcpy(config.station.name, station, BUFLEN);
    u8fix(config.station.name);
    display.putRequest(NEWSTATION);
    netserver.requestOnChange(STATION, 0);
}

void Config::indexPlaylist() {
    File playlist = LittleFS.open(PLAYLIST_PATH, "r");
    if (!playlist) { return; }
    int  sOvol;
    File index = LittleFS.open(INDEX_PATH, "w");
    while (playlist.available()) {
        uint32_t pos = playlist.position();
        if (parseCSV(playlist.readStringUntil('\n').c_str(), tmpBuf, sizeof(tmpBuf), tmpBuf2, sizeof(tmpBuf2), sOvol)) { index.write((uint8_t*)&pos, 4); }
    }
    index.close();
    playlist.close();
}

// DLNA mod
#ifdef USE_DLNA
void Config::indexDLNAPlaylist() {
    File playlist = LittleFS.open(PLAYLIST_DLNA_PATH, "r");
    if (!playlist) {
        Serial.println("[DLNA][IDX] Cannot open DLNA playlist");
        return;
    }

    File index = LittleFS.open(INDEX_DLNA_PATH, "w");
    if (!index) {
        Serial.println("[DLNA][IDX] Cannot create DLNA index");
        playlist.close();
        return;
    }

    static char lineBuf[512];
    int         sOvol = 0;

    uint32_t lines = 0;
    uint32_t ok = 0;

    while (playlist.available()) {
        uint32_t pos = playlist.position();

        // readBytesUntil nem allokál, stabil
        size_t n = playlist.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        lineBuf[n] = 0;

        // CRLF kezelés
        if (n > 0 && lineBuf[n - 1] == '\r') { lineBuf[n - 1] = 0; }

        // üres sor skip
        if (lineBuf[0] == 0) {
            lines++;
            continue;
        }

        // FONTOS: parseCSV kapjon ÍRHATÓ buffert (lineBuf), ne String.c_str()-t
        if (parseCSV(lineBuf, tmpBuf, sizeof(tmpBuf), tmpBuf2, sizeof(tmpBuf2), sOvol)) {
            index.write((uint8_t*)&pos, 4);
            ok++;
        }

        lines++;

        // A hívó lehet a DspTask is. Egy valódi ticknyi szünet kell, hogy az
        // IDLE0 fusson és az ESP32-S3 task watchdogot etesse.
        if ((lines % 32) == 0) {
            vTaskDelay(1);
        }
    }

    index.close();
    playlist.close();

    Serial.printf("[DLNA][IDX] DLNA playlist indexed: %lu/%lu\n", (unsigned long)ok, (unsigned long)lines);
}
#endif

void Config::initPlaylist() {
    // store.countStation = 0;
    if (!LittleFS.exists(INDEX_PATH)) { indexPlaylist(); }

    /*if (LittleFS.exists(INDEX_PATH)) {
      File index = LittleFS.open(INDEX_PATH, "r");
      store.countStation = index.size() / 4;
      index.close();
      saveValue(&store.countStation, store.countStation, true, true);
    }*/
}

#ifdef USE_DLNA // DLNA mod
void Config::initDLNAPlaylist() {
    // Feltöltés/build után a main loop már újraépíti az indexet. Módváltáskor
    // csak akkor dolgozzunk végig a teljes CSV-n, ha az index tényleg hiányzik.
    if (!LittleFS.exists(INDEX_DLNA_PATH)) { indexDLNAPlaylist(); }

    if (LittleFS.exists(INDEX_DLNA_PATH)) {
        File index = LittleFS.open(INDEX_DLNA_PATH, "r");
        if (index) {
            // lastStation(_randomStation());
            index.close();
        }
    }
}
#endif

uint16_t Config::playlistLength() {
    uint16_t out = 0;
    if (SDPLFS()->exists(REAL_INDEX)) {
        File index = SDPLFS()->open(REAL_INDEX, "r");
        out = index.size() / 4;
        index.close();
    }
    return out;
}
bool Config::loadStation(uint16_t ls) {
    int      sOvol;
    uint16_t cs = playlistLength();
    if (cs == 0) {
        memset(station.url, 0, BUFLEN);
        memset(station.name, 0, BUFLEN);
        strncpy(station.name, "FusionEdge", BUFLEN);
        memset(station.iconName, 0, BUFLEN);
        strncpy(station.iconName, "FusionEdge", BUFLEN);
        station.genre[0] = '\0';
        station.ovol = 0;
        return false;
    }
    if (ls > playlistLength()) { ls = 1; }
    File playlist = SDPLFS()->open(REAL_PLAYL, "r");
    File index = SDPLFS()->open(REAL_INDEX, "r");
    index.seek((ls - 1) * 4, SeekSet);
    uint32_t pos;
    index.readBytes((char*)&pos, 4);
    index.close();
    playlist.seek(pos, SeekSet);
    String csvLine = playlist.readStringUntil('\n');
    if (parseCSV(csvLine.c_str(), tmpBuf, sizeof(tmpBuf), tmpBuf2, sizeof(tmpBuf2), sOvol)) {
        memset(station.url, 0, BUFLEN);
        memset(station.name, 0, BUFLEN);
        strncpy(station.name, tmpBuf, BUFLEN);
        strncpy(station.url, tmpBuf2, BUFLEN);
        station.ovol = sOvol;

        // FusionEdge: station.iconName mindig a playlist-eredeti név, a vezető
        // "." nélkül – ez a StationIconWidget map.csv lookup-jának alapja.
        // Ez független a station.name későbbi META-alapú módosulásától.
        memset(station.iconName, 0, BUFLEN);
        if (tmpBuf[0] == '.') {
            strncpy(station.iconName, tmpBuf + 1, BUFLEN - 1);
        } else {
            strncpy(station.iconName, tmpBuf, BUFLEN);
        }

        // FusionEdge: station.genre a playlist CSV 4. mezőjéből (Auto EQ).
        parseCSVGenre(csvLine.c_str(), station.genre, sizeof(station.genre));

        setLastStation(ls);
    }
    playlist.close();
    return true;
}

char* Config::stationByNum(uint16_t num) {
    File playlist = SDPLFS()->open(REAL_PLAYL, "r");
    File index = SDPLFS()->open(REAL_INDEX, "r");
    index.seek((num - 1) * 4, SeekSet);
    uint32_t pos;
    memset(_stationBuf, 0, sizeof(_stationBuf));
    index.readBytes((char*)&pos, 4);
    index.close();
    playlist.seek(pos, SeekSet);
    strncpy(_stationBuf, playlist.readStringUntil('\t').c_str(), sizeof(_stationBuf));
    playlist.close();
    return _stationBuf;
}

void Config::escapeQuotes(const char* input, char* output, size_t maxLen) {
    size_t j = 0;
    for (size_t i = 0; input[i] != '\0' && j < maxLen - 1; ++i) {
        if (input[i] == '"' && j < maxLen - 2) {
            output[j++] = '\\';
            output[j++] = '"';
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

bool Config::parseCSV(const char* line, char* name, size_t nameSize, char* url, size_t urlSize, int& ovol) {
    char*       tmpe;
    const char* cursor = line;
    char        buf[5];
    if (!line || !name || !url || nameSize == 0 || urlSize == 0) { return false; }
    tmpe = strstr(cursor, "\t");
    if (tmpe == NULL) { return false; }
    size_t nameLen = static_cast<size_t>(tmpe - cursor);
    size_t nameCopyLen = min(nameLen, nameSize - 1);
    memcpy(name, cursor, nameCopyLen);
    name[nameCopyLen] = '\0';
    if (strlen(name) == 0) { return false; }
    cursor = tmpe + 1;
    tmpe = strstr(cursor, "\t");
    if (tmpe == NULL) { return false; }
    size_t urlLen = static_cast<size_t>(tmpe - cursor);
    size_t urlCopyLen = min(urlLen, urlSize - 1);
    memcpy(url, cursor, urlCopyLen);
    url[urlCopyLen] = '\0';
    if (strlen(url) == 0) { return false; }
    cursor = tmpe + 1;
    if (strlen(cursor) == 0) { return false; }
    strlcpy(buf, cursor, sizeof(buf));
    ovol = atoi(buf);
    return true;
}

// FusionEdge: a playlist CSV 4. mezőjének (genre) kiolvasása az Auto EQ
// funkcióhoz. A CSV formátum: name\turl\tovol\tgenre – ez a 3. tabulátor
// utáni rész. Ha nincs 4. mező vagy üres, genre[0]='\0'.
void Config::parseCSVGenre(const char* line, char* genre, size_t genreSize) {
    if (!genre || genreSize == 0) { return; }
    genre[0] = '\0';
    if (!line) { return; }

    const char* cursor = line;
    // 3 tabulátoron kell átlépni (name, url, ovol mezők után jön a genre)
    for (uint8_t i = 0; i < 3; i++) {
        cursor = strchr(cursor, '\t');
        if (!cursor) { return; } // nincs genre mező
        cursor++;
    }
    if (strlen(cursor) == 0) { return; }

    strlcpy(genre, cursor, genreSize);

    // Sorvégi CR/LF és felesleges whitespace levágása
    size_t len = strlen(genre);
    while (len > 0 && (genre[len-1] == '\r' || genre[len-1] == '\n' ||
                        genre[len-1] == ' '  || genre[len-1] == '\t')) {
        genre[--len] = '\0';
    }
}

bool Config::parseJSON(const char* line, char* name, size_t nameSize, char* url, size_t urlSize, int& ovol) {
    char *      tmps, *tmpe;
    const char* cursor = line;
    char        port[8], host[246], file[254];
    if (!line || !name || !url || nameSize == 0 || urlSize == 0) { return false; }
    tmps = strstr(cursor, "\":\"");
    if (tmps == NULL) { return false; }
    tmpe = strstr(tmps, "\",\"");
    if (tmpe == NULL) { return false; }
    size_t nameLen = static_cast<size_t>(tmpe - (tmps + 3));
    size_t nameCopyLen = min(nameLen, nameSize - 1);
    memcpy(name, tmps + 3, nameCopyLen);
    name[nameCopyLen] = '\0';
    if (strlen(name) == 0) { return false; }
    cursor = tmpe + 3;
    tmps = strstr(cursor, "\":\"");
    if (tmps == NULL) { return false; }
    tmpe = strstr(tmps, "\",\"");
    if (tmpe == NULL) { return false; }
    size_t hostLen = static_cast<size_t>(tmpe - (tmps + 3));
    size_t hostCopyLen = min(hostLen, sizeof(host) - 1);
    memcpy(host, tmps + 3, hostCopyLen);
    host[hostCopyLen] = '\0';
    if (strlen(host) == 0) { return false; }
    if (strstr(host, "http://") == NULL && strstr(host, "https://") == NULL) {
        sprintf(file, "http://%s", host);
        strlcpy(host, file, sizeof(host));
    }
    cursor = tmpe + 3;
    tmps = strstr(cursor, "\":\"");
    if (tmps == NULL) { return false; }
    tmpe = strstr(tmps, "\",\"");
    if (tmpe == NULL) { return false; }
    size_t fileLen = static_cast<size_t>(tmpe - (tmps + 3));
    size_t fileCopyLen = min(fileLen, sizeof(file) - 1);
    memcpy(file, tmps + 3, fileCopyLen);
    file[fileCopyLen] = '\0';
    cursor = tmpe + 3;
    tmps = strstr(cursor, "\":\"");
    if (tmps == NULL) { return false; }
    tmpe = strstr(tmps, "\",\"");
    if (tmpe == NULL) { return false; }
    size_t portLen = static_cast<size_t>(tmpe - (tmps + 3));
    size_t portCopyLen = min(portLen, sizeof(port) - 1);
    memcpy(port, tmps + 3, portCopyLen);
    port[portCopyLen] = '\0';
    int p = atoi(port);
    if (p > 0) {
        snprintf(url, urlSize, "%s:%d%s", host, p, file);
    } else {
        snprintf(url, urlSize, "%s%s", host, file);
    }
    cursor = tmpe + 3;
    tmps = strstr(cursor, "\":\"");
    if (tmps == NULL) { return false; }
    tmpe = strstr(tmps, "\"}");
    if (tmpe == NULL) { return false; }
    portLen = static_cast<size_t>(tmpe - (tmps + 3));
    portCopyLen = min(portLen, sizeof(port) - 1);
    memcpy(port, tmps + 3, portCopyLen);
    port[portCopyLen] = '\0';
    ovol = atoi(port);
    return true;
}

bool Config::parseWsCommand(const char* line, char* cmd, char* val, uint8_t cSize) {
    char* tmpe;
    if (!line || !cmd || !val || cSize == 0) { return false; }
    tmpe = strstr(line, "=");
    if (tmpe == NULL) { return false; }
    memset(cmd, 0, cSize);
    size_t cmdLen = static_cast<size_t>(tmpe - line);
    size_t cmdCopyLen = min(cmdLen, static_cast<size_t>(cSize - 1));
    memcpy(cmd, line, cmdCopyLen);
    cmd[cmdCopyLen] = '\0';
    // if (strlen(tmpe + 1) == 0) return false;
    memset(val, 0, cSize);
    strlcpy(val, tmpe + 1, cSize);
    return true;
}

bool Config::parseSsid(const char* line, char* ssid, char* pass) {
    char* tmpe;
    if (!line || !ssid || !pass) { return false; }
    tmpe = strstr(line, "\t");
    if (tmpe == NULL) { return false; }
    uint16_t pos = tmpe - line;
    if (pos > 29 || strlen(line) > 71) { return false; }
    memset(ssid, 0, 30);
    size_t ssidCopyLen = min(static_cast<size_t>(pos), static_cast<size_t>(29));
    memcpy(ssid, line, ssidCopyLen);
    ssid[ssidCopyLen] = '\0';
    memset(pass, 0, 40);
    strlcpy(pass, line + pos + 1, 40);
    return true;
}

bool Config::saveWifiFromNextion(const char* post) {
    if (!LittleFS.exists("/data")) { LittleFS.mkdir("/data"); }
    File file = LittleFS.open(SSIDS_PATH, "w");
    if (!file) {
        Serial.printf("[WIFI] saveWifiFromNextion: cannot open %s for write\n", SSIDS_PATH);
        return false;
    } else {
        file.print(post);
        file.close();
        ESP.restart();
        return true;
    }
}

bool Config::saveWifi() {
    if (!LittleFS.exists("/data")) { LittleFS.mkdir("/data"); }
    if (!LittleFS.exists(TMP_PATH)) { return false; }
    LittleFS.remove(SSIDS_PATH);
    LittleFS.rename(TMP_PATH, SSIDS_PATH);
    ESP.restart();
    return true;
}

void Config::setTimeConf() {
    if (strlen(store.sntp1) > 0 && strlen(store.sntp2) > 0) {
        configTime(store.tzHour * 3600 + store.tzMin * 60, getTimezoneOffset(), store.sntp1, store.sntp2);
    } else if (strlen(store.sntp1) > 0) {
        configTime(store.tzHour * 3600 + store.tzMin * 60, getTimezoneOffset(), store.sntp1);
    }
}

bool Config::initNetwork() {
    File file = LittleFS.open(SSIDS_PATH, "r");
    if (!file || file.isDirectory()) { return false; }
    char    ssidval[30], passval[40];
    uint8_t c = 0;
    while (file.available()) {
        if (parseSsid(file.readStringUntil('\n').c_str(), ssidval, passval)) {
            strlcpy(ssids[c].ssid, ssidval, 30);
            strlcpy(ssids[c].password, passval, 40);
            ssidsCount++;
            c++;
        }
    }
    file.close();
    return true;
}

void Config::setBrightness(bool dosave) {
    if (!store.dspon && dosave) { display.wakeup(); }
    display.setBrightnessPercent(store.brightness);
    if (!store.dspon) { store.dspon = true; }
    if (dosave) {
        saveValue(&store.brightness, store.brightness, false, true);
        saveValue(&store.dspon, store.dspon, true, true);
    }
#ifdef USE_NEXTION
    nextion.wake();
    char cmd[15];
    snprintf(cmd, 15, "dims=%d", store.brightness);
    nextion.putcmd(cmd);
    if (!store.dspon) { store.dspon = true; }
    if (dosave) {
        saveValue(&store.brightness, store.brightness, false, true);
        saveValue(&store.dspon, store.dspon, true, true);
    }
#endif
}

void Config::setDspOn(bool dspon, bool saveval) {
    if (saveval) {
        store.dspon = dspon;
        saveValue(&store.dspon, store.dspon, true, true);
    }
#ifdef USE_NEXTION
    if (!dspon) {
        nextion.sleep();
    } else {
        nextion.wake();
    }
#endif
    if (!dspon) {
#if BRIGHTNESS_PIN != 255
        analogWrite(BRIGHTNESS_PIN, 0);
#endif
        display.deepsleep();
    } else {
        display.wakeup();
#if BRIGHTNESS_PIN != 255
        analogWrite(BRIGHTNESS_PIN, map(store.brightness, 0, 100, 0, 255));
#endif
    }
}

void Config::doSleep() {
    if (BRIGHTNESS_PIN != 255) { analogWrite(BRIGHTNESS_PIN, 0); }
    display.deepsleep();
#ifdef USE_NEXTION
    nextion.sleep();
#endif
    uint64_t mask = 0;
#if WAKE_PIN1 >= 0 && WAKE_PIN1 < 64
    if (rtc_gpio_is_valid_gpio((gpio_num_t)WAKE_PIN1)) {
        rtc_gpio_pullup_en((gpio_num_t)WAKE_PIN1);
        rtc_gpio_pulldown_dis((gpio_num_t)WAKE_PIN1);
        mask |= (1ULL << WAKE_PIN1);
    }
#endif
#if WAKE_PIN2 >= 0 && WAKE_PIN2 < 64
    if (rtc_gpio_is_valid_gpio((gpio_num_t)WAKE_PIN2)) {
        rtc_gpio_pullup_en((gpio_num_t)WAKE_PIN2);
        rtc_gpio_pulldown_dis((gpio_num_t)WAKE_PIN2);
        mask |= (1ULL << WAKE_PIN2);
    }
#endif
    if (mask != 0) {
#if CONFIG_IDF_TARGET_ESP32
        // Classic ESP32 supports ALL_LOW and ANY_HIGH only. With the existing
        // pull-ups, active-low wake inputs therefore use ALL_LOW.
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
#else
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    }
    esp_sleep_enable_timer_wakeup(config.sleepfor * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void Config::doSleepW() {
    analogWrite(BRIGHTNESS_PIN, 0);           // ← add (MB)
    pinMode(BRIGHTNESS_PIN, OUTPUT);          // ← add (MB)
    digitalWrite(BRIGHTNESS_PIN, LOW);        // ← add (MB)
    gpio_hold_en((gpio_num_t)BRIGHTNESS_PIN); // ← add (MB)
    gpio_deep_sleep_hold_en();                // ← add (MB)
    display.deepsleep();
    
#ifdef USE_NEXTION
        nextion.sleep();
#endif
    uint64_t mask = 0;
#if WAKE_PIN1 >= 0 && WAKE_PIN1 < 64
    if (rtc_gpio_is_valid_gpio((gpio_num_t)WAKE_PIN1)) {
        rtc_gpio_pullup_en((gpio_num_t)WAKE_PIN1);
        rtc_gpio_pulldown_dis((gpio_num_t)WAKE_PIN1);
        mask |= (1ULL << WAKE_PIN1);
    }
#endif
#if WAKE_PIN2 >= 0 && WAKE_PIN2 < 64
    if (rtc_gpio_is_valid_gpio((gpio_num_t)WAKE_PIN2)) {
        rtc_gpio_pullup_en((gpio_num_t)WAKE_PIN2);
        rtc_gpio_pulldown_dis((gpio_num_t)WAKE_PIN2);
        mask |= (1ULL << WAKE_PIN2);
    }
#endif
    delay(200);
    if (mask != 0) {
#if CONFIG_IDF_TARGET_ESP32
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
#else
        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    }
    esp_deep_sleep_start();
}

void Config::sleepForAfter(uint16_t sf, uint16_t sa) {
    sleepfor = sf;
    if (sa > 0) {
        timekeeper.waitAndDo(sa * 60, doSleep);
    } else {
        doSleep();
    }
}

/*----- number to formated string -----*/
const char* fmtThousands(uint32_t v) {
    static char buf[16];
    char        tmp[16];
    sprintf(tmp, "%lu", v);

    int len = strlen(tmp);
    int pos = len % 3;
    int j = 0;

    for (int i = 0; i < len; i++) {
        if (i && (i % 3) == pos) buf[j++] = ' ';
        buf[j++] = tmp[i];
    }
    buf[j] = 0;

    return buf;
}

void Config::bootInfo() {
    BOOTLOG("************************************************");
    BOOTLOG("*            FusionEdge v%s                    *", FW_VERSION);
    BOOTLOG("************************************************");
    BOOTLOG("------------------------------------------------");
    BOOTLOG("arduino:\t%d", ARDUINO);
    BOOTLOG("compiler:\t%s", __VERSION__);
    BOOTLOG("esp32core:\t%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i = i + 8) { chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i; }
    BOOTLOG("chip:\t\tmodel: %s | rev: %d | id: %lu | cores: %d | psram: %lu", ESP.getChipModel(), ESP.getChipRevision(), chipId, ESP.getChipCores(), ESP.getPsramSize());
    BOOTLOG("display:\t%d", DSP_MODEL);
    BOOTLOG("audio:\t\t%s (%d, %d, %d, mclk:%d)", "I2S", I2S_DOUT, I2S_BCLK, I2S_LRC, I2S_MCLK);
    BOOTLOG("audioinfo:\t%s", store.audioinfo ? "true" : "false");
    BOOTLOG("smartstart:\t%d", store.smartstart);
    BOOTLOG("autoeq:\t%s", store.autoEqEnabled ? "true" : "false");
    BOOTLOG("vumeter:\t%s", store.vumeter ? "true" : "false");
    BOOTLOG("softapdelay:\t%d", store.softapdelay);
    BOOTLOG("flipscreen:\t%s", store.flipscreen ? "true" : "false");
    BOOTLOG("invertdisplay:\t%s", store.invertdisplay ? "true" : "false");
    BOOTLOG("showweather:\t%s", store.showweather ? "true" : "false");
    BOOTLOG("buttons:\tleft=%d, center=%d, right=%d, up=%d, down=%d, mode=%d, pullup=%s", BTN_LEFT, BTN_CENTER, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_MODE, BTN_INTERNALPULLUP ? "true" : "false");
    BOOTLOG("encoders:\tl1=%d, b1=%d, r1=%d, pullup=%s, l2=%d, b2=%d, r2=%d, pullup=%s", ENC_BTNL, ENC_BTNB, ENC_BTNR, ENC_INTERNALPULLUP ? "true" : "false", ENC2_BTNL, ENC2_BTNB, ENC2_BTNR,
            ENC2_INTERNALPULLUP ? "true" : "false");
    BOOTLOG("ir:\t\t%d", IR_PIN);
    if (SDC_CS != 255) { BOOTLOG("SD:\t\t%d", SDC_CS); }

    BOOTLOG("------------------------------------------------");
    BOOTLOG("CONFIG:\tsizeof(store)=%u B | EEPROM_START=%u | EEPROM_END=%u | EEPROM_SIZE=%u", (unsigned)sizeof(config.store), (unsigned)EEPROM_START, (unsigned)(EEPROM_START + sizeof(config.store)),
            (unsigned)EEPROM_SIZE);
    BOOTLOG("------------------------------------------------");
    BOOTLOG("------------- EEPROM AFTER READ ----------------");
    BOOTLOG("fadeEnabled   : %s", store.fadeEnabled ? "true" : "false");
    BOOTLOG("fadeStartDelay: %4s", fmtThousands(store.fadeStartDelay));
    BOOTLOG("fadeTarget    : %4s", fmtThousands(store.fadeTarget));
    BOOTLOG("fadeStep      : %4s", fmtThousands(store.fadeStep));
    BOOTLOG("Serial LittleFS : %s", store.serialLittlefsEnabled ? "true" : "false");
    BOOTLOG("------------------------------------------------");
    BOOTLOG("----------------- HEAP AND PSRAM ---------------");
    BOOTLOG("Total heap : %10s byte", fmtThousands(ESP.getHeapSize()));
    BOOTLOG("Free heap  : %10s byte", fmtThousands(ESP.getFreeHeap()));
    BOOTLOG(psramFound() ? "✅ PSRAM found!" : "❌ PSRAM not found!");
    BOOTLOG("Total PSRAM: %10s byte", fmtThousands(ESP.getPsramSize()));
    BOOTLOG("Free PSRAM : %10s byte", fmtThousands(ESP.getFreePsram()));
    BOOTLOG("------------------------------------------------");
}

// ══════════════════════════════════════════════════════════════════════════════
// Volume Curve implementáció (VTom v0.1.4 portolás)
// ══════════════════════════════════════════════════════════════════════════════

void Config::setDefaultVolumeCurve() {
    for (size_t i = 0; i < 21; ++i) {
        store.volumeCurveDb[i] = kDefaultVolumeCurveDb[i];
        saveValue(&store.volumeCurveDb[i], store.volumeCurveDb[i], false, true);
    }
    EEPROM.commit();
}

String Config::volumeCurveToCsv() const {
    String out;
    out.reserve(320);
    out += "step,db\n";
    for (int i = 1; i <= 21; ++i) {
        out += String(i);
        out += ",";
        out += String((int)store.volumeCurveDb[i - 1]);
        out += "\n";
    }
    return out;
}

bool Config::saveVolumeCurveToFile(const char* path) {
    if (!path || !*path) { return false; }
    if (!LittleFS.exists("/data")) { LittleFS.mkdir("/data"); }
    File file = LittleFS.open(path, "w");
    if (!file) { return false; }
    file.print(volumeCurveToCsv());
    file.close();
    return true;
}

bool Config::applyVolumeCurveCsv(const char* csvData, String* errorOut) {
    if (errorOut) { *errorOut = ""; }
    if (!csvData) {
        if (errorOut) { *errorOut = "csv data is empty"; }
        return false;
    }

    int8_t parsed[21] = {0};
    bool   seen[21]   = {false};
    int    count      = 0;
    int    lineNo     = 0;
    bool   anyNonMinusOne = false;

    String content(csvData);
    int    start = 0;
    while (start <= (int)content.length()) {
        int end = content.indexOf('\n', start);
        if (end < 0) { end = content.length(); }

        String line = content.substring(start, end);
        start = end + 1;
        ++lineNo;
        line.trim();
        if (!line.length()) { continue; }
        line.replace(";", ",");

        int step = 0;
        float db = 0.0f;
        if (sscanf(line.c_str(), " %d , %f", &step, &db) != 2) {
            if (line.startsWith("step") || line.startsWith("Step")) { continue; }
            if (errorOut) { *errorOut = "line " + String(lineNo) + ": invalid format, expected step,db"; }
            return false;
        }
        if (step < 1 || step > 21) {
            if (errorOut) { *errorOut = "line " + String(lineNo) + ": step out of range (1..21)"; }
            return false;
        }
        int dbInt = (int)db;
        if (dbInt < -60) dbInt = -60;
        if (dbInt > 0)   dbInt = 0;
        if (dbInt != -1) { anyNonMinusOne = true; }
        if (!seen[step - 1]) { seen[step - 1] = true; ++count; }
        parsed[step - 1] = (int8_t)dbInt;
    }

    if (!anyNonMinusOne) {
        if (errorOut) { *errorOut = "all values are -1, not a valid curve"; }
        return false;
    }
    if (count < 21) {
        // Hiányzó lépések: default értékekkel kitöltjük
        for (int i = 0; i < 21; ++i) {
            if (!seen[i]) { parsed[i] = kDefaultVolumeCurveDb[i]; }
        }
    }

    // Mentés EEPROM-ba és alkalmazás
    for (size_t i = 0; i < 21; ++i) {
        saveValue(&store.volumeCurveDb[i], parsed[i], false, true);
    }
    EEPROM.commit();

    float lut[22];
    lut[0] = -60.0f;
    for (int i = 1; i <= 21; ++i) {
        lut[i] = (float)store.volumeCurveDb[i - 1];
    }
    player.setVolumeCurveDbLut(lut, 22);
    netserver.requestOnChange(GETVOLCURVE, 0);
    return true;
}

bool Config::loadVolumeCurveFromFile(const char* path) {
    if (!path || !*path || !LittleFS.exists(path)) { return false; }
    File file = LittleFS.open(path, "r");
    if (!file) { return false; }

    String csv;
    csv.reserve(file.size() + 1);
    while (file.available()) {
        csv += file.readStringUntil('\n');
        csv += '\n';
    }
    file.close();

    String importError;
    if (applyVolumeCurveCsv(csv.c_str(), &importError)) {
        return true;
    }

    Serial.printf("[VOLCURVE] Invalid curve file: %s\n", importError.c_str());
    setDefaultVolumeCurve();
    for (size_t i = 0; i < 21; ++i) {
        saveValue(&store.volumeCurveDb[i], store.volumeCurveDb[i], false, true);
    }
    EEPROM.commit();
    saveVolumeCurveToFile(path);
    return false;
}
