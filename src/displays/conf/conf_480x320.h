// clang-format off
#pragma once

#include "../../core/config.h"
#include "../../core/display.h"
#include "../widgets/widgets.h"

#define DSP_WIDTH       480
#define DSP_HEIGHT      320
#define TFT_FRAMEWDT     10
#define MAX_WIDTH       DSP_WIDTH-TFT_FRAMEWDT*2
#define CLOCK_HEIGHT    100

#if BITRATE_FULL
  #define TITLE_FIX  44
#else
  #define TITLE_FIX   0
#endif
#define bootLogoTop 55

/* SROLLS  */                            /* {{ left, top, fontsize, align }, buffsize, uppercase, width, scrolldelay, scrolldelta, scrolltime } */
const ScrollConfig metaConf       PROGMEM = {{ TFT_FRAMEWDT+84,  6, 26, WA_CENTER }, 140, true,  MAX_WIDTH-84, 5000, 6, 40 };
const ScrollConfig title1Conf     PROGMEM = {{ TFT_FRAMEWDT+84, 40, 24, WA_CENTER }, 140, true,  MAX_WIDTH-84, 5000, 7, 40 };
const ScrollConfig title2Conf     PROGMEM = {{ TFT_FRAMEWDT+84, 71, 24, WA_CENTER }, 140, false, MAX_WIDTH-84, 5000, 7, 40 };
const ScrollConfig playlistConf   PROGMEM = {{ TFT_FRAMEWDT, 146, 24, WA_LEFT }, 140, true, MAX_WIDTH, 1000, 7, 40 };
const ScrollConfig apTitleConf    PROGMEM = {{ TFT_FRAMEWDT, TFT_FRAMEWDT, 4, WA_CENTER }, 140, false, MAX_WIDTH, 0, 7, 40 };
const ScrollConfig apSettConf     PROGMEM = {{ TFT_FRAMEWDT, 320-TFT_FRAMEWDT-16, 2, WA_LEFT }, 140, false, MAX_WIDTH, 0, 5, 40 };
const ScrollConfig weatherConf    PROGMEM = {{ TFT_FRAMEWDT, DSP_HEIGHT - 55, 18, WA_CENTER }, 254, false, MAX_WIDTH, 0, 4, 40 };

/* BACKGROUNDS  */                       /* {{ left, top, fontsize, align }, width, height, outlined } */
const FillConfig   metaBGConf     PROGMEM = {{ 3, 134, 0, WA_CENTER }, DSP_WIDTH - 6, 1, true };
const FillConfig   metaBGConfInv  PROGMEM = {{ 0, 50, 0, WA_LEFT }, DSP_WIDTH, 2, false };
const FillConfig   volbarConf     PROGMEM = {{TFT_FRAMEWDT, DSP_HEIGHT - TFT_FRAMEWDT - 8, 0, WA_LEFT}, MAX_WIDTH, 5, true};
const FillConfig   playlBGConf    PROGMEM = {{ 0, 138, 0, WA_LEFT }, DSP_WIDTH, 36, false };
const FillConfig   heapbarConf    PROGMEM = {{ TFT_FRAMEWDT, DSP_HEIGHT-2, 0, WA_LEFT }, DSP_WIDTH-20, 2, false };

// left,top, width, height, textsize, align, border, radius, fill, paddingX, paddingY
const textBoxConfig bootstrConf   PROGMEM = {60, 255, 360, 35, 20, WA_CENTER, 1, 4, true, 0, 0};
const textBoxConfig ipBoxConf     PROGMEM = {TFT_FRAMEWDT, 288, 150, 25, 18, WA_CENTER, 1, 4, true, 0, 0};
// left, top, width, height, segments, segWidth, segGap, segHeight, iconW, iconH, radius, border
const VolumeWidgetConfig bufferWidgetConf PROGMEM = {266, 288, 100, 25, 20, 3, 1, 6, 12, 12, 4, 1}; // Audio buffer widget
const textBoxConfig rssiBoxConf   PROGMEM = {370, 288, 72, 25, 18, WA_CENTER, 1, 4, true, 0, 0};

/* WIDGETS  */ /* { left, top, fontsize, align } */
const WidgetConfig numConf        PROGMEM = {0, 150, 70, WA_CENTER};
const WidgetConfig apNameConf     PROGMEM = {TFT_FRAMEWDT, 73, 3, WA_CENTER};
const WidgetConfig apName2Conf    PROGMEM = {TFT_FRAMEWDT, 105, 3, WA_CENTER};
const WidgetConfig apPassConf     PROGMEM = {TFT_FRAMEWDT, 173, 3, WA_CENTER};
const WidgetConfig apPass2Conf    PROGMEM = {TFT_FRAMEWDT, 205, 3, WA_CENTER};
const WidgetConfig bootWdtConf    PROGMEM = {0, 220, 1, WA_CENTER};
const WidgetConfig clockConf      PROGMEM = { 280, 157, 70, WA_RIGHT };
const WidgetConfig dateConf       PROGMEM = { TFT_FRAMEWDT, 135, 18, WA_CENTER };

// Speed, width, barwidth
const ProgressConfig bootPrgConf  PROGMEM = {90, 14, 4};

// left, top, width, height, segments, segWidth, segGap, segHeight, iconW, iconH, radius, border
const VolumeWidgetConfig volConf PROGMEM = {165, 288, 96, 25, 21, 2, 1, 8, 10, 14, 4, 1};

// left, top, width, height, image1, image2, image3, image4
const WifiWidgetConfig wifiConf PROGMEM = {445, 288, 30, 25, "/images/wifi_1_30x25.png", "/images/wifi_2_30x25.png", "/images/wifi_3_30x25.png", "/images/wifi_4_30x25.png"};

//left, top, textsize, align, border, radius, fill, paddingX, paddingY, dimension
/* BITRATE */
const BitrateBoxConfig bitrateConf PROGMEM = { (uint16_t)(DSP_WIDTH - TFT_FRAMEWDT - 100), 105, 16, WA_RIGHT, 1, 4, false, 3, 2, 50 };

/* PLAYMODE */
const BitrateBoxConfig pmodeConf   PROGMEM = { TFT_FRAMEWDT, 105, 16, WA_LEFT, 1, 4, false, 3, 2, 50 };

/* STRINGS  */
const char numtxtFmt[]  PROGMEM = "%d";
const char rssiFmt[]    PROGMEM = "WiFi %ddBm";
const char iptxtFmt[]   PROGMEM = "%s";
const char voltxtFmt[]  PROGMEM = "\023\025%d%%";
const char bitrateFmt[] PROGMEM = "%d kBs";

/* MOVES  */
const MoveConfig clockMove PROGMEM = { 280, 157, -1 };

// clang-format on
