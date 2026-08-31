#include "Arduino.h"
#include "options.h"
#include "WiFi.h"
#include "time.h"
#include "config.h"
#include "display.h"
#include "presets.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#include "timekeeper.h"
#include "../pluginsManager/pluginsManager.h"
#include "../displays/display_select.h"
#include "../displays/widgets/widgets.h"
#include "../displays/widgets/iconWidgets.h"
#include "../displays/widgets/spectrumWidget.h"
#include "../core/spectrumAnalyzer.h"
#include "../displays/widgets/eqWidget.h"
#include "../displays/widgets/pages.h"
#include "../displays/tools/language.h"
#include "fonts.h"
#include "speaker_bitmaps.h"
#include "freertos/semphr.h"
#ifdef USE_LASTFM_COVER
#    include "coverart.h"
#endif

// Inline overlay sáv koordináták (elválasztóvonal alatt, wifi/vol widget felett)
#define VOL_AREA_TOP    137
#define VOL_AREA_HEIGHT 149   // 286 - 137
#define VOL_AREA_LEFT     0
#define VOL_AREA_WIDTH  480

// Playlist window height. On ILI9341 (320x240) the shared VOL_AREA sizing
// (top=137, height=149 -> bottom=286) runs past the bottom of the 240px-tall
// panel, so the last couple of stations were drawn off-screen. Clamp the
// playlist area to the actual screen bottom so nothing gets clipped, which
// also has the effect of showing 2 fewer stations at a time. Other displays
// keep the original VOL_AREA_HEIGHT.
#if DSP_MODEL == DSP_ILI9341 || DSP_MODEL == DSP_ST7789
#    define PLAYLIST_AREA_HEIGHT (DSP_HEIGHT - VOL_AREA_TOP) // fits flush to screen bottom
#else
#    define PLAYLIST_AREA_HEIGHT VOL_AREA_HEIGHT
#endif
#include "serial_littlefs.h"
#include "touchscreen.h"
// #define LGFX_USE_PNG

Display display;
#ifdef USE_NEXTION
#    include "../displays/nextion.h"
Nextion nextion;
#endif

#ifndef CORE_STACK_SIZE
#    define CORE_STACK_SIZE 1024 * 8 // 4
#endif
#ifndef DSP_TASK_PRIORITY
#    if DSP_MODEL == DSP_AXS15231B
#        define DSP_TASK_PRIORITY 2
#    else
#        define DSP_TASK_PRIORITY 3 //"task_prioritas"
#    endif
#endif
#ifndef DSP_TASK_CORE_ID
#    define DSP_TASK_CORE_ID 0
#endif
#ifndef DSP_TASK_DELAY
#    if DSP_MODEL == DSP_AXS15231B
#        define DSP_TASK_DELAY pdMS_TO_TICKS(5)
#    else
#        define DSP_TASK_DELAY pdMS_TO_TICKS(30) // cap for 50 fps
#    endif
#endif
#ifndef DISPLAY_QUEUE_LENGTH
#    define DISPLAY_QUEUE_LENGTH 20
#endif

#ifndef RSSI_TEXT_HYSTERESIS_DB
#    define RSSI_TEXT_HYSTERESIS_DB 3
#endif
#ifndef RSSI_TEXT_FORCE_UPDATE_MS
#    define RSSI_TEXT_FORCE_UPDATE_MS 30000UL
#endif

#define DSP_QUEUE_TICKS 0

#ifndef DSQ_SEND_DELAY
#    define DSQ_SEND_DELAY pdMS_TO_TICKS(200)
#endif

QueueHandle_t displayQueue;
static SemaphoreHandle_t displayMutex = nullptr;
static uint16_t displayContentGeneration = 1;
static bool displayContentChanging = false;

// DisplayMutexGuard's declaration now lives in display.h (public, so widget
// .cpp files can hold this same lock across their own draw+push sequences).
// Implementation stays here, since it's the only place displayMutex itself
// is visible.
DisplayMutexGuard::DisplayMutexGuard(uint32_t ticksToWait) {
    _taken = (displayMutex == nullptr) || (xSemaphoreTakeRecursive(displayMutex, (TickType_t)ticksToWait) == pdTRUE);
}
DisplayMutexGuard::~DisplayMutexGuard() {
    if (_taken && displayMutex != nullptr) { xSemaphoreGiveRecursive(displayMutex); }
}

static void purgeQueuedRequestType(displayRequestType_e type) {
    if (displayQueue == NULL) { return; }
    requestParams_t keep[DISPLAY_QUEUE_LENGTH];
    size_t          keepCount = 0;
    requestParams_t item;
    while (xQueueReceive(displayQueue, &item, 0) == pdTRUE) {
        if (item.type != type && keepCount < (sizeof(keep) / sizeof(keep[0]))) { keep[keepCount++] = item; }
    }
    for (size_t i = 0; i < keepCount; i++) { xQueueSend(displayQueue, &keep[i], 0); }
}

void Display::purgeQueuedRequestType(displayRequestType_e type) {
    ::purgeQueuedRequestType(type);
}

static bool isContentRequest(displayRequestType_e type, int payload = 0) {
    switch (type) {
        case NEWTITLE:
        case NEWSTATION:
        case NEWCOVER:
        case DBITRATE:
        case PSTOP:
        case PSTART:
        case PLAYERREBUILD:
            return true;
        case NEWMODE:
            return payload == PLAYER;
        default:
            return false;
    }
}

static bool isBackgroundRequest(displayRequestType_e type) {
    switch (type) {
        case CLOCK:
        case DSPRSSI:
        case AUDIOINFO:
            return true;
        default:
            return false;
    }
}

static bool isSplitPlayerRequest(displayRequestType_e type, int payload) {
    switch (type) {
        case NEWTITLE:
        case NEWSTATION:
        case NEWCOVER:
        case DBITRATE:
            return true;
        case NEWMODE:
            return payload == PLAYER;
        default:
            return false;
    }
}

void Display::beginContentChange() {
    DisplayMutexGuard guard(portMAX_DELAY);
    displayContentGeneration++;
    if (displayContentGeneration == 0) { displayContentGeneration = 1; }
    displayContentChanging = true;
    purgeQueuedRequestType(NEWTITLE);
    purgeQueuedRequestType(NEWSTATION);
    purgeQueuedRequestType(NEWCOVER);
    purgeQueuedRequestType(DBITRATE);
    purgeQueuedRequestType(PLAYERREBUILD);
    purgeQueuedRequestType(PSTART);
    purgeQueuedRequestType(PSTOP);
    purgeQueuedRequestType(CLOCK);
    purgeQueuedRequestType(DSPRSSI);
    purgeQueuedRequestType(AUDIOINFO);
}

bool Display::waitQueueEmpty(uint32_t timeoutMs) {
    if (displayQueue == NULL) { return true; }
    const uint32_t startMs = millis();
    while (uxQueueMessagesWaiting(displayQueue) > 0) {
        if ((millis() - startMs) >= timeoutMs) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

bool Display::waitContentReady(uint32_t timeoutMs) {
    const uint32_t startMs = millis();
    while (displayContentChanging) {
        if ((millis() - startMs) >= timeoutMs) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

static void loopDspTask(void* pvParameters) {
    while (true) {
#ifndef DUMMYDISPLAY
        if (displayQueue == NULL) { break; }
        if (timekeeper.loop0()) {
            {
                DisplayMutexGuard guard(0);
                if (guard.taken()) {
#    if (TS_MODEL != TS_MODEL_UNDEFINED) && (DSP_MODEL != DSP_DUMMY)
                    if ((network.status == CONNECTED || network.status == SDREADY) && !display.isLocked()) touchscreen.loop();
#    endif
#    ifdef NAMEDAYS_FILE
                    display.loopDate(false);
#    endif
                    display.loop();
                }
            }
#    ifndef NETSERVER_LOOP1
            netserver.loop();
#    endif
        }
#else
        timekeeper.loop0();
#    ifndef NETSERVER_LOOP1
        netserver.loop();
#    endif
#endif
        vTaskDelay(DSP_TASK_DELAY);
    }
    vTaskDelete(NULL);
}

void Display::_createDspTask() {
    xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE, NULL, DSP_TASK_PRIORITY, NULL, DSP_TASK_CORE_ID); //"task_prioritas"
}

#ifndef DUMMYDISPLAY
//============================================================================================================================
DspCore dsp;

Page* pages[] = {new Page(), new Page(), new Page(), new Page(), new Page()}; // "presets" +1 page

#    if !((DSP_MODEL == DSP_ST7735 && DTYPE == INITR_BLACKTAB) || DSP_MODEL == DSP_ST7789 || DSP_MODEL == DSP_ST7796 || DSP_MODEL == DSP_ILI9488 || DSP_MODEL == DSP_ILI9486 || \
          DSP_MODEL == DSP_ILI9341 || DSP_MODEL == DSP_ILI9225 || DSP_MODEL == DSP_ST7789_170 || DSP_MODEL == DSP_SSD1322)
#        undef BITRATE_FULL
#        define BITRATE_FULL false
#    endif

void returnPlayer() {
    display.putRequest(NEWMODE, PLAYER);
}

Display::~Display() {
    delete _pager;
    delete _footer;
    delete _plwidget;
    delete _nums;
    delete _clock;
    delete _meta;
    delete _title1;
    delete _title2;
    delete _plcurrent;
}

void Display::init() {
    Serial.print("##[BOOT]#\tdisplay.init\t\n");
    if (displayMutex == nullptr) {
        displayMutex = xSemaphoreCreateRecursiveMutex();
        if (displayMutex == nullptr) { log_e("##[DSP]# display mutex create failed"); }
    }
#    ifdef USE_NEXTION
    nextion.begin();
#    endif
#    if LIGHT_SENSOR != 255
    analogSetAttenuation(ADC_0db);
#    endif
    _bootStep = 0;
    // --- HARDVER INIT ---
    dsp.initDisplay(); // void DspCore::initDisplay() - Ez a függvény hívja meg a display inicializálását, ami a DspCore osztályban van definiálva. Ez a függvény felelős a kijelző beállításáért és
                       // előkészítéséért a használatra.

    if (!loadFonts()) {
        Serial.println("[FONT] ERROR: One or more binding fonts are missing from LittleFS!");
    } else {
        Serial.println("[FONT] Binding fonts successfully loaded.");
    }

    // --- QUEUE ---
    displayQueue = xQueueCreate(DISPLAY_QUEUE_LENGTH, sizeof(requestParams_t)); // Increased from 5 to 20 for better handling of rapid channel switches
    while (displayQueue == NULL) { delay(1); }
    _pager = new Pager();
    _footer = new Page();
    _plwidget = new PlayListWidget();
    _nums = new NumWidget();
    _clock = new ClockWidget();
    _meta = new ScrollWidget();
    _title1 = new ScrollWidget();
    _plcurrent = new ScrollWidget();
    _createDspTask();
    while (_bootStep == 0) { delay(10); }
    Serial.println("done");
}

uint16_t Display::width() {
    return dsp.width();
}

uint16_t Display::height() {
    return dsp.height();
}

void Display::lock() {
    if (displayMutex != nullptr) { xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY); }
    _locked = true;
}

void Display::unlock() {
    _locked = false;
    if (displayMutex != nullptr) { xSemaphoreGiveRecursive(displayMutex); }
}

void Display::_bootScreen() {
    if (!_pager) return;
    _boot = new Page();
    _boot->addWidget(new ProgressWidget(bootWdtConf, bootPrgConf, 0xFFFF, 0));
    _bootstring = (textBoxWidget*)&_boot->addWidget(
        new textBoxWidget(bootstrConf, 50, true, 0xFFFF, 0, config.theme.div)); // Módosítás: textBoxWidget használata a boot szöveghez, hogy a hosszabb szövegek is elférjenek.
    _pager->addPage(_boot);
    _pager->setPage(_boot, true);
    dsp.drawLogo(bootLogoTop);
    _bootStep = 1;
}

void Display::_buildPager() {
    _meta->init("*", metaConf, config.theme.meta, config.theme.metabg);
    _title1->init("*", title1Conf, config.theme.title1, config.theme.background);
    _clock->init(clockConf, 0, 0);
    _plcurrent->init("*", playlistConf, config.theme.plcurrent, config.theme.plcurrentbg); // scrollwidget
    _plwidget->init(_plcurrent);
    _plcurrent->moveTo({TFT_FRAMEWDT, (uint16_t)(_plwidget->currentTop()), (int16_t)playlistConf.width});

#    ifndef HIDE_TITLE2
    _title2 = new ScrollWidget("*", title2Conf, config.theme.title2, config.theme.background);
#    endif

    _plbackground = new FillWidget(playlBGConf, config.theme.plcurrentfill);

#    if DSP_INVERT_TITLE || defined(DSP_OLED)
    _metabackground = new FillWidget(metaBGConf, config.theme.metafill);
#    else
    _metabackground = new FillWidget(metaBGConfInv, config.theme.metafill);
#    endif

    // ── SpectrumWidget ────────────────────────────────────────────────────
    // Pozíció: left=TFT_FRAMEWDT, top=145, height=120px
    // Szélesség: az óra bal széléig (clockConf.left - TFT_FRAMEWDT - 4)
    {
#if DSP_WIDTH == 320
        static const WidgetConfig cc = { 180, 132, 52, WA_RIGHT }; // == clockConf (ILI9341 320x240)
        SpectrumConfig sc;
        sc.left      = TFT_FRAMEWDT;
        sc.top       = 132;
        sc.width     = cc.left - TFT_FRAMEWDT - 4;  // óra bal széléig, 4px margóval
        sc.height    = 76;
        sc.bands     = 8;
        sc.barGap    = 2;
        sc.ledGap    = 2;
        sc.barHeight = 3;
#else
        static const WidgetConfig cc = { 280, 162, 70, WA_RIGHT }; // == clockConf
        SpectrumConfig sc;
        sc.left      = TFT_FRAMEWDT;
        sc.top       = 157;
        sc.width     = cc.left - TFT_FRAMEWDT - 4;  // óra bal széléig, 4px margóval
        sc.height    = 106;
        sc.bands     = 14;
        sc.barGap    = 3;
        sc.ledGap    = 3;
        sc.barHeight = 4;
#endif
        sc.midPct    = config.store.vuMidPctDef;
        sc.highPct   = config.store.vuHighPctDef;
        sc.peakColor = 0xFFFF;  // fehér
        sc.bgColor   = config.theme.background;
        sc.showPeak  = (config.store.vuPeakOn != 0);
        _spectrum = new SpectrumWidget(sc);
        spectrumAnalyzer.begin(sc.bands);
        pages[PG_PLAYER]->addWidget(_spectrum);
    }

#    ifndef HIDE_HEAPBAR
#    endif

#    ifndef HIDE_VOL
    _volwidget = new VolumeWidget(&dsp, volConf);

    // BufferWidget: audio buffer töltöttség, hengerstack ikonnal
    // Ugyanolyan VolumeWidgetConfig mint a vol, csak más pozícióban
    VolumeWidgetConfig buffConf; memcpy_P(&buffConf, &bufferWidgetConf, sizeof(VolumeWidgetConfig));
    _bufferwidget = new BufferWidget(&dsp, buffConf);
#    endif

#    ifndef HIDE_IP
#    if DSP_MODEL == DSP_ILI9341
    // Smaller built-in fallback font (1 instead of the default 2) so the IP box doesn't
    // overflow its box before the VLW fonts have been uploaded to LittleFS.
    _ipbox = new textBoxWidget(ipBoxConf, 30, false, config.theme.ip, config.theme.ip_bg, config.theme.ip_border, 1);
#    elif DSP_MODEL == DSP_ST7789
    // Same fallback-font overflow issue as ILI9341: use the smaller built-in font (1)
    // so IP text fits ipBoxConf's 18px-tall box before VLW fonts are loaded.
    _ipbox = new textBoxWidget(ipBoxConf, 30, false, config.theme.ip, config.theme.ip_bg, config.theme.ip_border, 1);
#    else
    _ipbox = new textBoxWidget(ipBoxConf, 30, false, config.theme.ip, config.theme.ip_bg, config.theme.ip_border);
#    endif
#    endif

#    ifndef HIDE_RSSI
#    if DSP_MODEL != DSP_ST7789
    _wifiwidget = new WifiWidget(&dsp, &wifiConf);
#    endif
#    if DSP_MODEL == DSP_ST7789
    // Same fallback-font overflow fix as ipbox above, for rssiBoxConf's 18px-tall box.
    _rssibox = new textBoxWidget(rssiBoxConf, 16, false, config.theme.rssi, config.theme.rssi_bg, config.theme.rssi_border, 1);
#    else
    _rssibox = new textBoxWidget(rssiBoxConf, 16, false, config.theme.rssi, config.theme.rssi_bg, config.theme.rssi_border);
#    endif
    // Wifi ikon és RSSI szöveg egyszerre látszik, nincs kapcsoló
    // ST7789: wifiConf not defined in conf_320x240_custom.h (icon widget disabled to start),
    // rssibox (dBm text) still shown.
#    endif

    _nums->init(numConf, 10, false, config.theme.digit, config.theme.background);

#    ifndef HIDE_WEATHER
    _weather = new ScrollWidget("\007", weatherConf, config.theme.weather, config.theme.background);
#    endif


    if (_volwidget) { _footer->addWidget(_volwidget); }
    if (_bufferwidget) { _bufferwidget->setActive(true, false); _footer->addWidget(_bufferwidget); }
    if (_ipbox) { _footer->addWidget(_ipbox); }
    if (_wifiwidget) { _footer->addWidget(_wifiwidget); }
    if (_rssibox) { _footer->addWidget(_rssibox); }
    if (_metabackground) { pages[PG_PLAYER]->addWidget(_metabackground); }
    pages[PG_PLAYER]->addWidget(_meta);
    pages[PG_PLAYER]->addWidget(_title1);
    if (_title2) { pages[PG_PLAYER]->addWidget(_title2); }
    if (_weather) { pages[PG_PLAYER]->addWidget(_weather); }

    _bitratewidget = new BitrateWidget(bitrateConf, config.theme.bitrate, config.theme.background);
    _bitratewidget->setActive(true);
    pages[PG_PLAYER]->addWidget(_bitratewidget);

    BitrateBoxConfig pmodeConf_local; memcpy_P(&pmodeConf_local, &pmodeConf, sizeof(BitrateBoxConfig));
    _pmodewidget = new PlayModeWidget();
    _pmodewidget->init(pmodeConf_local, config.theme.pmode, config.theme.background);
    _pmodewidget->setActive(true);
    pages[PG_PLAYER]->addWidget(_pmodewidget);

#    ifdef NAMEDAYS_FILE
    // DateWidget: dátum + névnap összefűzve, scrollozható
    // A ScrollConfig itt van definiálva, mert a conf_480x320.h körkörös include miatt
    // nem érhető el a display.cpp scope-jában fordítási időben.
    // Minden dateWidget paraméter a conf_480x320.h dateConf-ból jön
    WidgetConfig _dc; memcpy_P(&_dc, &dateConf, sizeof(WidgetConfig));
    static ScrollConfig dateWidgetConf = {{ 0, 0, 0, WA_LEFT }, 192, false, MAX_WIDTH, 3000, 4, 40 };
    dateWidgetConf.widget.left     = _dc.left;
    dateWidgetConf.widget.top      = _dc.top;
    dateWidgetConf.widget.textsize = _dc.textsize;
    dateWidgetConf.widget.align    = _dc.align;
    // ── Status widget ────────────────────────────────────────────────────
#if DSP_WIDTH == 320
    WidgetConfig _stc = { 85, 68, 9, WA_LEFT }; // == statusWidgetConf (ILI9341 320x240)
#else
    WidgetConfig _stc = { 127, 105, 16, WA_LEFT }; // == statusWidgetConf
#endif
    _statuswidget = new StatusWidget();
    _statuswidget->init(_stc, config.theme.background);
    _statuswidget->setActive(true, false);
    pages[PG_PLAYER]->addWidget(_statuswidget);

    // ── EQ widget ─────────────────────────────────────────────────────────
#if DSP_WIDTH == 320
    // Status widget group is now 3*34 + 2*4 = 110px wide starting at 85, ending at 195.
    WidgetConfig _eqc = { 203, 68, 9, WA_LEFT };
#else
    WidgetConfig _eqc = { 301, 105, 16, WA_LEFT };
#endif
#if DSP_MODEL != DSP_ST7789
    _eqwidget = new EqWidget();
    _eqwidget->init(_eqc, config.theme.background);
    _eqwidget->setActive(true, false);
    pages[PG_PLAYER]->addWidget(_eqwidget);
#endif
    // ST7789: EQ icon removed from the status row to start with.

    // ── Icon widgetek ──────────────────────────────────────────────────────
#if DSP_WIDTH == 320
    // Right-aligned near the top, like the original layout. Uses WeatherIconWidget::ICO_W
    // directly so this position always tracks the icon's actual current size.
    WidgetConfig _wic = { (uint16_t)(DSP_WIDTH-TFT_FRAMEWDT-WeatherIconWidget::ICO_W), 170, 16, WA_RIGHT }; // == weatherIconConf (ILI9341/ST7789 320x240)
#else
    WidgetConfig _wic = { (uint16_t)(DSP_WIDTH-TFT_FRAMEWDT-81), 205, 18, WA_RIGHT }; // == weatherIconConf
#endif
    _weatherIcon = new WeatherIconWidget();
    _weatherIcon->init(_wic, config.theme.background);
    _weatherIcon->lock(!config.store.showweather);
    _weatherIcon->setActive(true, false);
    pages[PG_PLAYER]->addWidget(_weatherIcon);

#if DSP_WIDTH == 320
    WidgetConfig _sic = { TFT_FRAMEWDT, 2, 0, WA_LEFT }; // == stationIconConf (ILI9341 320x240)
#else
    WidgetConfig _sic = { TFT_FRAMEWDT, 15, 0, WA_LEFT }; // == stationIconConf
#endif
    _stationIcon = new StationIconWidget();
    _stationIcon->init(_sic, config.theme.background);
    _stationIcon->setActive(true, false);
    pages[PG_PLAYER]->addWidget(_stationIcon);

    _datewidget = new DateWidget();
    _datewidget->init(dateWidgetConf, config.theme.date, config.theme.background);
    _datewidget->setActive(true, false);
    pages[PG_PLAYER]->addWidget(_datewidget);

#    endif
    pages[PG_PLAYER]->addWidget(_clock);
#    ifdef USE_CASSETTE_SCREENSAVER
    _cassettewidget = new CassetteWidget();
    _cassettewidget->init({0, 0, 0, WA_LEFT}, config.theme.meta, config.theme.background);
    _cassettewidget->setTextColors(config.theme.title1, config.theme.title2);
    _cassettewidget->lock(true);
    pages[PG_SCREENSAVER]->addWidget(_cassettewidget);
#    endif
#    ifdef USE_COVERART_SCREENSAVER
    _coverartwidget = new CoverArtWidget();
    _coverartwidget->init({0, 0, 0, WA_LEFT}, config.theme.meta, config.theme.background);
    _coverartwidget->setTextColors(config.theme.title1, config.theme.title2);
    _coverartwidget->lock(true);
    pages[PG_SCREENSAVER]->addWidget(_coverartwidget);
#    endif
    pages[PG_SCREENSAVER]->addWidget(_clock);
    pages[PG_PLAYER]->addPage(_footer);
    // _metabackground NEM kerül PG_DIALOG-ra (SDCHANGE overlay-nél nem kell a vonal)
    pages[PG_DIALOG]->addWidget(_meta);
    pages[PG_DIALOG]->addWidget(_nums);
    pages[PG_DIALOG]->addPage(_footer);
    pages[PG_PLAYLIST]->addWidget(_plcurrent); // scrollwidget (legacy)
    pages[PG_PLAYLIST]->addWidget(_plwidget);  // legacy
    pages[PG_PLAYER]->addWidget(_plcurrent);   // inline playlist mode
    pages[PG_PLAYER]->addWidget(_plwidget);    // inline playlist mode
    _plwidget->lock(true);   // alapból inaktív PG_PLAYER-en
    _plcurrent->lock(true);  // alapból inaktív PG_PLAYER-en
    for (const auto& p : pages) { _pager->addPage(p); }
}

void Display::_apScreen() {
    if (_boot) { _pager->removePage(_boot); }
    _boot = new Page();
#    if DSP_INVERT_TITLE || defined(DSP_OLED)
    _boot->addWidget(new FillWidget(metaBGConf, config.theme.metafill));
#    else
    _boot->addWidget(new FillWidget(metaBGConfInv, config.theme.metafill));
#    endif
    ScrollWidget* bootTitle = (ScrollWidget*)&_boot->addWidget(new ScrollWidget("*", apTitleConf, config.theme.meta, config.theme.metabg));
    bootTitle->setText("FusionEdge AP Mode");
    TextWidget* apname = (TextWidget*)&_boot->addWidget(new TextWidget(apNameConf, 30, false, config.theme.title1, config.theme.background));
    apname->setText(LANG::apNameTxt);
    TextWidget* apname2 = (TextWidget*)&_boot->addWidget(new TextWidget(apName2Conf, 30, false, config.theme.clock, config.theme.background));
    apname2->setText(apSsid);
    TextWidget* appass = (TextWidget*)&_boot->addWidget(new TextWidget(apPassConf, 30, false, config.theme.title1, config.theme.background));
    appass->setText(LANG::apPassTxt);
    TextWidget* appass2 = (TextWidget*)&_boot->addWidget(new TextWidget(apPass2Conf, 30, false, config.theme.clock, config.theme.background));
    appass2->setText(apPassword);
    ScrollWidget* bootSett = (ScrollWidget*)&_boot->addWidget(new ScrollWidget("*", apSettConf, config.theme.title2, config.theme.background));
    bootSett->setText(config.ipToStr(WiFi.softAPIP()), LANG::apSettFmt);
    _pager->addPage(_boot);
    _pager->setPage(_boot);
}

void Display::_start() {
    if (_boot) { _pager->removePage(_boot); }
#    ifdef USE_NEXTION
    nextion.wake();
#    endif
    if (network.status != CONNECTED && network.status != SDREADY) {
        _apScreen();
#    ifdef USE_NEXTION
        nextion.apScreen();
#    endif
        _bootStep = 2;
        return;
    }
#    ifdef USE_NEXTION
    nextion.start();
#    endif
    _buildPager();
    _mode = PLAYER;
    config.setTitle(LANG::const_PlReady);

    _pager->setPage(pages[PG_PLAYER]);

    if (_bufferwidget) {
        _bufferwidget->lock(!config.store.audioinfo);
        if (!config.store.audioinfo) {
            _bufferwidget->clear();
        } else {
            uint32_t bm = psramInit() ? 300000 : 1600 * config.store.abuff;
            _bufferwidget->setValue(player.inBufferFilled(), bm);
        }
    }

    if (_weather) { _weather->lock(!config.store.showweather); }
    if (_weather && config.store.showweather) { _weather->setText(LANG::const_getWeather); } // Üres string.
    if (_wifiwidget) { _wifiwidget->setRSSI(WiFi.RSSI()); }
    // Status widget kezdeti állapot
    if (_statuswidget) {
        _statuswidget->setFade(config.store.fadeEnabled);
        _statuswidget->setTts(config.store.clockTtsEnabled, config.store.clockTtsOnlyWhenNoStream);
#if DSP_MODEL != DSP_ST7789
        _statuswidget->setRgb(config.store.lsEnabled,       config.store.lsSsEnabled);
#endif
    }
#    ifndef HIDE_RSSI
    _applyRssiMode();
#    endif
#    ifndef HIDE_IP
    if (_ipbox) { _ipbox->setText(config.ipToStr(WiFi.localIP()), iptxtFmt); }
#    endif
    _volume();
    _station();
    _time(false);
    _bootStep = 2;
    pm.on_display_player();
} // _start vége

void Display::_showDialog(const char* title) {
    dsp.setScrollId(NULL);
    _pager->setPage(pages[PG_DIALOG]);
#    ifdef META_MOVE
    _meta->moveTo(metaMove);
#    endif
    _meta->setAlign(WA_CENTER);
    _meta->setText(title);
} // _showDialog vége

void Display::_refreshThemeColors() {
    if (_meta) { _meta->setColors(config.theme.meta, config.theme.metabg); }
    if (_title1) { _title1->setColors(config.theme.title1, config.theme.background); }
    if (_title2) { _title2->setColors(config.theme.title2, config.theme.background); }
#    ifdef USE_CASSETTE_SCREENSAVER
    if (_cassettewidget) {
        _cassettewidget->setColors(config.theme.meta, config.theme.background);
        _cassettewidget->setTextColors(config.theme.title1, config.theme.title2);
    }
#    endif
#    ifdef USE_COVERART_SCREENSAVER
    if (_coverartwidget) {
        _coverartwidget->setColors(config.theme.meta, config.theme.background);
        _coverartwidget->setTextColors(config.theme.title1, config.theme.title2);
    }
#    endif
    if (_metabackground) { _metabackground->setColors(config.theme.metafill, config.theme.metafill); }
    if (_weather) { _weather->setColors(config.theme.weather, config.theme.background); }
    if (_nums) { _nums->setColors(config.theme.digit, config.theme.background); }
    if (_plcurrent) { _plcurrent->setColors(config.theme.plcurrent, config.theme.plcurrentbg); }
    if (_plbackground) { _plbackground->setColors(config.theme.plcurrentfill, config.theme.plcurrentfill); }
    if (_ipbox) { _ipbox->setColors(config.theme.ip, config.theme.ip_bg, config.theme.ip_border); }
    if (_rssibox) { _rssibox->setColors(config.theme.rssi, config.theme.rssi_bg, config.theme.rssi_border); }
}

void Display::_applyRssiMode() {
#    ifndef HIDE_RSSI
    // Wifi ikon és RSSI szöveg egyszerre látszik, mindkettő aktív
    const int currentRssi = WiFi.RSSI();
    if (_rssibox) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d dBm", currentRssi);
        _lastRssiText = currentRssi;
        _lastRssiTextMs = millis();
        _rssibox->setText(buf);
    }
    if (_wifiwidget) {
        _wifiwidget->setRSSI(currentRssi);
        _wifiwidget->lock(false);
    }
#    endif
}

void Display::_swichMode(displayMode_e newmode) {
#    ifdef USE_NEXTION
    nextion.putRequest({NEWMODE, newmode});
#    endif
    if (newmode == _mode || (network.status != CONNECTED && network.status != SDREADY)) {
        return;
    }
    const displayMode_e prevMode = _mode;
    _mode = newmode;
    dsp.setScrollId(NULL);
    if (newmode == PLAYER) {
#    ifdef USE_CASSETTE_SCREENSAVER
        if (prevMode == SCREENSAVER || prevMode == SCREENBLANK) {
            if (_cassettewidget) { _cassettewidget->lock(true); }
            if (_clock) { _clock->unlock(); }
        }
#    endif
#    ifdef USE_COVERART_SCREENSAVER
        if (prevMode == SCREENSAVER || prevMode == SCREENBLANK) {
            if (_coverartwidget) { _coverartwidget->lock(true); }
            if (_clock) { _clock->unlock(); }
        }
#    endif
        if (prevMode == NUMBERS) {
            purgeQueuedRequestType(NEXTSTATION);
            purgeQueuedRequestType(NEWTITLE); // Clear stale title updates to avoid delayed metadata display
        }
        if (prevMode == VOL) {
            // VOL inline módból visszatérés: unlock widgetek
            _volInlineHide();
        }
        if (prevMode == MODESELECT) {
            // MODESELECT-ből visszatérés: unlock widgetek (ha nem modeSelectorClose() hívta)
            if (_spectrum)    { _spectrum->lock(displayContentChanging || !config.store.vumeter); }
            if (_clock)       { _clock->lock(false); }
            if (_datewidget)  { _datewidget->lock(false); }
            if (_weather)     { _weather->lock(!config.store.showweather); }
            if (_weatherIcon) { _weatherIcon->lock(!config.store.showweather); }
        }
        if (prevMode == SDCHANGE) {
            // PG_DIALOG-ból visszatérés: a _pager->setPage(PG_PLAYER) már megtörtént
            // fentebb (_refreshThemeColors + _pager->setPage hívódik a PLAYER ágban)
            // Nincs külön widget unlock szükség
        }
        if (prevMode == STATIONS) {
            // Playlist inline módból visszatérés
            _plwidget->lock(true);    // playlist elrejtése
            _plcurrent->lock(true);   // scroll elrejtése
            if (_spectrum)    { _spectrum->lock(displayContentChanging || !config.store.vumeter); }
            if (_clock)       { _clock->lock(false); }
            if (_datewidget)  { _datewidget->lock(false); }
            if (_weather)     { _weather->lock(!config.store.showweather); }
            if (_weatherIcon) { _weatherIcon->lock(!config.store.showweather); }
            // DrawArea visszaállítása teljes képernyőre
            _plwidget->setDrawArea(0, dsp.height());
        }
        _clock->setZoom(1.0f);  // Zoom visszaállítása
        _clock->setSsDate("");  // Dátum törlése
        _clock->moveBack();
        _refreshThemeColors();
        numOfNextStation = 0;
        _meta->setAlign(metaConf.widget.align);
        config.isScreensaver = false;
        if (prevMode == VOL) {
            // VOL is an inline overlay on PG_PLAYER, so do not clear/reactivate the
            // whole page when returning. _volInlineHide() repaints the overlay band.
        } else {
            _pager->setPage(pages[PG_PLAYER]);
        }
        _station();
        _title();
        if (_nums) { _nums->setText(""); }
        if (_spectrum) { _spectrum->lock(displayContentChanging || !config.store.vumeter); }
        config.setDspOn(config.store.dspon, false);
        pm.on_display_player();
    }

    if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
        eqForceClose();
        config.isScreensaver = true;
#    ifdef USE_CASSETTE_SCREENSAVER
        // Cassette art scales down to fit small screens (e.g. ILI9341
        // @ 320x240) instead of requiring the full 456x291 native canvas.
        const bool showCassette = config.store.screensaverStyle == 1 && newmode == SCREENSAVER && player.isRunning();
        if (_cassettewidget) { _cassettewidget->lock(!showCassette); }
#    else
        const bool showCassette = false;
#    endif
#    ifdef USE_COVERART_SCREENSAVER
        const bool showCoverArt = config.store.screensaverStyle == 3 && newmode == SCREENSAVER && player.isRunning();
        if (_coverartwidget) { _coverartwidget->lock(!showCoverArt); }
#    else
        const bool showCoverArt = false;
#    endif
        // Decide the clock's lock/zoom state *before* activating the page.
        // _clock lives on PG_SCREENSAVER alongside the cassette/coverart
        // widgets, so if setPage() runs first it can activate and draw the
        // (still-unlocked-from-PLAYER-mode) clock for a frame before we get
        // a chance to lock it -- that stray draw (incl. its digit-erase
        // background rect) is what showed up as a black bar left over where
        // the clock used to be once the cover art painted over the rest.
        if (newmode != SCREENBLANK) {
            if (showCassette || showCoverArt) {
                _clock->lock(true);
            } else {
                _clock->setZoom(2.0f);  // Nagy óra screensaverben
                _clock->unlock();
            }
        }
        _pager->setPage(pages[PG_SCREENSAVER]);
        if (newmode == SCREENBLANK) {
#    if !defined(USE_CASSETTE_SCREENSAVER) && !defined(USE_COVERART_SCREENSAVER)
            _clock->clear();
#    endif
            config.setDspOn(false, false);
        }
    } else {
        config.screensaverTicks = SCREENSAVERSTARTUPDELAY;
        config.screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
        config.isScreensaver = false;
#    if PWR_AMP != 255 // "PWR_AMP"
        digitalWrite(PWR_AMP, HIGH);
#    endif
    }

    if (newmode == VOL) {
        // Inline volume megjelenítés - nincs teljes képernyőtörlés
        _volInlineShow();
    }
    if (newmode == NUMBERS) { _showDialog(""); }
    if (newmode == UPDATING) { _showDialog(LANG::const_DlgUpdate); }
    if (newmode == SLEEPING) {
        _showDialog("SLEEPING");
        delay(2000);
        dsp.clearDsp();
        config.doSleepW();
    }
    if (newmode == SDCHANGE) {
        // PG_DIALOG-ra váltunk: clearDsp + csak _meta + _nums + _footer aktív
        // Így semmilyen PG_PLAYER widget nem rajzolgat a háttérben
        dsp.setScrollId(NULL);
        _pager->setPage(pages[PG_DIALOG]);
        _meta->setAlign(WA_CENTER);
        _meta->setText("");   // canvas-ban jelenítjük meg a szöveget
        if (_nums) { _nums->setText(""); }
        _sdProgressDraw(0);
    }
    if (newmode == INFO || newmode == SETTINGS || newmode == TIMEZONE || newmode == WIFI) { _showDialog(LANG::const_DlgNextion); }
    if (newmode == NUMBERS) { _showDialog(""); }
#    if (DSP_MODEL == DSP_ILI9488) || (DSP_MODEL == DSP_ST7796) || (DSP_MODEL == DSP_AXS15231B)
    if (newmode == PRESETS) {
        _pager->setPage(pages[PG_PRESETS], true);
        presets_drawScreen();
    }
#    endif
    if (newmode == STATIONS) {
        _refreshThemeColors();
        // Drop stale player-mode draw requests so they cannot paint one extra old frame.
        resetQueue();
        // Lock widgetek a 137-286px sávban
        if (_spectrum)    { _spectrum->lock(true); }
        if (_clock)       { _clock->lock(true); }
        if (_datewidget)  { _datewidget->lock(true); }
        if (_weather)     { _weather->lock(true); }
        if (_weatherIcon) { _weatherIcon->lock(true); }
        // Sáv törlése
        dsp.fillRect(0, VOL_AREA_TOP, VOL_AREA_WIDTH, PLAYLIST_AREA_HEIGHT, config.theme.background);
        // Playlist sávra korlátozása
        _plwidget->setDrawArea(VOL_AREA_TOP, PLAYLIST_AREA_HEIGHT);
        // Prevent ScrollWidget pre-draw during page activation to avoid an empty-row flash.
        _plcurrent->lock(true);
        // Ensure no stale current-row text is visible before the fresh playlist draw.
        _plcurrent->setText("");
        // Maradunk PG_PLAYER-en - nem váltunk page-et, nincs clearDsp
        _plwidget->lock(false);   // playlist widget aktiválása
        currentPlItem = config.lastStation();
        // Átadjuk a scrollwidgetet, ha eddig nem tettük meg
        _plwidget->init(_plcurrent);
        _plcurrent->unlock();
        _drawPlaylist();
    }
    if (newmode == MODESELECT) {
        _refreshThemeColors();
        resetQueue();
        // Widget lockolás — STATIONS mintájára
        if (_spectrum)    { _spectrum->lock(true); }
        if (_clock)       { _clock->lock(true); }
        if (_datewidget)  { _datewidget->lock(true); }
        if (_weather)     { _weather->lock(true); }
        if (_weatherIcon) { _weatherIcon->lock(true); }
        // Sáv törlése
        dsp.fillRect(0, VOL_AREA_TOP, VOL_AREA_WIDTH, VOL_AREA_HEIGHT, config.theme.background);
        _msBuildList();
        _msDraw();
    }
} // _swichMode vége

void Display::resetQueue() {
    if (displayQueue != NULL) { xQueueReset(displayQueue); }
}

bool Display::tapPlaylistItem(uint16_t y) {
    if (!_plwidget) return false;
    const uint16_t itemH  = _plwidget->itemHeight();   // 30px
    const uint16_t midY   = _plwidget->currentTop();   // a kiválasztott (középső) sor Y koordinátája
    const int      total  = (int)config.playlistLength();
    if (itemH == 0 || total == 0) return false;

    // Melyik sort érintette? A középső = currentPlItem, felfelé/lefelé offset
    int rowOffset  = ((int)y - (int)midY) / (int)itemH;
    int tappedItem = (int)currentPlItem + rowOffset;   // 1-alapú index
    if (tappedItem < 1)     tappedItem = 1;
    if (tappedItem > total) tappedItem = total;

    currentPlItem = (uint16_t)tappedItem;
    putRequest(CLOSEPLAYLIST, currentPlItem);
    return true;
}


void Display::_drawPlaylist() {
    _plwidget->drawPlaylist(currentPlItem);
    timekeeper.waitAndReturnPlayer(config.store.stationsListReturnTime);
}

void Display::_drawNextStationNum(uint16_t num) {
    timekeeper.waitAndReturnPlayer(config.store.stationsListReturnTime); // Visszatérési idő a főképernyőre.
    _meta->setText(config.stationByNum(num));
    _nums->setText(num, "%d");
}

void Display::putRequest(displayRequestType_e type, int payload) {
    if (displayQueue == NULL) { return; }
    if (displayContentChanging && isSplitPlayerRequest(type, payload)) {
        return;
    }
    if (displayContentChanging && isBackgroundRequest(type)) {
        return;
    }
    if (type == PSTART) { purgeQueuedRequestType(PSTOP); }
    if (type == PSTOP) { purgeQueuedRequestType(PSTART); }
    switch (type) {
        case DRAWPLAYLIST:        case NEWTITLE:
        case NEWSTATION:
        case NEWCOVER:
        case DBITRATE:
        case PLAYERREBUILD:
        case PSTART:
        case PSTOP:
        case AUDIOINFO:
        case DSPRSSI:
        case DRAWVOL:
        case DRAWMODESELECT:
            purgeQueuedRequestType(type);
            break;
        default:
            break;
    }
    requestParams_t request;
    request.type = type;
    request.payload = payload;
    request.generation = displayContentGeneration;
    xQueueSend(displayQueue, &request, DSQ_SEND_DELAY);
#    ifdef USE_NEXTION
    nextion.putRequest(request);
#    endif
}

void Display::_layoutChange(bool played) {
    if (!_spectrum) { return; }
    spectrumAnalyzer.resetSmooth();
    if (_mode != PLAYER) { return; }
    if (config.store.vumeter) {
        if (played) {
            _spectrum->lock(displayContentChanging);
        } else {
            _spectrum->reset();
        }
    } else {
        // Diagnostic/off mode: do not touch the spectrum drawing area on
        // playback start. reset() clears the full spectrum rectangle and would
        // make a "spectrum disabled" A/B test dirty.
        _spectrum->lock(true);
    }
}

void Display::applyVuModeChange() {
    spectrumAnalyzer.resetSmooth();
    if (_mode != PLAYER) { return; }
    if (_spectrum) { _spectrum->reset(); }
    _layoutChange(player.isRunning());
}
void Display::invalidateThemeWidgets() {
    if (!_wifiwidget || _locked || config.isScreensaver) { return; }
    _wifiwidget->invalidate();
    if (_statuswidget) {
        _statuswidget->setFade(config.store.fadeEnabled);
        _statuswidget->setTts(config.store.clockTtsEnabled, config.store.clockTtsOnlyWhenNoStream);
#if DSP_MODEL != DSP_ST7789
        _statuswidget->setRgb(config.store.lsEnabled,       config.store.lsSsEnabled);
#endif
    }
    if (_pmodewidget) { _pmodewidget->invalidate(); }
    if (_datewidget)  { _datewidget->setColors(config.theme.date, config.theme.background); }
    if (_eqwidget) { _eqwidget->setActive(true, false); }
}

void Display::loop() {
    DisplayMutexGuard guard(0);
    if (!guard.taken()) { return; }
    if (_bootStep == 0) {
        _pager->begin();
        _bootScreen();
        return;
    }
    // MODESELECT auto-bezárás timer
    modeSelectorTick();
#if DSP_MODEL != DSP_AXS15231B
    if (_mode == STATIONS) {
        _plcurrent->loop(); // Ez hajtja az X irányú görgetést a lejátszási listában, ahol a hosszabb szövegek vannak.
    }
#endif
    if (displayQueue == NULL || _locked) { return; }
#    ifdef USE_NEXTION
    nextion.loop();
#    endif
    requestParams_t request;
    bool handledRequest = false;
    if (xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)) {
        handledRequest = true;
        // A free-running widget frame may still be flushing through LGFX DMA.
        // Starting a title/station/icon redraw before that transfer is fully
        // settled can corrupt the display transaction state on rare timing.
#if DSP_MODEL != DSP_AXS15231B
        dsp.waitDMA();
#endif
        bool processRequest = true;
        if (isContentRequest(request.type, request.payload) && request.generation != displayContentGeneration) {
            processRequest = false;
        }
        bool pm_result = true;
        if (processRequest) { pm.on_display_queue(request, pm_result); }
        if (processRequest && pm_result) {
            switch (request.type) {
                case NEWMODE: lock(); _swichMode((displayMode_e)request.payload); unlock(); break;
                case CLOSEPLAYLIST: {
                    dsp.setTextDatum(top_left);
                    player.sendCommand({PR_PLAY, request.payload});
                } break;

                case CLOCK:
                    if (_mode == PLAYER || _mode == SCREENSAVER) { _time(request.payload == 1); }
                    /*#ifdef USE_NEXTION
  if(_mode==TIMEZONE) nextion.localTime(network.timeinfo);
  if(_mode==INFO)     nextion.rssi();
#endif*/
                    break;

                case NEWTITLE:
                    if (_mode == PLAYER) {
                        _title();
                        _updateStationIcon();
                    }
                    break;
                case NEWCOVER:
                    if (_mode == PLAYER) { _updateStationIcon(); }
                    break;
                case PLAYERREBUILD:
                    if (request.payload > 0 && request.payload != config.lastStation()) {
                        break;
                    }
                    if (_mode != PLAYER) {
                        lock();
                        _swichMode(PLAYER);
                        unlock();
                    }
                    if (_mode == PLAYER) {
                        _title();
                        if (request.payload > 0 && request.payload != config.lastStation()) {
                            break;
                        }
                        if (_bitrate) {
                            char buf[20];
                            snprintf(buf, 20, bitrateFmt, config.station.bitrate);
                            _bitrate->setText(config.station.bitrate == 0 ? "" : buf);
                        }
                        if (_bitratewidget) {
                            _bitratewidget->setState(config.configFmt, config.station.bitrate);
                        }
                        if (_pmodewidget) {
                            uint8_t pm = (config.getMode() == PM_SDCARD) ? DPS_SDCARD
                                       : (config.getMode() == PM_BLUETOOTH) ? DPS_BLUETOOTH
                                       : (config.store.playlistSource == PL_SRC_DLNA) ? DPS_DLNA : DPS_WEB;
                            _pmodewidget->setState(pm, config.getMode() == PM_BLUETOOTH ? 0 : config.lastStation());
                        }
                        if (request.payload > 0 && request.payload != config.lastStation()) {
                            break;
                        }
                        _station();
                        if (request.payload > 0 && request.payload != config.lastStation()) {
                            break;
                        }
                        _updateStationIcon();
                    }
                    break;
                case NEWSTATION:
                    if (_mode == PLAYER) {
                        _station();
                        _updateStationIcon();
                    }
                    break;
                case NEXTSTATION:
                    if (_mode == NUMBERS) { _drawNextStationNum(request.payload); }
                    break;
                case DRAWPLAYLIST: _drawPlaylist(); break;
                case DRAWVOL: _volume(); break;
                case DRAWMODESELECT:
                    if (_mode == MODESELECT) { _msDraw(); }
                    break;

                case DBITRATE: {
                    if (_mode == PLAYER) { // csak a lejátszás képernyőn frissíti a bitrateWidgetet
                        char buf[20];
                        snprintf(buf, 20, bitrateFmt, config.station.bitrate);

                        if (_bitrate) { _bitrate->setText(config.station.bitrate == 0 ? "" : buf); }

                        if (_bitratewidget) {
                            _bitratewidget->setState(config.configFmt, config.station.bitrate);
                        }
                        if (_pmodewidget) {
                            uint8_t pm = (config.getMode() == PM_SDCARD) ? DPS_SDCARD
                                       : (config.getMode() == PM_BLUETOOTH) ? DPS_BLUETOOTH
                                       : (config.store.playlistSource == PL_SRC_DLNA) ? DPS_DLNA : DPS_WEB;
                            _pmodewidget->setState(pm, config.getMode() == PM_BLUETOOTH ? 0 : config.lastStation());
                        }
                        // Beállítja a csatorna számát a widgeten
                    }
                } break;

                case AUDIOINFO:
                    if (_bufferwidget) {
                        _bufferwidget->lock(!config.store.audioinfo);
                        if (!config.store.audioinfo) {
                            _bufferwidget->clear();
                        } else {
                            uint32_t bm = psramInit() ? 300000 : 1600 * config.store.abuff;
                            _bufferwidget->setValue(player.isRunning() ? player.inBufferFilled() : 0, bm);
                        }
                    }
                    break;

                case SHOWVUMETER:
                    if (_mode != PLAYER) {
                        spectrumAnalyzer.resetSmooth();
                        break;
                    }
                    if (_spectrum) {
                        if (config.store.vumeter) {
                            _spectrum->lock(false);
                            _layoutChange(player.isRunning());
                        } else {
                            _spectrum->lock(true);
                            spectrumAnalyzer.resetSmooth();
                            _spectrum->reset();
                        }
                    }
                    break;

                case SWITCHVUMODE:applyVuModeChange(); break;

                case SHOWWEATHER:
                    if (_weatherIcon) { _weatherIcon->lock(!config.store.showweather); if (!config.store.showweather) _weatherIcon->clearArea(); }
                    if (_weather) { _weather->lock(!config.store.showweather); }
                    if (!config.store.showweather) {
#    ifndef HIDE_IP
                        if (_ipbox) { _ipbox->setText(config.ipToStr(WiFi.localIP()), iptxtFmt); }
#    endif
                    } else {
                        if (_weather) { _weather->setText(LANG::const_getWeather); } // Üres string
                    }
                    break;

                case NEWWEATHER:
                    if (_weatherIcon && timekeeper.weatherIcon[0] != '\0') {
                        _weatherIcon->setWeather(timekeeper.weatherIcon, timekeeper.tempC);
                    }
                    if (_weather && timekeeper.weatherBuf) {
                        _weather->resetText(); // short/long váltáskor az oldtext cache-t invalidáljuk
                        _weather->setText(timekeeper.weatherBuf);
                        // A weather refresh must not keep an old scroll owner.
                        // The normal page order will then choose station first, then metadata,
                        // and weather only if the higher-priority rows do not need scrolling.
                        ScrollWidget::releaseScrollOwner();
                    }
                    break;

                case SHOWRSSIMODE:
#    ifndef HIDE_RSSI
                    _applyRssiMode();
#    endif
                    break;

                case INVALIDATETHEMEWIDGETS: invalidateThemeWidgets(); break;

                case BOOTSTRING:
                    if (_bootstring) { _bootstring->setText(config.ssids[request.payload].ssid, LANG::bootstrFmt); }
                    /*#ifdef USE_NEXTION
 char buf[50];
 snprintf(buf, 50, bootstrFmt, config.ssids[request.payload].ssid);
 nextion.bootString(buf);
#endif*/
                    break;

                case WAITFORSD:
                    if (_bootstring) { _bootstring->setText(LANG::const_waitForSD); }
                    break;

                case SDFILEINDEX:
                    if (_mode == SDCHANGE) { _sdProgressDraw(request.payload); }
                    break;

                case DSPRSSI:
                    if (_mode == PLAYER) {
                        _setRSSI(request.payload);
                        if (_bufferwidget && config.store.audioinfo) {
                            uint32_t bm = psramInit() ? 300000 : 1600 * config.store.abuff;
                            _bufferwidget->setValue(player.isRunning() ? player.inBufferFilled() : 0, bm);
                        }
                    }
                    break;

                case PSTART:
                    if (request.payload > 0 && request.payload != config.lastStation()) {
                        break;
                    }
                    displayContentChanging = false;
                    _layoutChange(true);
                    break;
                case PSTOP:
                    _layoutChange(false);
                    displayContentChanging = false;
                    break;
                case DSP_START: _start(); break;

                case NEWIP:
#    ifndef HIDE_IP
                    if (_ipbox) { _ipbox->setText(config.ipToStr(WiFi.localIP()), iptxtFmt); }
#    endif
                    break;

                default:
                    break;
            }
        }
    }

    // After an explicit display request, let the bus/DMA settle and process
    // free-running widget frames on the next display tick. This avoids mixing
    // a mode/title/station redraw with a spectrum/scroll/clock frame.
    if (handledRequest) {
#if DSP_MODEL != DSP_AXS15231B
        dsp.waitDMA();
#endif
        return;
    }

#if DSP_MODEL != DSP_AXS15231B
    // VOL overlay aktív → ne futtassuk a widget loop-okat (spektrum belerajzolna a VOL területbe)
    dsp.waitDMA();
    if (_mode != VOL) { _pager->loop(); }
    dsp.waitDMA();
#endif

#if DSP_MODEL == DSP_AXS15231B
    _pager->loop();
#endif
    dsp.loop();
} // loop vége

void Display::_setRSSI(int rssi) {
    // Wifi ikon és RSSI szöveg egyszerre frissül
    if (_rssibox) {
        const uint32_t now = millis();
        const bool     firstUpdate = (_lastRssiText == 9999);
        const bool     changedEnough = abs(rssi - _lastRssiText) >= RSSI_TEXT_HYSTERESIS_DB;
        const bool     forceUpdate = !firstUpdate && RSSI_TEXT_FORCE_UPDATE_MS > 0 && (now - _lastRssiTextMs >= RSSI_TEXT_FORCE_UPDATE_MS);

        if (firstUpdate || changedEnough || forceUpdate) {
            _lastRssiText = rssi;
            _lastRssiTextMs = now;
            _rssibox->setText(rssi, "%d dBm");
        }
    }
    if (_wifiwidget) {
        _wifiwidget->setRSSI(rssi);
        return;
    }
    if (!_rssi) { return; }
#    if RSSI_DIGIT
    _rssi->setText(rssi, rssiFmt);
    return;
#    endif
    char rssiG[3];
    int  rssi_steps[] = {RSSI_STEPS};
    if (rssi >= rssi_steps[0]) { strlcpy(rssiG, "\004\006", 3); }
    if (rssi >= rssi_steps[1] && rssi < rssi_steps[0]) { strlcpy(rssiG, "\004\005", 3); }
    if (rssi >= rssi_steps[2] && rssi < rssi_steps[1]) { strlcpy(rssiG, "\004\002", 3); }
    if (rssi >= rssi_steps[3] && rssi < rssi_steps[2]) { strlcpy(rssiG, "\003\002", 3); }
    if (rssi < rssi_steps[3] || rssi >= 0) { strlcpy(rssiG, "\001\002", 3); }
    _rssi->setText(rssiG);
}

void Display::_station() {
    _meta->setAlign(metaConf.widget.align);
    char nameSnapshot[BUFLEN];
    strlcpy(nameSnapshot, config.station.name, sizeof(nameSnapshot));
    if (nameSnapshot[0] == '.') {
        _meta->setText(nameSnapshot + 1);
    } else {
        _meta->setText(nameSnapshot);
    }
    // When station text changes, let the next display loop pick the
    // highest-priority long text again: station -> artist/title -> weather.
    ScrollWidget::releaseScrollOwner();

    /*#ifdef USE_NEXTION
    nextion.newNameset(config.station.name);
    nextion.bitrate(config.station.bitrate);
    nextion.bitratePic(ICON_NA);
  #endif*/
}

void Display::_updateStationIcon() {
    if (!_stationIcon) return;

#ifdef USE_LASTFM_COVER
    uint8_t* coverData = nullptr;
    size_t coverSize = 0;
    bool coverIsJpeg = false;
    uint32_t coverGeneration = 0;
    if (coverArt.copyReadyFor(config.station.title,
                              config.getMode() == PM_BLUETOOTH,
                              coverData, coverSize,
                              coverIsJpeg, coverGeneration)) {
        _stationIcon->setCover(coverData, coverSize, coverIsJpeg, coverGeneration);
        return;
    }
#endif

    const uint8_t pm = (config.getMode() == PM_SDCARD) ? DPS_SDCARD
                     : (config.getMode() == PM_BLUETOOTH) ? DPS_BLUETOOTH
                     : (config.store.playlistSource == PL_SRC_DLNA) ? DPS_DLNA : DPS_WEB;
    const char* iconLookupName = (config.getMode() == PM_BLUETOOTH)
        ? ""
        : (config.station.iconName[0] != '\0') ? config.station.iconName : config.station.name;
    _stationIcon->setStation(iconLookupName, pm);
}

char* split(char* str, const char* delim) {
    char* dmp = strstr(str, delim);
    if (dmp == NULL) { return NULL; }
    *dmp = '\0';
    return dmp + strlen(delim);
}

void Display::_title() {
    // Ha üres a title, használja a playlistben tárolt nevet.
    char titleSnapshot[BUFLEN];
    char nameSnapshot[BUFLEN];
    strlcpy(titleSnapshot, config.station.title, sizeof(titleSnapshot));
    strlcpy(nameSnapshot, config.station.name, sizeof(nameSnapshot));

    const char* titleText = titleSnapshot[0] ? titleSnapshot : nameSnapshot;
    if (titleText[0] != '\0') {
        char tmpbuf[BUFLEN];
        strlcpy(tmpbuf, titleText, sizeof(tmpbuf));
        char* stitle = split(tmpbuf, " - ");
        if (stitle && _title2) {
            _title1->setText(tmpbuf);
            _title2->setText(stitle);
        } else {
            _title1->setText(titleText);
            if (_title2) { _title2->setText(""); }
        }
    } else {
        _title1->setText("");
        if (_title2) { _title2->setText(""); }
    }
    // New metadata should be able to pre-empt weather scrolling.
    ScrollWidget::releaseScrollOwner();
    if (player_on_track_change) { player_on_track_change(); }
    pm.on_track_change();
}

void Display::_time(bool redraw) {
    tm displayTime{};
    network_get_timeinfo_snapshot(&displayTime);

#    if LIGHT_SENSOR != 255
    if (config.store.dspon) {
        config.store.brightness = AUTOBACKLIGHT(analogRead(LIGHT_SENSOR));
        config.setBrightness();
    }
#    endif
    if (_clock && _clock->locked()) return;   // EQ / VOL overlay aktív — ne rajzoljon felé
    if (config.isScreensaver) {
        _ssUpdateDate();
        _clock->draw(redraw);
    } else {
        _clock->draw(redraw);
    }
}

void Display::_volume() {
#    ifndef HIDE_VOL
    if (_volwidget) { _volwidget->setVolume(config.store.volume); }
#    endif
    if (_mode == VOL) {
        timekeeper.waitAndReturnPlayer(2);
        _volInlineDraw();  // Frissítjük az inline volume overlay-t
    }
    /*#ifdef USE_NEXTION
      nextion.setVol(config.store.volume, _mode == VOL);
    #endif*/
}

// ---- Screensaver dátum kirajzolása a zoom-olt óra alá ----
void Display::_ssUpdateDate() {
    // Dátum szöveg frissítése a ClockWidget-ben (az óra canvas-ba rajzolja bele)
    time_t now = time(nullptr);
    tm displayTime{};
    localtime_r(&now, &displayTime);

    char datebuf[64];
    switch (config.store.dateFormat) {
        case 0:  snprintf(datebuf, sizeof(datebuf), "%d. %s %2d. %s",     displayTime.tm_year+1900, LANG::mnths[displayTime.tm_mon], displayTime.tm_mday, LANG::dowf[displayTime.tm_wday]); break;
        case 1:  snprintf(datebuf, sizeof(datebuf), "%2d %s %d",           displayTime.tm_mday, LANG::mnths[displayTime.tm_mon], displayTime.tm_year+1900); break;
        case 2:  snprintf(datebuf, sizeof(datebuf), "%s %2d %s %d",        LANG::dowf[displayTime.tm_wday], displayTime.tm_mday, LANG::mnths[displayTime.tm_mon], displayTime.tm_year+1900); break;
        case 3:  snprintf(datebuf, sizeof(datebuf), "%s - %02d. %s. %04d", LANG::dowf[displayTime.tm_wday], displayTime.tm_mday, LANG::mnths[displayTime.tm_mon], displayTime.tm_year+1900); break;
        default: snprintf(datebuf, sizeof(datebuf), "%s - %02d. %s. %d",   LANG::dowf[displayTime.tm_wday], displayTime.tm_mday, LANG::mnths[displayTime.tm_mon], displayTime.tm_year+1900); break;
    }
    _clock->setSsDate(datebuf);
}

// ---- Inline volume overlay (137px → 286px sáv) ----

void Display::_volInlineShow() {
    // Ha EQ overlay nyitva van, zárjuk be
    eqForceClose();
    // Lock: spektrum, óra, dátum, időjárás widgetek
    if (_spectrum)    { _spectrum->lock(true); }
    if (_clock)       { _clock->lock(true); }
    if (_datewidget)  { _datewidget->lock(true); }
    if (_weather)     { _weather->lock(true); }
    if (_weatherIcon) { _weatherIcon->lock(true); }
    // Sáv törlése
    dsp.fillRect(VOL_AREA_LEFT, VOL_AREA_TOP, VOL_AREA_WIDTH, VOL_AREA_HEIGHT, config.theme.background);
    // Kirajzolás
    _volInlineDraw();
}

void Display::_volInlineHide() {
    // Clear the overlay first, then unlock widgets so their redraw is not erased.
    dsp.fillRect(VOL_AREA_LEFT, VOL_AREA_TOP, VOL_AREA_WIDTH, VOL_AREA_HEIGHT, config.theme.background);

    // Unlock widgetek - a _pager->setPage(PG_PLAYER) után újrarajzolódnak
    if (_spectrum)    { _spectrum->lock(!config.store.vumeter); }
    if (_clock)       { _clock->lock(false); }
    if (_datewidget)  { _datewidget->lock(false); }
    if (_weather)     { _weather->lock(!config.store.showweather); }
    if (_weatherIcon) { _weatherIcon->lock(!config.store.showweather); }

    // The VOL_AREA band (y=137..286) is sized for larger panels. On ILI9341
    // (240px tall) it runs down into the footer row - wifi icon/IP/volume/
    // RSSI/buffer widget all sit around y=210-234 - so the fillRect above
    // wipes them out too, but none of those widgets were part of the
    // lock/unlock set above. Force them to redraw now instead of leaving
    // them blank until something unrelated (like a station change) happens
    // to touch them again.
    if (_wifiwidget)   { _wifiwidget->lock(false); }
    if (_ipbox)        { _ipbox->lock(false); }
    if (_volwidget)    { _volwidget->lock(false); }
    if (_rssibox)      { _rssibox->lock(false); }
    if (_bufferwidget) { _bufferwidget->lock(!config.store.audioinfo); }
}

void Display::_volInlineDraw() {
    // Canvas sprite a villogásmentes rajzoláshoz
    LGFX_Sprite canvas(&dsp);
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    canvas.createSprite(VOL_AREA_WIDTH, VOL_AREA_HEIGHT);
    canvas.fillSprite(config.theme.background);

    uint8_t vol = config.store.volume;

    // --- "VOLUME" felirat - vlw20 font ---
    canvas.setTextDatum(MC_DATUM);
    canvas.setTextColor(config.theme.meta, config.theme.background);
    if (font_vlw_20) {
        canvas.loadFont(font_vlw_20);
        canvas.setTextSize(1);
        canvas.drawString("VOLUME", VOL_AREA_WIDTH / 2, 14);
        canvas.unloadFont();
    } else {
        canvas.setTextSize(2);
        canvas.drawString("VOLUME", VOL_AREA_WIDTH / 2, 14);
    }

    // --- Nagy szám - clock font ---
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", vol);
    canvas.setTextColor(config.theme.digit, config.theme.background);
    if (font_vlw_clock) {
        canvas.loadFont(font_vlw_clock);
        canvas.setTextSize(1);
        canvas.setTextDatum(MC_DATUM);
        canvas.drawString(buf, VOL_AREA_WIDTH / 2, 75);
        canvas.unloadFont();
    } else {
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextSize(7);
        canvas.drawString(buf, VOL_AREA_WIDTH / 2, 75);
    }

    // --- Hangszóró ikonok (bitmap maszk alapján) ---
    const uint16_t SPK_Y     = 30;  // canvas-on belül
    const uint16_t SPK_COLOR = config.theme.digit;
    const uint16_t SPK_LEFT  = 15;
    const uint16_t SPK_RIGHT = VOL_AREA_WIDTH - SPEAKER_W - 15;

    for (uint16_t y = 0; y < SPEAKER_H; y++) {
        for (uint16_t x = 0; x < SPEAKER_W; x++) {
            uint16_t idx = y * SPEAKER_W + x;
            if (pgm_read_byte(&speakerLeftMask[idx >> 3]) & (0x80 >> (idx & 7)))
                canvas.drawPixel(SPK_LEFT + x, SPK_Y + y, SPK_COLOR);
            if (pgm_read_byte(&speakerRightMask[idx >> 3]) & (0x80 >> (idx & 7)))
                canvas.drawPixel(SPK_RIGHT + x, SPK_Y + y, SPK_COLOR);
        }
    }

    // --- Szegmens bar a sáv aljára ---
    const uint8_t  SEG_COUNT = 21;
    const uint16_t SEG_GAP  = 5;
    const uint16_t SEG_W    = 14;
    const uint16_t BAR_LEFT = (VOL_AREA_WIDTH - (SEG_COUNT * (SEG_W + SEG_GAP) - SEG_GAP)) / 2;
    const uint16_t BAR_TOP  = VOL_AREA_HEIGHT - 28;  // canvas-on belül
    const uint16_t SEG_H    = 14;

    for (uint8_t i = 0; i < SEG_COUNT; i++) {
        uint16_t x = BAR_LEFT + i * (SEG_W + SEG_GAP);
        uint16_t col;
        if (i < vol) {
            float ratio = (float)i / SEG_COUNT;
            if (ratio < 0.6f)       col = config.theme.vol_low;
            else if (ratio < 0.85f) col = config.theme.vol_mid;
            else                    col = config.theme.vol_high;
        } else {
            col = config.theme.vol_inactive;
        }
        canvas.fillRect(x, BAR_TOP, SEG_W, SEG_H, col);
    }

    // Egy lépésben kirakjuk a sávba
    canvas.pushSprite(VOL_AREA_LEFT, VOL_AREA_TOP);
    canvas.deleteSprite();
}

// ── SD indexelés progress overlay (VOL_AREA sávban, PG_PLAYER-en maradva) ──
void Display::_sdProgressDraw(int count) {
    LGFX_Sprite canvas(&dsp);
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    canvas.createSprite(VOL_AREA_WIDTH, VOL_AREA_HEIGHT);
    canvas.fillSprite(config.theme.background);

    canvas.setTextDatum(MC_DATUM);

    // Fejléc: "INDEX SD"
    canvas.setTextColor(config.theme.meta, config.theme.background);
    if (font_vlw_20) {
        canvas.loadFont(font_vlw_20);
        canvas.setTextSize(1);
        canvas.drawString(LANG::const_waitForSD, VOL_AREA_WIDTH / 2, 20);
        canvas.unloadFont();
    } else {
        canvas.setTextSize(2);
        canvas.drawString(LANG::const_waitForSD, VOL_AREA_WIDTH / 2, 20);
    }

    // Nagy szám: indexelt fájlok száma
    if (count > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", count);
        canvas.setTextColor(config.theme.digit, config.theme.background);
        if (font_vlw_clock) {
            canvas.loadFont(font_vlw_clock);
            canvas.setTextSize(1);
            canvas.drawString(buf, VOL_AREA_WIDTH / 2, 90);
            canvas.unloadFont();
        } else {
            canvas.setTextSize(7);
            canvas.drawString(buf, VOL_AREA_WIDTH / 2, 90);
        }
    }

    canvas.pushSprite(VOL_AREA_LEFT, VOL_AREA_TOP);
    canvas.deleteSprite();
}
// ══════════════════════════════════════════════════════════════════════════════

// EQ overlay konstansok
#define EQ_OVERLAY_TOP    VOL_AREA_TOP
#define EQ_OVERLAY_H      VOL_AREA_HEIGHT
#define EQ_OVERLAY_LEFT   VOL_AREA_LEFT
#define EQ_OVERLAY_W      VOL_AREA_WIDTH

// EQ preset nevek és értékek: { bass, middle, trebble }
static const char* const eqPresetNames[]    = { "Flat","Pop","Blues","Rock","Metal","Jazz","Country","Alternative" };
static const int8_t      eqPresetValues[][3] = {
    {  0,  0,  0 },   // Flat
    { -1,  5,  5 },   // Pop
    {  4,  6,  0 },   // Blues
    {  3,  0,  5 },   // Rock
    {  6, -3,  5 },   // Metal
    { -2,  0,  3 },   // Jazz
    {  1,  2,  4 },   // Country
    {  4, -2,  4 },   // Alternative
};
static constexpr uint8_t EQ_PRESET_COUNT = 8;

// FusionEdge: Auto EQ – genre-name alapú EQ preset keresés (case-insensitive).
// A playlist CSV "genre" mezőjét veti össze az eqPresetNames[] tömb elemeivel.
// Pl. genre="metal" -> "Metal" preset egyezik.
bool findEqPresetByGenre(const char* genre, int8_t& bass, int8_t& mid, int8_t& treb) {
    if (!genre || genre[0] == '\0') { return false; }
    for (uint8_t i = 0; i < EQ_PRESET_COUNT; i++) {
        if (strcasecmp(eqPresetNames[i], genre) == 0) {
            bass = eqPresetValues[i][0];
            mid  = eqPresetValues[i][1];
            treb = eqPresetValues[i][2];
            return true;
        }
    }
    return false;
}


// Belső állapot: preset nézet aktív-e
static bool _eqPresetView = false;

// Slider érintési állapot
static int8_t __attribute__((unused)) _eqTouchSlider = -1;  // 0=bass,1=mid,2=treb,3=bal; -1=nincs

bool Display::eqToggle() {
    if (!_eqwidget) return false;
    bool nowActive = _eqwidget->toggle();
    if (nowActive) {
        _eqInlineShow();
    } else {
        _eqPresetView = false;
        _eqInlineHide();
    }
    return nowActive;
}

bool Display::isEqOpen() const {
    return _eqwidget && _eqwidget->isEqActive();
}

void Display::eqForceClose() {
    if (!_eqwidget || !_eqwidget->isEqActive()) return;
    _eqPresetView = false;
    _eqwidget->forceClose();
    _eqInlineHide();
}

void Display::_eqInlineShow() {
    // Lock: spektrum, óra, dátum, időjárás — ugyanaz mint VOL
    if (_spectrum)    { _spectrum->lock(true); }
    if (_clock)       { _clock->lock(true); }
    if (_datewidget)  { _datewidget->lock(true); }
    if (_weather)     { _weather->lock(true); }
    if (_weatherIcon) { _weatherIcon->lock(true); }
    // Sáv törlése
    dsp.fillRect(EQ_OVERLAY_LEFT, EQ_OVERLAY_TOP, EQ_OVERLAY_W, EQ_OVERLAY_H, config.theme.background);
    _eqInlineDraw();
}

void Display::_eqInlineHide() {
    // Clear first; unlocking an active widget redraws its cached content.
    dsp.fillRect(EQ_OVERLAY_LEFT, EQ_OVERLAY_TOP, EQ_OVERLAY_W, EQ_OVERLAY_H, config.theme.background);

    // Unlock
    if (_spectrum)    { _spectrum->lock(!config.store.vumeter); }
    if (_clock)       { _clock->lock(false); }
    if (_datewidget)  { _datewidget->lock(false); }
    if (_weather)     { _weather->lock(!config.store.showweather); }
    if (_weatherIcon) { _weatherIcon->lock(!config.store.showweather); }
}

void Display::_eqInlineDraw() {
    LGFX_Sprite canvas(&dsp);
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    canvas.createSprite(EQ_OVERLAY_W, EQ_OVERLAY_H);
    canvas.fillSprite(config.theme.background);

    const uint16_t bg   = config.theme.background;
    const uint16_t fg   = config.theme.status_active;
    const uint16_t dim  = config.theme.status_inactive;

    if (_eqPresetView) {
        // ── PRESET NÉZET ─────────────────────────────────────────────────────
        // 2 sor × 4 gomb, az egész sávban középre igazítva
        const uint8_t  COLS = 4, ROWS = 2;
        const uint16_t BTN_W = 100, BTN_H = 34, BTN_GAP_X = 8, BTN_GAP_Y = 8;
        const uint16_t gridW = COLS * BTN_W + (COLS - 1) * BTN_GAP_X;
        const uint16_t gridH = ROWS * BTN_H + (ROWS - 1) * BTN_GAP_Y;
        const uint16_t offX  = (EQ_OVERLAY_W - gridW) / 2;
        const uint16_t offY  = (EQ_OVERLAY_H - gridH) / 2;

        for (uint8_t i = 0; i < EQ_PRESET_COUNT; i++) {
            uint8_t  col  = i % COLS;
            uint8_t  row  = i / COLS;
            uint16_t bx   = offX + col * (BTN_W + BTN_GAP_X);
            uint16_t by   = offY + row * (BTN_H + BTN_GAP_Y);
            canvas.fillRoundRect(bx, by, BTN_W, BTN_H, 4, fg);
            canvas.drawRoundRect(bx, by, BTN_W, BTN_H, 4, bg);
            //canvas.setTextColor(fg, dim);
            canvas.setTextColor(bg, fg);
            canvas.setTextDatum(MC_DATUM);
            if (font_vlw_16) {
                canvas.loadFont(font_vlw_16);
                canvas.drawString(eqPresetNames[i], bx + BTN_W / 2, by + BTN_H / 2);
                canvas.unloadFont();
            } else {
                canvas.setTextSize(2);
                canvas.drawString(eqPresetNames[i], bx + BTN_W / 2, by + BTN_H / 2);
            }
        }
    } else {
        // ── SLIDER NÉZET ─────────────────────────────────────────────────────
        // 4 vízszintes slider: BASS / MID / TREB / BAL
        // bass/mid/treb: -6..+6 (WebUI alapján), balance: -16..+16
        // Elrendezés: 4 sor egyenletesen, jobb oldalon PRESETS gomb

        const int8_t vals[4] = {
            config.store.bass,
            config.store.middle,
            config.store.trebble,
            config.store.balance
        };
        const char* labels[4] = { "BAS", "MID", "TRB", "BAL" };
        // Per-slider max (szimmetrikus: -max..+max)
        const int8_t slMax[4] = { 6, 6, 6, 16 };

        // PRESETS gomb jobb szélen
        const uint16_t PRESETS_BTN_W = 70;
        const uint16_t PRESETS_BTN_H = EQ_OVERLAY_H - 20;
        const uint16_t PRESETS_BTN_X = EQ_OVERLAY_W - PRESETS_BTN_W - 8;
        const uint16_t PRESETS_BTN_Y = 10;
        canvas.fillRoundRect(PRESETS_BTN_X, PRESETS_BTN_Y, PRESETS_BTN_W, PRESETS_BTN_H, 4, fg);
        canvas.drawRoundRect(PRESETS_BTN_X, PRESETS_BTN_Y, PRESETS_BTN_W, PRESETS_BTN_H, 4, bg);
        canvas.setTextColor(bg, fg);
        canvas.setTextDatum(MC_DATUM);
        if (font_vlw_16) {
            canvas.loadFont(font_vlw_16);
            canvas.drawString("PRE", PRESETS_BTN_X + PRESETS_BTN_W / 2, PRESETS_BTN_Y + PRESETS_BTN_H / 2 - 9);
            canvas.drawString("SETS", PRESETS_BTN_X + PRESETS_BTN_W / 2, PRESETS_BTN_Y + PRESETS_BTN_H / 2 + 9);
            canvas.unloadFont();
        } else {
            canvas.setTextSize(1);
            canvas.drawString("PRE",  PRESETS_BTN_X + PRESETS_BTN_W / 2, PRESETS_BTN_Y + PRESETS_BTN_H / 2 - 8);
            canvas.drawString("SETS", PRESETS_BTN_X + PRESETS_BTN_W / 2, PRESETS_BTN_Y + PRESETS_BTN_H / 2 + 8);
        }

        // Slider terület (PRESETS gombtól balra)
        const uint16_t LABEL_W   = 36;
        const uint16_t VAL_W     = 22;
        const uint16_t SL_LEFT   = 8 + LABEL_W;
        const uint16_t SL_RIGHT  = PRESETS_BTN_X - 8 - VAL_W;
        const uint16_t SL_W      = SL_RIGHT - SL_LEFT;  // slider sáv szélesség
        const uint16_t ROW_H     = EQ_OVERLAY_H / 4;
        const uint8_t  SL_H      = 6;   // slider csík magasság
        const uint8_t  THUMB_W   = 10;
        const uint8_t  THUMB_H   = 18;

        for (uint8_t i = 0; i < 4; i++) {
            uint16_t rowY    = i * ROW_H;
            uint16_t centerY = rowY + ROW_H / 2;

            // Felirat
            canvas.setTextColor(fg, bg);
            canvas.setTextDatum(ML_DATUM);
            if (font_vlw_16) {
                canvas.loadFont(font_vlw_16);
                canvas.drawString(labels[i], 8, centerY);
                canvas.unloadFont();
            } else {
                canvas.setTextSize(1);
                canvas.drawString(labels[i], 8, centerY);
            }

            // Slider háttér sáv (-6..+6 → 13 lépés)
            // Középvonal
            uint16_t midX = SL_LEFT + SL_W / 2;
            // Teljes sáv
            canvas.fillRect(SL_LEFT, centerY - SL_H / 2, SL_W, SL_H, dim);
            // Középjelző
            canvas.fillRect(midX - 1, centerY - SL_H / 2 - 2, 2, SL_H + 4, fg);

            // Aktív rész (0 ponttól a thumb felé)
            int8_t   v    = vals[i];
            int8_t   vmax = slMax[i];   // -vmax..+vmax
            // thumb X: map -vmax..+vmax → SL_LEFT..SL_LEFT+SL_W-THUMB_W
            int32_t  range = 2 * vmax;
            uint16_t thumbX = SL_LEFT + (uint16_t)((v + vmax) * (SL_W - THUMB_W) / range);
            // Aktív sáv
            if (v > 0) {
                canvas.fillRect(midX, centerY - SL_H / 2, thumbX + THUMB_W / 2 - midX, SL_H, fg);
            } else if (v < 0) {
                uint16_t fillW = midX - (thumbX + THUMB_W / 2);
                if (fillW > 0) canvas.fillRect(thumbX + THUMB_W / 2, centerY - SL_H / 2, fillW, SL_H, fg);
            }

            // Thumb
            canvas.fillRoundRect(thumbX, centerY - THUMB_H / 2, THUMB_W, THUMB_H, 3, fg);

            // Érték szám jobb oldalon
            char vbuf[6];
            snprintf(vbuf, sizeof(vbuf), "%+d", (int)v);
            canvas.setTextColor(fg, bg);
            canvas.setTextDatum(MR_DATUM);
            if (font_vlw_16) {
                canvas.loadFont(font_vlw_16);
                canvas.drawString(vbuf, SL_RIGHT + VAL_W, centerY);
                canvas.unloadFont();
            } else {
                canvas.setTextSize(1);
                canvas.drawString(vbuf, SL_RIGHT + VAL_W, centerY);
            }
        }
    }

    canvas.pushSprite(EQ_OVERLAY_LEFT, EQ_OVERLAY_TOP);
    canvas.deleteSprite();
}

// ── EQ touch helper (touchscreen.cpp hívja) ──────────────────────────────────
// Visszaadja hogy az (x,y) koordináta az EQ overlay melyik elemét érinti.
// Csak ha az EQ overlay nyitva van és nem preset nézetben.
// preset nézet: visszaadja a preset indexet (0..7) vagy -1
// slider nézet: módosítja az EQ értéket és újrarajzol
// PRESETS gomb: átváltja a preset nézetet

void display_eq_touch(uint16_t x, uint16_t y, bool drag) {
    // Csak PLAYER módban és ha EQ overlay nyitva van
    if (!display.isEqOpen()) return;

    // Canvas-on belüli koordináták
    int16_t cx = (int16_t)x - EQ_OVERLAY_LEFT;
    int16_t cy = (int16_t)y - EQ_OVERLAY_TOP;
    if (cx < 0 || cy < 0 || cx >= EQ_OVERLAY_W || cy >= EQ_OVERLAY_H) return;

    if (_eqPresetView) {
        // Preset gomb hittest (csak tap, nem drag)
        if (drag) return;
        const uint8_t  COLS = 4;
        const uint16_t BTN_W = 100, BTN_H = 34, BTN_GAP_X = 8, BTN_GAP_Y = 8;
        const uint16_t gridW = COLS * BTN_W + (COLS - 1) * BTN_GAP_X;
        const uint16_t gridH = 2 * BTN_H + BTN_GAP_Y;
        const uint16_t offX  = (EQ_OVERLAY_W - gridW) / 2;
        const uint16_t offY  = (EQ_OVERLAY_H - gridH) / 2;

        for (uint8_t i = 0; i < EQ_PRESET_COUNT; i++) {
            uint8_t  col = i % COLS;
            uint8_t  row = i / COLS;
            uint16_t bx  = offX + col * (BTN_W + BTN_GAP_X);
            uint16_t by  = offY + row * (BTN_H + BTN_GAP_Y);
            if (cx >= bx && cx < bx + BTN_W && cy >= by && cy < by + BTN_H) {
                // Preset alkalmazása
                config.setTone(eqPresetValues[i][0], eqPresetValues[i][1], eqPresetValues[i][2]);
                _eqPresetView = false;
                // Újrarajzol slider nézetbe
                dsp.fillRect(EQ_OVERLAY_LEFT, EQ_OVERLAY_TOP, EQ_OVERLAY_W, EQ_OVERLAY_H, config.theme.background);
                display._eqInlineDraw();
                return;
            }
        }
        return;
    }

    // Slider nézetben: PRESETS gomb
    const uint16_t PRESETS_BTN_W = 70;
    const uint16_t PRESETS_BTN_H = EQ_OVERLAY_H - 20;
    const uint16_t PRESETS_BTN_X = EQ_OVERLAY_W - PRESETS_BTN_W - 8;
    const uint16_t PRESETS_BTN_Y = 10;
    if (!drag && cx >= PRESETS_BTN_X && cx < PRESETS_BTN_X + PRESETS_BTN_W &&
                 cy >= PRESETS_BTN_Y && cy < PRESETS_BTN_Y + PRESETS_BTN_H) {
        _eqPresetView = true;
        dsp.fillRect(EQ_OVERLAY_LEFT, EQ_OVERLAY_TOP, EQ_OVERLAY_W, EQ_OVERLAY_H, config.theme.background);
        display._eqInlineDraw();
        return;
    }

    // Slider hittest
    const uint16_t LABEL_W   = 36;
    const uint16_t VAL_W     = 22;
    const uint16_t SL_LEFT   = 8 + LABEL_W;
    const uint16_t SL_RIGHT  = PRESETS_BTN_X - 8 - VAL_W;
    const uint16_t SL_W      = SL_RIGHT - SL_LEFT;
    const uint16_t ROW_H     = EQ_OVERLAY_H / 4;
    const uint8_t  THUMB_W   = 10;

    if (cx >= SL_LEFT && cx < SL_RIGHT + VAL_W) {
        uint8_t sliderIdx = cy / ROW_H;
        if (sliderIdx > 3) sliderIdx = 3;

        // Per-slider tartomány
        const int8_t slMax[4] = { 6, 6, 6, 16 };
        int8_t vmax = slMax[sliderIdx];

        // Érték számítása: cx → -vmax..+vmax
        int16_t raw = (int16_t)(cx - SL_LEFT);
        if (raw < 0) raw = 0;
        if (raw > (int16_t)(SL_W - THUMB_W)) raw = SL_W - THUMB_W;
        int8_t newVal = (int8_t)map(raw, 0, (int16_t)(SL_W - THUMB_W), -vmax, vmax);

        bool changed = false;
        switch (sliderIdx) {
            case 0:
                if (newVal != config.store.bass) {
                    config.setTone(newVal, config.store.middle, config.store.trebble);
                    changed = true;
                }
                break;
            case 1:
                if (newVal != config.store.middle) {
                    config.setTone(config.store.bass, newVal, config.store.trebble);
                    changed = true;
                }
                break;
            case 2:
                if (newVal != config.store.trebble) {
                    config.setTone(config.store.bass, config.store.middle, newVal);
                    changed = true;
                }
                break;
            case 3:
                if (newVal != config.store.balance) {
                    config.setBalance(newVal);
                    changed = true;
                }
                break;
        }
        if (changed) {
            display._eqInlineDraw();
        }
    }
}

void Display::flip() {
    DisplayMutexGuard guard(portMAX_DELAY);
    dsp.flip();
}

void Display::invert() {
    DisplayMutexGuard guard(portMAX_DELAY);
    dsp.invert();
}

void Display::setContrast() {}

void Display::i2sReconfigBegin() {
#if DSP_MODEL == DSP_AXS15231B
    return;
#else
    if (displayMutex != nullptr) { xSemaphoreTakeRecursive(displayMutex, portMAX_DELAY); }
    // Do not open an SPI transaction here. We only need to ensure that an
    // in-flight display DMA transfer is finished and that no new frame starts
    // while the ESP32-S3 I2S channel is being reconfigured.
    dsp.waitDMA();
#endif
}

void Display::i2sReconfigEnd() {
#if DSP_MODEL != DSP_AXS15231B
    if (displayMutex != nullptr) { xSemaphoreGiveRecursive(displayMutex); }
#endif
}

void Display::waitDMA() {
#if DSP_MODEL == DSP_AXS15231B
    return;
#else
    DisplayMutexGuard guard(portMAX_DELAY);
    // Megvárja az LGFX háttérben futó DMA transzfer befejezését.
    // SD kártya SPI olvasás előtt kell hívni, hogy a megosztott buszon
    // ne legyen aktív DMA transzfer (csíkozás / kép szétesés megelőzése).
    dsp.waitDMA();
#endif
}

bool Display::deepsleep() {
#    if defined(DSP_OLED) || BRIGHTNESS_PIN != 255
    DisplayMutexGuard guard(portMAX_DELAY);
    if (!_panelAwake) { return true; }
#if DSP_MODEL != DSP_AXS15231B
    dsp.waitDMA();
#endif
    dsp.sleep();
    _panelAwake = false;
    log_i("##[PANEL]# sleep core=%u", (unsigned)xPortGetCoreID());
    return true;
#    endif
    return false;
}

void Display::wakeup() {
#    if defined(DSP_OLED) || BRIGHTNESS_PIN != 255
    DisplayMutexGuard guard(portMAX_DELAY);
    if (_panelAwake) { return; }
    dsp.wake();
    dsp.flip();
    dsp.invert();
    _panelAwake = true;
    log_i("##[PANEL]# wake core=%u", (unsigned)xPortGetCoreID());
#    endif
}

void Display::setBrightnessPercent(uint8_t percent) {
    percent = constrain(percent, 0, 100);
#    if DSP_MODEL == DSP_SSD1322
    uint8_t master = map(percent, 0, 100, 0, 15);
    uint8_t contrast = map(percent, 0, 100, 0, 255);
    dsp.ssd1322_setMasterContrast(master);
    dsp.ssd1322_setContrast(contrast);
#    else
#        if (BRIGHTNESS_PIN != 255)
    analogWrite(BRIGHTNESS_PIN, map(percent, 0, 100, 0, 255));
#        endif
#    endif
}

#    ifdef NAMEDAYS_FILE
void Display::loopDate(bool force) {
    if (_datewidget)   { _datewidget->draw(force); }
}
#    endif
//============================================================================================================================
#else // !DUMMYDISPLAY
//============================================================================================================================
void Display::init() {
    _createDspTask();
#    ifdef USE_NEXTION
    nextion.begin(true);
#    endif
}
void Display::_start() {
#    ifdef USE_NEXTION
    nextion.start();
#    endif
    config.setTitle(LANG::const_PlReady);
}

void Display::putRequest(displayRequestType_e type, int payload) {
    if (type == DSP_START) { _start(); }
#    ifdef USE_NEXTION
    requestParams_t request;
    request.type = type;
    request.payload = payload;
    request.generation = 0;
    nextion.putRequest(request);
#    else
    if (type == NEWMODE) { mode((displayMode_e)payload); }
#    endif
}
//============================================================================================================================
#endif // DUMMYDISPLAY

#ifndef DUMMYDISPLAY
void display_show_maintenance_screen() {
    DisplayMutexGuard guard(portMAX_DELAY);
    dsp.initDisplay();
    dsp.wake();
#    if BRIGHTNESS_PIN != 255
    pinMode(BRIGHTNESS_PIN, OUTPUT);
    analogWrite(BRIGHTNESS_PIN, map(100, 0, 100, 0, 255));
#    endif
    dsp.fillScreen(0x0000);
    uint16_t cx = dsp.width() / 2;
    uint16_t cy = dsp.height() / 2;
    dsp.setTextSize(2);
    dsp.setTextDatum(datum_t::middle_center);
    dsp.setTextColor(0xFFFF, 0x0000);
    dsp.drawString("LittleFS Serial Service", cx, cy - 20);
    dsp.drawString("Maintenance mode active", cx, cy + 14);
}
#else
void display_show_maintenance_screen() {}
#endif

// =====================================================================
//  MODESELECT — mód-választó menü
//  A STATIONS playlist mintájára: VOL_AREA (y=137..286) sávba rajzol,
//  a spektrum/óra/dátum/időjárás widgetek lockolva vannak.
//  Aktiválás: display.modeSelectorOpen()
//  Enkóder forgat: modeSelectorScroll(±1)
//  Megerősítés: modeSelectorConfirm()
//  Auto-bezárás 5 másodperc inaktivitás után.
// =====================================================================

#if DSP_MODEL != DSP_DUMMY

#define MS_AUTO_CLOSE_MS 5000

// --- Elérhető módok listájának összeállítása -----------------------
void Display::_msBuildList() {
    _msCount = 0;
    _msValues[_msCount] = PM_WEB;       _msLabels[_msCount++] = "WEB";
#ifdef USE_SD
    if (SDC_CS != 255) { _msValues[_msCount] = PM_SDCARD; _msLabels[_msCount++] = "SD"; }
#endif
#ifdef USE_DLNA
    _msValues[_msCount] = -1;           _msLabels[_msCount++] = "DLNA";
#endif
#ifdef USE_BLUETOOTH
    _msValues[_msCount] = PM_BLUETOOTH; _msLabels[_msCount++] = "BT";
#endif

    // Aktuális mód kijelölése
    int8_t curVal = (config.getMode() == PM_WEB &&
                     config.store.playlistSource == (uint8_t)PL_SRC_DLNA)
                    ? -1 : (int8_t)config.getMode();
    _msIdx = 0;
    for (uint8_t i = 0; i < _msCount; i++) {
        if (_msValues[i] == curVal) { _msIdx = i; break; }
    }
}

// --- Módlista kirajzolása a VOL_AREA sávba -------------------------
// Mintája: _drawPlaylist() — direkt dsp hívások, nincs sprite overlay
void Display::_msDraw() {
    if (_mode != MODESELECT) return;

    const uint16_t bg   = config.theme.background;
    const uint16_t fg   = config.theme.meta;
    const uint16_t sel  = config.theme.digit;
    const uint16_t brd  = config.theme.pmode;

    // Sáv törlése
    dsp.fillRect(0, VOL_AREA_TOP, VOL_AREA_WIDTH, VOL_AREA_HEIGHT, bg);

    // Fejléc — "SELECT MODE"
    dsp.setTextDatum(TC_DATUM);
    dsp.setTextColor(fg, bg);
    if (font_vlw_16) {
        dsp.loadFont(font_vlw_16);
        dsp.drawString("SELECT MODE", VOL_AREA_WIDTH / 2, VOL_AREA_TOP + 6);
        dsp.unloadFont();
    } else {
        dsp.setTextSize(2);
        dsp.drawString("SELECT MODE", VOL_AREA_WIDTH / 2, VOL_AREA_TOP + 6);
    }

    // Elválasztó vonal a fejléc alatt
    dsp.drawFastHLine(20, VOL_AREA_TOP + 28, VOL_AREA_WIDTH - 40, brd);

    // Módok listája — függőlegesen elosztva a maradék helyen
    const uint16_t listTop  = VOL_AREA_TOP + 35;
    const uint16_t listH    = VOL_AREA_HEIGHT - 40;
    const uint16_t itemH    = listH / _msCount;

    // Aktuális mód meghatározása az aktív jelölőhöz
    int8_t curVal = (config.getMode() == PM_WEB &&
                     config.store.playlistSource == (uint8_t)PL_SRC_DLNA)
                    ? -1 : (int8_t)config.getMode();

    for (uint8_t i = 0; i < _msCount; i++) {
        uint16_t y    = listTop + i * itemH;
        bool  isSelected = (i == _msIdx);
        bool  isActive   = (_msValues[i] == curVal);

        // Kijelölt elem háttere
        if (isSelected) {
            dsp.fillRoundRect(10, y, VOL_AREA_WIDTH - 20, itemH - 2, 4, sel);
            dsp.setTextColor(bg, sel);
        } else {
            dsp.setTextColor(fg, bg);
        }

        // Módnév
        dsp.setTextDatum(ML_DATUM);
        if (font_vlw_20) {
            dsp.loadFont(font_vlw_20);
            dsp.drawString(_msLabels[i], 28, y + itemH / 2);
            dsp.unloadFont();
        } else {
            dsp.setTextSize(2);
            dsp.drawString(_msLabels[i], 28, y + itemH / 2);
        }

        // Aktív mód jelölő (kis kör a jobb oldalon)
        if (isActive) {
            uint16_t dotColor = isSelected ? bg : sel;
            dsp.fillCircle(VOL_AREA_WIDTH - 22, y + itemH / 2, 5, dotColor);
        }
    }
}

// --- Megnyitás (FŐTASK-BÓL HÍVHATÓ) -------------------------------
void Display::modeSelectorOpen() {
    if (_mode == MODESELECT) return;
    _msAutoCloseMs = millis() + MS_AUTO_CLOSE_MS;
    putRequest(NEWMODE, MODESELECT); // display task: widget lock + _msBuildList + _msDraw
}

// --- Görgetés (FŐTASK-BÓL HÍVHATÓ) --------------------------------
void Display::modeSelectorScroll(int dir) {
    if (_mode != MODESELECT) return;
    if (_msCount == 0) return;
    _msAutoCloseMs = millis() + MS_AUTO_CLOSE_MS;
    if (dir > 0) { _msIdx = (_msIdx + 1) % _msCount; }
    else         { _msIdx = (_msIdx + _msCount - 1) % _msCount; }
    putRequest(DRAWMODESELECT); // display task: _msDraw()
}

// --- Megerősítés (FŐTASK-BÓL HÍVHATÓ) ----------------------------
void Display::modeSelectorConfirm() {
    if (_mode != MODESELECT) return;
    int8_t val = _msValues[_msIdx];
    purgeQueuedRequestType(DRAWMODESELECT);
    putRequest(NEWMODE, PLAYER); // bezárás
    if (val == -1) {
        config.toggleMode();
    } else {
#ifdef USE_DLNA
        // DLNA is represented as PM_WEB + PL_SRC_DLNA. Selecting WEB must
        // therefore switch the logical playlist source as well as the mode.
        if (val == PM_WEB) {
            config.store.playlistSource = PL_SRC_WEB;
            config.saveValue(&config.store.playlistSource, (uint8_t)PL_SRC_WEB);
        }
#endif
        config.changeMode(val);
    }
}

// --- Bezárás módváltás nélkül (FŐTASK-BÓL HÍVHATÓ) ---------------
void Display::modeSelectorClose() {
    if (_mode != MODESELECT) return;
    purgeQueuedRequestType(DRAWMODESELECT);
    putRequest(NEWMODE, PLAYER); // display task unlock-ol és frissít
}

// --- Auto-bezárás (DISPLAY LOOP()-BÓL — display task kontextus) ---
void Display::modeSelectorTick() {
    if (_mode != MODESELECT) return;
    if (millis() < _msAutoCloseMs) return;
    // _mode = PLAYER-t NEM állítjuk itt közvetlenül — ha megtennénk,
    // akkor putRequest(NEWMODE, PLAYER) feldolgozásakor _swichMode() azt
    // látná hogy prevMode = PLAYER (nem MODESELECT), és a MODESELECT
    // cleanup blokk (widget unlock + teljes player refresh) nem futna le
    // → maradna a "kosz" a képernyőn.
    // A _swichMode(PLAYER) fogja a prevMode=MODESELECT ágban kezelni az
    // unlock-ot és a teljes player nézet újrarajzolását.
//    if (_spectrum)    { _spectrum->lock(!config.store.vumeter); }
//    if (_clock)       { _clock->lock(false); }
//    if (_datewidget)  { _datewidget->lock(false); }
//    if (_weather)     { _weather->lock(!config.store.showweather); }
//    if (_weatherIcon) { _weatherIcon->lock(!config.store.showweather); }
//    _mode = PLAYER;
    resetQueue();
    putRequest(NEWMODE, PLAYER);
    putRequest(NEWSTATION);
    putRequest(DBITRATE);
}

#else // DSP_DUMMY stubs
void Display::modeSelectorOpen()         {}
void Display::modeSelectorScroll(int)    {}
void Display::modeSelectorConfirm()      {}
void Display::modeSelectorClose()        {}
void Display::modeSelectorTick()         {}
#endif
