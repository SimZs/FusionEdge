#include "backlight.h"
#include <Arduino.h>
#include "../../core/options.h"
#include "../../core/config.h"
#include "../../core/display.h"
#include "../../core/network.h"

#if (BRIGHTNESS_PIN != 255)

BacklightPlugin backlightPlugin; // globalis példány

BacklightPlugin::BacklightPlugin() {}

void backlightPluginInit() {
    pm.add(&backlightPlugin);
}

bool BacklightPlugin::isFading() const {
    // Serial.println("backlight.cpp-->isFading()");
    return state == FADING;
}

bool BacklightPlugin::isDimmed() const {
    //  Serial.println("backlight.cpp-->isDimmed()");
    return state == DIMMED;
}

bool BacklightPlugin::isFadeControl() {
    if (state == FADING || state == DIMMED) {
        backlightPlugin.wake();
        return true;
    }
    activity();
    // Ébresztés után ennyi ideig nem veszi figyelembe az érintéseket.
    if ((millis() - lastUiWakeMs) < 500) {
        return true;
    } else {
        return false;
    }
}

void BacklightPlugin::activity() {
    lastActivity = millis();
}

void BacklightPlugin::setBacklight(uint8_t backLight) {
    #if BRIGHTNESS_PIN != 255
    if (!config.store.dspon) { display.wakeup(); }
    display.setBrightnessPercent(backLight);
    if (!config.store.dspon) { config.store.dspon = true; }
    #endif
}

void BacklightPlugin::setEnabled(bool enabled) {
    if (!enabled) {
        if (brightnessCaptured) setBacklight(config.store.brightness);
        brightnessCaptured = false;
        state = WAIT;
        log_i("##[FADE]# disabled, brightness=%u%%", (unsigned)config.store.brightness);
        return;
    }

    if (brightnessCaptured && currentBrightness != config.store.brightness) {
        setBacklight(config.store.brightness);
    }
    brightnessCaptured = false;
    state = WAIT;
    lastActivity = millis();
    log_i("##[FADE]# armed delay=%us target=%u%% step=%u%% brightness=%u%%",
          (unsigned)config.store.fadeStartDelay,
          (unsigned)config.store.fadeTarget,
          (unsigned)(config.store.fadeStep ? config.store.fadeStep : 1),
          (unsigned)config.store.brightness);
}

void BacklightPlugin::wake() {
    if (brightnessCaptured && currentBrightness != config.store.brightness) {
        setBacklight(config.store.brightness);
        log_i("##[FADE]# wake, brightness=%u%%", (unsigned)config.store.brightness);
    }
    lastUiWakeMs = millis();
    lastActivity = lastUiWakeMs;
    brightnessCaptured = false;
    state = WAIT;
}

void BacklightPlugin::tick() {
    if (network.status == SOFT_AP) return;
    if (!display.ready()) return;
    // Serial.printf("Backlight.cpp-->config.store.fadeEnabled: %d\n",config.store.fadeEnabled);
    // Serial.printf("Backlight.cpp-->config.store.fadeStartDelay: %d\n",config.store.fadeStartDelay);
    // Serial.printf("Backlight.cpp-->config.store.fadeTarget: %d\n",config.store.fadeTarget);
    // Serial.printf("Backlight.cpp-->config.store.fadeStep: %d\n",config.store.fadeStep);
    if (config.store.fadeEnabled == 0) { // ha ki van kapcsolva a fade funkció
        if (state == FADING || state == DIMMED) { wake(); }
        return;
    }
    displayMode_e m = display.mode();
    if (m != lastMode) {
        lastMode = m;
        wake();
    }
    if (display.mode() != PLAYER) { return; }
    if (!brightnessCaptured && config.store.brightness > 0) { // ha nincs rögzített fényerő és a mentett fényerő > 0
        savedBrightness = config.store.brightness;            // beolvassa az eredeti fényerőt
        currentBrightness = savedBrightness;                  // az aktuális fényerő az eredeti fényerő lesz
        brightnessCaptured = true;                            // rögzítés állapota igaz
    }
    uint32_t now = millis();
    switch (state) {
        case WAIT:
            if (now - lastActivity > (uint32_t)config.store.fadeStartDelay * 1000UL) { // ha eltelt a várakozási idő
                targetBrightness = config.store.fadeTarget > 100 ? 100 : config.store.fadeTarget;
                if (currentBrightness <= targetBrightness) {
                    state = DIMMED;
                    log_i("##[FADE]# no dimming needed: brightness=%u%% target=%u%%",
                          (unsigned)currentBrightness, (unsigned)targetBrightness);
                    break;
                }
                state = FADING;                                            // átkapcsol FADING állapotba
                lastFadeStep = now;                                        // a mostani idő lesz a fade lépcső kezdete
                log_i("##[FADE]# start %u%% -> %u%%",
                      (unsigned)currentBrightness, (unsigned)targetBrightness);
            }
            break;
        case FADING:
            if (now - lastFadeStep < FADE_PERIOD) break; // ha a két lépcső közötti idő még nem telt el, akkor kilép
            lastFadeStep = now;                          // a mostani idő lesz a fade lépcső kezdete
            if (currentBrightness > targetBrightness) {  // ha még kell tovább sötétíteni
                const uint8_t step = config.store.fadeStep ? config.store.fadeStep : 1;
                const uint8_t remaining = currentBrightness - targetBrightness;
                currentBrightness -= step < remaining ? step : remaining;
                setBacklight(currentBrightness); // beállítja a képernyőt
                if (currentBrightness == targetBrightness) {
                    state = DIMMED;
                    log_i("##[FADE]# dimmed to %u%%", (unsigned)currentBrightness);
                }
            } else {
                state = DIMMED;
            }
            break;
        case DIMMED: break; // ha már DIMMED csak továbblép
    }
}

void BacklightPlugin::on_setup() {
    Serial.printf("BACKLIGHT -> .on_setup this=%p\n", this);
}

void BacklightPlugin::on_ticker() {
    tick();
}

void BacklightPlugin::on_start_play() {
    wake();
}

void BacklightPlugin::on_stop_play() {
    wake();
}

void BacklightPlugin::on_track_change() {
    // wake();
}

void BacklightPlugin::on_btn_click(controlEvt_e& btnid) {
    wake();
}

void BacklightPlugin::on_user_activity(bool& consumed) {
    if (config.store.fadeEnabled && isFadeControl()) {
        consumed = true;
    }
}

void BacklightPlugin::on_display_player() {
    wake();
}

#endif
