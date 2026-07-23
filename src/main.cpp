#include "Arduino.h"
#include "core/clock_tts.h" // "clock_tts"
#include "core/options.h"
#include "core/config.h"
#include "pluginsManager/pluginsManager.h"
#include "plugins/backlight/backlight.h" // backlight plugin
#include "plugins/ledstrip/ledstrip.h" // ledstrip plugin
#include "core/player.h"
#include "core/display.h"
#include "core/network.h"
#include "core/netserver.h"
#include "core/controls.h"
#ifdef USE_BLUETOOTH
#    include "core/bluetooth.h"
#endif
// #include "core/mqtt.h"
#include "driver/rtc_io.h"
#include "core/serial_littlefs.h"

#include "core/optionschecker.h"
#include "core/timekeeper.h"
#ifdef USE_NEXTION
#    include "displays/nextion.h"
#endif
#include "core/audiohandlers.h" //"audio_change"
#ifdef USE_DLNA                 // DLNA mod
#    include "dlna/dlna_service.h"
#    include "dlna/dlna_worker.h"
extern volatile bool g_dlnaPlaylistDirty;
#endif
#if USE_OTA
#    if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#        include <NetworkUdp.h>
#    else
#        include <WiFiUdp.h>
#    endif
#    include <ArduinoOTA.h>
#endif

extern __attribute__((weak)) void radio_on_setup();

#if USE_OTA
void setupOTA() {
    if (strlen(config.store.mdnsname) > 0) ArduinoOTA.setHostname(config.store.mdnsname);
#    ifdef OTA_PASS
    ArduinoOTA.setPassword(OTA_PASS);
#    endif
    ArduinoOTA
        .onStart([]() {
            player.sendCommand({PR_STOP, 0});
            display.putRequest(NEWMODE, UPDATING);
            Serial.printf("Start OTA updating %s\r\n", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
        })
        .onEnd([]() {
            Serial.printf("\nEnd OTA update, Rebooting...\r\n");
            ESP.restart();
        })
        .onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress OTA: %u%%\r", (progress / (total / 100))); })
        .onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) {
                Serial.printf("Auth Failed\r\n");
            } else if (error == OTA_BEGIN_ERROR) {
                Serial.printf("Begin Failed\r\n");
            } else if (error == OTA_CONNECT_ERROR) {
                Serial.printf("Connect Failed\r\n");
            } else if (error == OTA_RECEIVE_ERROR) {
                Serial.printf("Receive Failed\r\n");
            } else if (error == OTA_END_ERROR) {
                Serial.printf("End Failed\r\n");
            }
        });
    ArduinoOTA.begin();
}
#endif

#if IR_PIN != 255
#    include "IRremoteESP8266/IRrecv.h"
#    include "IRremoteESP8266/IRutils.h"

extern IRrecv         irrecv;
extern decode_results irResults;
#endif

static TaskHandle_t clockTtsTaskHandle = nullptr;
static bool         serialLittlefsEnabled = true;

static bool loadSerialLittlefsEnabledFromEeprom() {
    config.eepromRead(EEPROM_START, config.store);
    if (config.store.config_set != 0) return true;
    if (config.store.version != CONFIG_VERSION) return true;
    return config.store.serialLittlefsEnabled;
}

static void clockTtsTask(void* /*param*/) {
    while (true) {
        clock_tts_loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void startClockTtsTask() {
    if (clockTtsTaskHandle != nullptr) return;
#if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t targetCore = 0;
#else
    constexpr BaseType_t targetCore = 1;
#endif
    xTaskCreatePinnedToCore(clockTtsTask, "clock_tts", 4096, nullptr, 1, &clockTtsTaskHandle, targetCore);
}

static void hideDisplayBacklight() {
#if BRIGHTNESS_PIN != 255
    display.setBrightnessPercent(0);
#endif
}

static void revealDisplayBacklight(bool forceOn) {
#if BRIGHTNESS_PIN != 255
    uint8_t targetBrightness = 0;

    if (forceOn) {
        targetBrightness = config.store.brightness > 0 ? config.store.brightness : 100;
    } else if (config.store.dspon) {
        targetBrightness = config.store.brightness;
    }

    if (targetBrightness == 0) return;

    constexpr uint8_t fadeStep = 2;
    for (uint8_t level = 0; level < targetBrightness;) {
        display.setBrightnessPercent(level);
        delay(12);
        uint8_t nextLevel = level + fadeStep;
        level = nextLevel > targetBrightness ? targetBrightness : nextLevel;
    }
    display.setBrightnessPercent(targetBrightness);
#else
    (void)forceOn;
#endif
}

static bool serviceMaintenanceMode() {
    if (!serialLittlefsEnabled) return false;
    static bool maintenanceScreenShown = false;
    serial_littlefs_poll();
    if (!serial_littlefs_is_active()) {
        maintenanceScreenShown = false;
        return false;
    }
    if (!maintenanceScreenShown) {
        display_show_maintenance_screen();
        maintenanceScreenShown = true;
    }
    delay(2);
    return true;
}

void setup() {
    Serial.begin(460800); 
    delay(100);
    EEPROM.begin(EEPROM_SIZE);
    serial_littlefs_begin(Serial);
    serialLittlefsEnabled = loadSerialLittlefsEnabledFromEeprom();
    
#if IR_PIN != 255
    irQueue = xQueueCreate(4, sizeof(IRCommand));
    config.eepromRead(EEPROM_START_IR, config.ircodes);
    irWakeup();
#endif

    if (serialLittlefsEnabled) {
        // Boot window: ESP 4 masodpercig varja a maintenance kapcsolatot.
        // A Python HELLO -> BEGIN kezfogassal lep be; END paranccsal vagy idle timeout
        // utan a maintenance mod le is tud zarni, es a normal boot folytatodik.
        {
            uint32_t bootEnd = millis() + 4000;
            while (millis() < bootEnd) {
                serial_littlefs_poll();
                if (serial_littlefs_is_active()) {
                    display_show_maintenance_screen();
                    while (serial_littlefs_is_active()) {
                        serial_littlefs_poll();
                        delay(1);
                    }
                    break; // END parancs vagy idle timeout utan folytatja a normal bootot
                }
                delay(5);
            }
        }
    }

#if (BRIGHTNESS_PIN != 255) // backlight plugin
    Serial.printf("Exists? %p\n", &backlightPlugin);
    backlightPluginInit();
#endif
    ledstripPluginInit();
    if (REAL_LEDBUILTIN != 255) pinMode(REAL_LEDBUILTIN, OUTPUT);
    if (radio_on_setup) radio_on_setup();
    pm.init();     // pluginsManager
    pm.on_setup(); // pluginsManager
    config.init();
    serialLittlefsEnabled = config.store.serialLittlefsEnabled;
    display.init();
    revealDisplayBacklight(false);

    player.init();
    network.begin();
    if (network.status != CONNECTED && network.status != SDREADY) {
        netserver.begin();
        initControls();
        display.putRequest(DSP_START);
        while (!display.ready()) {
            if (serviceMaintenanceMode()) continue;
            delay(10);
        }
        revealDisplayBacklight(true);
        return;
    }
    if (SDC_CS != 255) {
        display.putRequest(WAITFORSD, 0);
        Serial.print("##[BOOT]#\tSD search\t");
    }
    config.initPlaylistMode();
    netserver.begin();
    initControls();
    hideDisplayBacklight();
    display.putRequest(DSP_START);
    while (!display.ready()) {
        if (serviceMaintenanceMode()) continue;
        delay(10);
    }
    revealDisplayBacklight(false);
#if USE_OTA
    setupOTA();
#endif
    if (config.getMode() == PM_SDCARD) player.initHeaders(config.station.url);
    player.lockOutput = false;
    if (config.store.smartstart == 1
#ifdef USE_BLUETOOTH
        && config.getMode() != PM_BLUETOOTH
#endif
    ) {
        player.sendCommand({PR_PLAY, config.lastStation()});
    }
    clock_tts_setup();
    Audio::audio_info_callback = my_audio_info; // "audio_change" audiohandlers.h ban kezelve.
    Audio::spi_lock_callback = [](bool lock) {
        if (lock) {
            display.i2sReconfigBegin();
        } else {
            display.i2sReconfigEnd();
        }
    };
    startClockTtsTask();
#ifdef USE_BLUETOOTH
    bluetooth.begin();
    // BT módban boot: pmodeWidget + állomásnév frissítése (changeMode() nem fut boot közben)
    if (config.getMode() == PM_BLUETOOTH) {
        display.putRequest(NEWSTATION);
        display.putRequest(NEWTITLE);
        display.putRequest(DBITRATE);
        if (!bluetooth.startBridge()) {
            log_e("##[BT]# startBridge() failed on boot!");
        } else {
            log_i("##[BT]# bridge started on boot");
        }
    }
#endif
    if (POWER_LED != 255) {
        pinMode(POWER_LED, OUTPUT);
        digitalWrite(POWER_LED, HIGH);
    }
    pm.on_end_setup();
}

void loop() {
    pm.on_loop();
    if (serviceMaintenanceMode()) return;

    timekeeper.loop1();

#ifdef USE_DLNA
    // DLNA build/append után runtime playlist refresh (main context-ben!)
    static uint32_t dlnaReloadAt = 0;

    if (g_dlnaPlaylistDirty) {
        g_dlnaPlaylistDirty = false;
        dlnaReloadAt = millis() + 150; // kis debounce/flush idő LittleFS rename után
    }

    if (dlnaReloadAt && (int32_t)(millis() - dlnaReloadAt) >= 0) {
        dlnaReloadAt = 0;

        // újratölti a playlist/index cache-t → playlistLength() innentől nem 0
        config.indexDLNAPlaylist();

        // WebUI/index frissítés
        netserver.resetQueue();
        netserver.requestOnChange(GETINDEX, 0);

        // ha épp DLNA módban van, frissítsd a képernyőt is
        if (config.getMode() == PM_WEB &&
            config.store.playlistSource == (uint8_t)PL_SRC_DLNA) {
            display.purgeQueuedRequestType(NEWMODE);
            display.purgeQueuedRequestType(NEWSTATION);
            display.putRequest(NEWMODE, PLAYER);
            display.putRequest(NEWSTATION);
        }
    }
#endif

    if (network.status == CONNECTED || network.status == SDREADY) {
        player.loop();
#ifdef USE_BLUETOOTH
        bluetooth.loop();
#endif
#if USE_OTA
        ArduinoOTA.handle();
#endif
    }
    loopControls();
#ifdef NETSERVER_LOOP1
    netserver.loop();
#endif
}
