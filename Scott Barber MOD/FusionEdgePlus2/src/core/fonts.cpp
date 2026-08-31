#include "fonts.h"
#include <LittleFS.h>
#include <stdlib.h>
#include <LovyanGFX.hpp>
#include "../displays/display_select.h"
#include "config.h"

// ================= VLW =================
uint8_t* font_vlw_9 = nullptr;
uint8_t* font_vlw_11 = nullptr; // TEST: robotom11.vlw
uint8_t* font_vlw_12 = nullptr;
uint8_t* font_vlw_16 = nullptr;
uint8_t* font_vlw_18 = nullptr;
uint8_t* font_vlw_20 = nullptr;
uint8_t* font_vlw_22 = nullptr;
uint8_t* font_vlw_24 = nullptr;
uint8_t* font_vlw_26 = nullptr;
uint8_t* font_vlw_36 = nullptr;
uint8_t* font_vlw_clock = nullptr;
uint8_t* font_vlw_clock_sec = nullptr;

static uint8_t* font_vlw_clock_digi = nullptr;
static uint8_t* font_vlw_clock_sec_digi = nullptr;
static uint8_t* font_vlw_clock_calibri = nullptr;
static uint8_t* font_vlw_clock_sec_calibri = nullptr;
static uint8_t* font_vlw_clock_android = nullptr;
static uint8_t* font_vlw_clock_sec_android = nullptr;
static uint8_t* font_vlw_clock_oldtimer = nullptr;
static uint8_t* font_vlw_clock_sec_oldtimer = nullptr;
static uint8_t* font_vlw_clock_laradot = nullptr;
static uint8_t* font_vlw_clock_sec_laradot = nullptr;
static uint8_t* font_vlw_clock_decoderr = nullptr;
static uint8_t* font_vlw_clock_sec_decoderr = nullptr;
static uint8_t* font_vlw_clock_squarefont = nullptr;
static uint8_t* font_vlw_clock_sec_squarefont = nullptr;

// ================= GFX FONTOK =================
using namespace lgfx::v1::fonts;

const GFXfont Clock_GFXfont     = FreeMonoBold24pt7b;
const GFXfont Clock_GFXfont_sec = FreeMonoBold18pt7b;

#ifndef DSP_OLED
const GFXfont* font_clock = &Clock_GFXfont;
const GFXfont* font_clock_sec = &Clock_GFXfont_sec;
#else
const GFXfont* font_clock = &Clock_GFXfont_sec;
const GFXfont* font_clock_sec = &Clock_GFXfont_sec;
#endif

// ================= FONT BETÖLTÉS =================
static uint8_t* loadFontFile(const char* path) {
    if (!LittleFS.exists(path)) {
        Serial.printf("[FONT] FILE NOT FOUND: %s\n", path);
        return nullptr;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[FONT] OPEN FAIL: %s\n", path);
        return nullptr;
    }

    size_t size = f.size();
    if (size == 0) {
        Serial.printf("[FONT] EMPTY FILE: %s\n", path);
        f.close();
        return nullptr;
    }

    uint8_t* buf = (uint8_t*)ps_malloc(size);

    if (!buf) {
        Serial.printf("[FONT] PSRAM FAIL (%u), fallback DRAM\n", (unsigned)size);
        buf = (uint8_t*)malloc(size);

        if (!buf) {
            Serial.printf("[FONT] DRAM FAIL (%u)\n", (unsigned)size);
            f.close();
            return nullptr;
        }
    }

    size_t readed = f.read(buf, size);
    f.close();

    if (readed != size) {
        Serial.printf("[FONT] READ ERROR: %s\n", path);
        free(buf);
        return nullptr;
    }

    Serial.printf("[FONT] LOADED: %s (%u bytes)\n", path, (unsigned)size);
    return buf;
}

bool loadFonts() {
    freeFonts();

    // Kötelező fontok – hiányukára a rendszer visszaesik alap fontokra, de funkcionális marad
    font_vlw_9  = loadFontFile("/fonts/roboto9.vlw");
    font_vlw_12 = loadFontFile("/fonts/roboto12.vlw");
    font_vlw_16 = loadFontFile("/fonts/roboto16.vlw");
    font_vlw_18 = loadFontFile("/fonts/roboto18.vlw");
    font_vlw_20 = loadFontFile("/fonts/roboto20.vlw");
    font_vlw_22 = loadFontFile("/fonts/roboto22.vlw");
    font_vlw_24 = loadFontFile("/fonts/roboto24.vlw");
    font_vlw_26 = loadFontFile("/fonts/roboto26.vlw");
    font_vlw_36 = loadFontFile("/fonts/roboto36.vlw");

    // TEST font - only has a 9-glyph subset (, - . 0 9 A Z a z), NOT part of the
    // required-fonts check below. Swap for the full charset once it's ready.
    font_vlw_11 = loadFontFile("/fonts/robotom11.vlw");

    font_vlw_clock_digi        = loadFontFile("/fonts/digi7_it.vlw");
    font_vlw_clock_sec_digi    = loadFontFile("/fonts/digi7_it_sec.vlw");
    font_vlw_clock_calibri     = loadFontFile("/fonts/calibri.vlw");
    font_vlw_clock_sec_calibri = loadFontFile("/fonts/calibri_sec.vlw");
    font_vlw_clock_android     = loadFontFile("/fonts/androidclock.vlw");
    font_vlw_clock_sec_android = loadFontFile("/fonts/androidclock_sec.vlw");
    font_vlw_clock_oldtimer     = loadFontFile("/fonts/oldtimer.vlw");
    font_vlw_clock_sec_oldtimer = loadFontFile("/fonts/oldtimer_sec.vlw");
    font_vlw_clock_laradot      = loadFontFile("/fonts/laradotserif.vlw");
    font_vlw_clock_sec_laradot  = loadFontFile("/fonts/laradotserif_sec.vlw"); 
    font_vlw_clock_decoderr     = loadFontFile("/fonts/decoderr.vlw");
    font_vlw_clock_sec_decoderr = loadFontFile("/fonts/decoderr_sec.vlw");  
    font_vlw_clock_squarefont   = loadFontFile("/fonts/squarefont.vlw");
    font_vlw_clock_sec_squarefont = loadFontFile("/fonts/squarefont_sec.vlw");        

    setClockFontStyle(config.store.clockFontStyle);

    bool required = font_vlw_9 && font_vlw_12 && font_vlw_16 && font_vlw_18 &&
                    font_vlw_20 && font_vlw_22 && font_vlw_24 && font_vlw_26 && font_vlw_36;
    bool optional = (font_vlw_clock_digi && font_vlw_clock_sec_digi) || (font_vlw_clock_calibri && font_vlw_clock_sec_calibri) || (font_vlw_clock_android && font_vlw_clock_sec_android);

    if (!optional) Serial.println("[FONT] Optional fonts not fully loaded now uses default GFX fonts.");
    return required;
}

void setClockFontStyle(uint8_t style) {
    switch (style) {
        case CLOCKFONT_STYLE_CALIBRI:      font_vlw_clock = font_vlw_clock_calibri;    font_vlw_clock_sec = font_vlw_clock_sec_calibri;    break;
        case CLOCKFONT_STYLE_ANDROIDCLOCK: font_vlw_clock = font_vlw_clock_android;    font_vlw_clock_sec = font_vlw_clock_sec_android;    break;
        case CLOCKFONT_STYLE_OLDTIMER:     font_vlw_clock = font_vlw_clock_oldtimer;   font_vlw_clock_sec = font_vlw_clock_sec_oldtimer;   break;
        case CLOCKFONT_STYLE_LARADOT:      font_vlw_clock = font_vlw_clock_laradot;    font_vlw_clock_sec = font_vlw_clock_sec_laradot;    break;
        case CLOCKFONT_STYLE_DECODERR:     font_vlw_clock = font_vlw_clock_decoderr;   font_vlw_clock_sec = font_vlw_clock_sec_decoderr;   break;
        case CLOCKFONT_STYLE_SQUAREFONT:   font_vlw_clock = font_vlw_clock_squarefont; font_vlw_clock_sec = font_vlw_clock_sec_squarefont; break;
        default:                           font_vlw_clock = font_vlw_clock_digi;        font_vlw_clock_sec = font_vlw_clock_sec_digi;       break;
    }
}

void getClockFontStylePointers(uint8_t style, uint8_t** mainFont, uint8_t** secFont) {
    uint8_t* m = nullptr;
    uint8_t* s = nullptr;
    switch (style) {
        case CLOCKFONT_STYLE_CALIBRI:      m = font_vlw_clock_calibri;    s = font_vlw_clock_sec_calibri;    break;
        case CLOCKFONT_STYLE_ANDROIDCLOCK: m = font_vlw_clock_android;    s = font_vlw_clock_sec_android;    break;
        case CLOCKFONT_STYLE_OLDTIMER:     m = font_vlw_clock_oldtimer;   s = font_vlw_clock_sec_oldtimer;   break;
        case CLOCKFONT_STYLE_LARADOT:      m = font_vlw_clock_laradot;    s = font_vlw_clock_sec_laradot;    break;
        case CLOCKFONT_STYLE_DECODERR:     m = font_vlw_clock_decoderr;   s = font_vlw_clock_sec_decoderr;   break;
        case CLOCKFONT_STYLE_SQUAREFONT:   m = font_vlw_clock_squarefont; s = font_vlw_clock_sec_squarefont; break;
        default:                           m = font_vlw_clock_digi;        s = font_vlw_clock_sec_digi;       break;
    }
    if (mainFont) *mainFont = m;
    if (secFont)  *secFont  = s;
}

void freeFonts() {
    if (font_vlw_9) {
        free(font_vlw_9);
        font_vlw_9 = nullptr;
    }
    if (font_vlw_12) {
        free(font_vlw_12);
        font_vlw_12 = nullptr;
    }
    if (font_vlw_16) {
        free(font_vlw_16);
        font_vlw_16 = nullptr;
    }
    if (font_vlw_18) {
        free(font_vlw_18);
        font_vlw_18 = nullptr;
    }
    if (font_vlw_20) {
        free(font_vlw_20);
        font_vlw_20 = nullptr;
    }
    if (font_vlw_22) {
        free(font_vlw_22);
        font_vlw_22 = nullptr;
    }
    if (font_vlw_24) {
        free(font_vlw_24);
        font_vlw_24 = nullptr;
    }

    if (font_vlw_26) {
        free(font_vlw_26);
        font_vlw_26 = nullptr;
    }
    if (font_vlw_36) {
        free(font_vlw_36);
        font_vlw_36 = nullptr;
    }
    if (font_vlw_11) {
        free(font_vlw_11);
        font_vlw_11 = nullptr;
    }
    if (font_vlw_clock_digi) {
        free(font_vlw_clock_digi);
        font_vlw_clock_digi = nullptr;
    }
    if (font_vlw_clock_sec_digi) {
        free(font_vlw_clock_sec_digi);
        font_vlw_clock_sec_digi = nullptr;
    }
    if (font_vlw_clock_calibri) {
        free(font_vlw_clock_calibri);
        font_vlw_clock_calibri = nullptr;
    }
    if (font_vlw_clock_sec_calibri) {
        free(font_vlw_clock_sec_calibri);
        font_vlw_clock_sec_calibri = nullptr;
    }
    if (font_vlw_clock_android) {
        free(font_vlw_clock_android);
        font_vlw_clock_android = nullptr;
    }
    if (font_vlw_clock_sec_android) {
        free(font_vlw_clock_sec_android);
        font_vlw_clock_sec_android = nullptr;
    }
    if (font_vlw_clock_oldtimer) {
        free(font_vlw_clock_oldtimer);
        font_vlw_clock_oldtimer = nullptr;
    }
    if (font_vlw_clock_sec_oldtimer) {
        free(font_vlw_clock_sec_oldtimer);
        font_vlw_clock_sec_oldtimer = nullptr;
    }
    if (font_vlw_clock_laradot) {
        free(font_vlw_clock_laradot);
        font_vlw_clock_laradot = nullptr;
    }
    if (font_vlw_clock_sec_laradot) {
        free(font_vlw_clock_sec_laradot);
        font_vlw_clock_sec_laradot = nullptr;
    }
    if (font_vlw_clock_decoderr) {
        free(font_vlw_clock_decoderr);
        font_vlw_clock_decoderr = nullptr;
    }
    if (font_vlw_clock_sec_decoderr) {
        free(font_vlw_clock_sec_decoderr);
        font_vlw_clock_sec_decoderr = nullptr;
    }
    if (font_vlw_clock_squarefont) {
        free(font_vlw_clock_squarefont);
        font_vlw_clock_squarefont = nullptr;
    }
    if (font_vlw_clock_sec_squarefont) {
        free(font_vlw_clock_sec_squarefont);
        font_vlw_clock_sec_squarefont = nullptr;
    }

    font_vlw_clock = nullptr;
    font_vlw_clock_sec = nullptr;
}

// ================= GFX MÉRET SEGÉDEK =================

static inline GFXglyph* getGlyph(const GFXfont* f, uint8_t c) {
    return f->glyph + c;
}

#ifndef CLOCKFONT5x7

uint8_t font_charWidth(unsigned char c, bool sec) {
    const GFXfont* f = sec ? font_clock_sec : font_clock;
    GFXglyph*      glyph = getGlyph(f, c - 0x20);
    return pgm_read_byte(&glyph->xAdvance);
}

uint16_t font_textHeight(bool sec) {
    const GFXfont* f = sec ? font_clock_sec : font_clock;
    GFXglyph*      glyph = getGlyph(f, '8' - 0x20);
    return pgm_read_byte(&glyph->height);
}

#else

uint8_t font_charWidth(unsigned char, bool) {
    return CHARWIDTH * TIME_SIZE;
}

uint16_t font_textHeight(bool) {
    return CHARHEIGHT * TIME_SIZE;
}

#endif

uint16_t font_textWidth(const char* txt, bool sec) {
    uint16_t w = 0;
    while (*txt) { w += font_charWidth(*txt++, sec); }
    return w;
}
