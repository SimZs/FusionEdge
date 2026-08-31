#include "../core/options.h"
#if DSP_MODEL == DSP_AXS15231B
#    include "display_select.h"
#    include "../core/config.h"

DspCore::DspCore() {}
void DspCore::initDisplay() {
    Serial.printf("displayAXS15231B.cpp: initDisplay()\n");

    if (!init()) {
        Serial.println("LGFX AXS15231B Init FAIL");
    } else {
        Serial.println("LGFX AXS15231B Init OK");
    }

#    if TS_MODEL == TS_MODEL_AXS15231B
    if (touch()) {
        Serial.println("AXS15231B touch: found");
    } else {
        Serial.println("AXS15231B touch: NOT found / init failed");
    }
#    endif

    setTextWrap(false);
    setTextSize(1);
    fillScreen(0x0000);
    invert();
    flip();
    Serial.printf("displayAXS15231B.cpp: initDisplay() DONE\n");
}

void DspCore::clearDsp(bool black) {
    fillScreen(black ? 0 : config.theme.background);
}

void DspCore::flip() {
    uint8_t rotation = static_cast<uint8_t>(DEFAULT_SCREEN_ROTATION & 0x03);
    if (config.store.flipscreen) {
        rotation = static_cast<uint8_t>((rotation + 2) & 0x03);
    }
    setRotation(rotation);
}

void DspCore::invert() {
    invertDisplay(config.store.invertdisplay);
}

void DspCore::sleep(void) {
    yoDisplay::sleep();
    delay(150);
}

void DspCore::wake(void) {
    yoDisplay::wakeup();
    delay(150);
}

void DspCore::createBootSprite() {
    if (_bootSpr) return;

    _bootSpr = new LGFX_Sprite(this);
    _bootSpr->setPsram(true);
    _bootSpr->createSprite(width(), height());
}

void DspCore::deleteBootSprite() {
    if (_bootSpr) {
        _bootSpr->deleteSprite();
        delete _bootSpr;
        _bootSpr = nullptr;
    }
}

LGFX_Sprite* DspCore::getBootSprite() {
    return _bootSpr;
}

#endif
