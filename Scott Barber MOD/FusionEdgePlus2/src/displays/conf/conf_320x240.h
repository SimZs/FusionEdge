// clang-format off
#pragma once

#include "../../core/config.h"
#include "../../core/display.h"
#include "../widgets/widgets.h"

#define DSP_WIDTH       320
#define DSP_HEIGHT      240
#define TFT_FRAMEWDT      6
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2
#define CLOCK_HEIGHT     70

#if BITRATE_FULL
  #define TITLE_FIX  30
#else
  #define TITLE_FIX   0
#endif
#define bootLogoTop 6

// Station icon (64x64, fixed) sits top-left; text columns start clear of it.
#define TEXTCOL_LEFT (TFT_FRAMEWDT+74)

/* SROLLS  */                            /* {{ left, top, fontsize, align }, buffsize, uppercase, width, scrolldelay, scrolldelta, scrolltime } */
const ScrollConfig metaConf       PROGMEM = {{ TEXTCOL_LEFT,  4, 20, WA_CENTER }, 140, true,  DSP_WIDTH-TFT_FRAMEWDT-TEXTCOL_LEFT, 5000, 3, 30 };
const ScrollConfig title1Conf     PROGMEM = {{ TEXTCOL_LEFT, 28, 16, WA_CENTER }, 140, true,  DSP_WIDTH-TFT_FRAMEWDT-TEXTCOL_LEFT-TITLE_FIX, 5000, 3, 30 };
const ScrollConfig title2Conf     PROGMEM = {{ TEXTCOL_LEFT, 48, 16, WA_CENTER }, 140, false, DSP_WIDTH-TFT_FRAMEWDT-TEXTCOL_LEFT, 5000, 3, 30 };
const ScrollConfig playlistConf   PROGMEM = {{ TFT_FRAMEWDT, 110, 16, WA_LEFT }, 140, true, MAX_WIDTH, 1000, 3, 30 };
const ScrollConfig apTitleConf    PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 4, WA_CENTER }, 140, false, MAX_WIDTH, 0, 3, 20 };
const ScrollConfig apSettConf     PROGMEM = {{ TFT_FRAMEWDT, DSP_HEIGHT-TFT_FRAMEWDT-16, 2, WA_LEFT }, 140, false, MAX_WIDTH, 0, 3, 30 };
const ScrollConfig weatherConf    PROGMEM = {{ TFT_FRAMEWDT, DSP_HEIGHT - 130, 16, WA_CENTER }, 254, false, MAX_WIDTH, 0, 3, 60 };

/* BACKGROUNDS  */                       /* {{ left, top, fontsize, align }, width, height, outlined } */
const FillConfig   metaBGConf     PROGMEM = {{ 3, 88, 0, WA_CENTER }, DSP_WIDTH - 6, 1, true }; // LINE
const FillConfig   metaBGConfInv  PROGMEM = {{ 0, 38, 0, WA_LEFT }, DSP_WIDTH, 2, false };
const FillConfig   volbarConf     PROGMEM = {{TFT_FRAMEWDT, DSP_HEIGHT - TFT_FRAMEWDT - 6, 0, WA_LEFT}, MAX_WIDTH, 3, true};
const FillConfig   playlBGConf    PROGMEM = {{ 0, 106, 0, WA_LEFT }, DSP_WIDTH, 22, false };
const FillConfig   heapbarConf    PROGMEM = {{ TFT_FRAMEWDT, DSP_HEIGHT-2, 0, WA_LEFT }, DSP_WIDTH-20, 2, false };

// left,top, width, height, textsize, align, border, radius, fill, paddingX, paddingY
const textBoxConfig bootstrConf   PROGMEM = {40, 180, 240, 30, 12, WA_CENTER, 1, 4, true, 0, 0};
const textBoxConfig ipBoxConf     PROGMEM = {6, 216, 110, 18, 11, WA_CENTER, 1, 3, true, 0, 0};

// left, top, width, height, segments, segWidth, segGap, segHeight, iconW, iconH, radius, border
const VolumeWidgetConfig bufferWidgetConf PROGMEM = {120, 216, 44, 18, 8, 2, 1, 4, 7, 7, 3, 1}; // Audio buffer widget
const textBoxConfig rssiBoxConf   PROGMEM = {262, 216, 52, 18, 11, WA_CENTER, 1, 3, true, 0, 0};


/* WIDGETS  */ /* { left, top, fontsize, align } */
const WidgetConfig numConf        PROGMEM = {0, 90, 52, WA_CENTER};
const WidgetConfig apNameConf     PROGMEM = {TFT_FRAMEWDT, 55, 3, WA_CENTER};
const WidgetConfig apName2Conf    PROGMEM = {TFT_FRAMEWDT, 79, 3, WA_CENTER};
const WidgetConfig apPassConf     PROGMEM = {TFT_FRAMEWDT, 130, 3, WA_CENTER};
const WidgetConfig apPass2Conf    PROGMEM = {TFT_FRAMEWDT, 154, 3, WA_CENTER};
const WidgetConfig bootWdtConf    PROGMEM = {0, 128, 1, WA_CENTER};
const WidgetConfig clockConf      PROGMEM = { 180, 132, 52, WA_RIGHT };
const WidgetConfig dateConf       PROGMEM = { TFT_FRAMEWDT, 91, 16, WA_CENTER };

// Speed, width, barwidth
const ProgressConfig bootPrgConf  PROGMEM = {90, 10, 4};

// left, top, width, height, segments, segWidth, segGap, segHeight, iconW, iconH, radius, border
//const VolumeWidgetConfig volConf PROGMEM = {TFT_FRAMEWDT, 216, 60, 18, 14, 2, 1, 5, 7, 9, 3, 1};
const VolumeWidgetConfig volConf PROGMEM = {168, 216, 56, 18, 12, 2, 1, 5, 7, 9, 3, 1};

// left, top, width, height, image1, image2, image3, image4
// NOTE: same 30x25 PNG assets as conf_480x320.h — wifiWidget draws the PNG at its native
// resolution (no scaling), so this must keep pointing at the existing image files.
// left=228: sits in the gap between volConf (ends x=224) and rssiBoxConf (starts x=262),
// clearing both with ~4px margin. IP box and volume widget were swapped (IP now at x=6,
// buffer widget shifted to x=120, volume now at x=168) but the overall envelope from
// x=6 to x=224 is unchanged, so this gap still holds.
const WifiWidgetConfig wifiConf PROGMEM = {228, 210, 30, 25, "/images/wifi_1_30x25.png", "/images/wifi_2_30x25.png", "/images/wifi_3_30x25.png", "/images/wifi_4_30x25.png"};

//left, top, textsize, align, border, radius, fill, paddingX, paddingY, dimension
/* BITRATE */
const BitrateBoxConfig bitrateConf PROGMEM = { (uint16_t)(DSP_WIDTH - TFT_FRAMEWDT - 70), 68, 12, WA_RIGHT, 1, 3, false, 2, 1, 34 };

/* PLAYMODE */
const BitrateBoxConfig pmodeConf   PROGMEM = { TFT_FRAMEWDT, 68, 12, WA_LEFT, 1, 3, false, 2, 1, 34 };

/* STRINGS  */
const char numtxtFmt[]  PROGMEM = "%d";
const char rssiFmt[]    PROGMEM = "WiFi %ddBm";
const char iptxtFmt[]   PROGMEM = "%s";
const char voltxtFmt[]  PROGMEM = "\023\025%d%%";
const char bitrateFmt[] PROGMEM = "%d kBs";

/* MOVES  */
const MoveConfig clockMove PROGMEM = { 180, 132, -1 };

// clang-format on
