#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>
#include "../myoptions.h"
// clang-format off
const char mon[] PROGMEM = "mo";
const char tue[] PROGMEM = "tu";
const char wed[] PROGMEM = "we";
const char thu[] PROGMEM = "th";
const char fri[] PROGMEM = "fr";
const char sat[] PROGMEM = "sa";
const char sun[] PROGMEM = "su";

const char monf[] PROGMEM = "monday";
const char tuef[] PROGMEM = "tuesday";
const char wedf[] PROGMEM = "wednesday";
const char thuf[] PROGMEM = "thursday";
const char frif[] PROGMEM = "friday";
const char satf[] PROGMEM = "saturday";
const char sunf[] PROGMEM = "sunday";

const char jan[] PROGMEM = "january";
const char feb[] PROGMEM = "february";
const char mar[] PROGMEM = "march";
const char apr[] PROGMEM = "april";
const char may[] PROGMEM = "may";
const char jun[] PROGMEM = "june";
const char jul[] PROGMEM = "july";
const char aug[] PROGMEM = "august";
const char sep[] PROGMEM = "september";
const char octt[] PROGMEM = "october";
const char nov[] PROGMEM = "november";
const char decc[] PROGMEM = "december";


const char* const dow[]     PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char* const dowf[]    PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char* const mnths[]   PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octt, nov, decc };
const char* const wind[]    PROGMEM = { wn_N, wn_NNE, wn_NE, wn_ENE, wn_E, wn_ESE, wn_SE, wn_SSE, wn_S, wn_SSW, wn_SW, wn_WSW, wn_W, wn_WNW, wn_NW, wn_NNW, wn_N };

const char    const_PlReady[]    PROGMEM = "[ready]";
const char  const_PlStopped[]    PROGMEM = "[stopped]";
const char  const_PlConnect[]    PROGMEM = "[connecting]";
const char  const_DlgVolume[]    PROGMEM = "VOLUME";
const char    const_DlgLost[]    PROGMEM = "* LOST *";
const char  const_DlgUpdate[]    PROGMEM = "* UPDATING *";
const char const_DlgNextion[]    PROGMEM = "* NEXTION *";
const char const_getWeather[]    PROGMEM = "";
const char  const_waitForSD[]    PROGMEM = "INDEX SD";

const char        apNameTxt[]    PROGMEM = "AP NAME";
const char        apPassTxt[]    PROGMEM = "PASSWORD";
const char       bootstrFmt[]    PROGMEM = "Trying to %s";
const char        apSettFmt[]    PROGMEM = "SETTINGS PAGE ON: HTTP://%s/";
// ============================================================
// WEATHER FORMAT STRINGS
// ============================================================
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · feels like: %.1f°C · pressure: %d hPa · humidity: %d%% · wind: %.1f km/h [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · pressure: %d hPa · humidity: %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric"; /* standard, metric, imperial */
const char weatherLang[]  PROGMEM = "en";

const char prstAssigned[]     PROGMEM = "Assigned";
const char prstDeleted[]      PROGMEM = "Preset deleted";
const char prstNoUrl[]        PROGMEM = "No URL";
const char prstEmptyPreset[]  PROGMEM = "Empty preset";
const char prstPlay[] PROGMEM = "Play";
const char prstSave[] PROGMEM = "Save";
const char prstDel[]  PROGMEM = "Delete";
const char prstSpace[]  PROGMEM = "Space";
const char prstCancel[] PROGMEM = "Cancel";
const char prstOk[]     PROGMEM = "OK";

#endif
