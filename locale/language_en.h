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
// ============================================================
// WIND DIRECTIONS - SHORT
// ============================================================
const char wn_N_s[]   PROGMEM = "N";
const char wn_NNE_s[] PROGMEM = "NNE";
const char wn_NE_s[]  PROGMEM = "NE";
const char wn_ENE_s[] PROGMEM = "ENE";
const char wn_E_s[]   PROGMEM = "E";
const char wn_ESE_s[] PROGMEM = "ESE";
const char wn_SE_s[]  PROGMEM = "SE";
const char wn_SSE_s[] PROGMEM = "SSE";
const char wn_S_s[]   PROGMEM = "S";
const char wn_SSW_s[] PROGMEM = "SSW";
const char wn_SW_s[]  PROGMEM = "SW";
const char wn_WSW_s[] PROGMEM = "WSW";
const char wn_W_s[]   PROGMEM = "W";
const char wn_WNW_s[] PROGMEM = "WNW";
const char wn_NW_s[]  PROGMEM = "NW";
const char wn_NNW_s[] PROGMEM = "NNW";

// ============================================================
// WIND DIRECTIONS - LONG
// ============================================================
const char wn_N_l[]   PROGMEM = "north";
const char wn_NNE_l[] PROGMEM = "north-northeast";
const char wn_NE_l[]  PROGMEM = "northeast";
const char wn_ENE_l[] PROGMEM = "east-northeast";
const char wn_E_l[]   PROGMEM = "east";
const char wn_ESE_l[] PROGMEM = "east-southeast";
const char wn_SE_l[]  PROGMEM = "southeast";
const char wn_SSE_l[] PROGMEM = "south-southeast";
const char wn_S_l[]   PROGMEM = "south";
const char wn_SSW_l[] PROGMEM = "south-southwest";
const char wn_SW_l[]  PROGMEM = "southwest";
const char wn_WSW_l[] PROGMEM = "west-southwest";
const char wn_W_l[]   PROGMEM = "west";
const char wn_WNW_l[] PROGMEM = "west-northwest";
const char wn_NW_l[]  PROGMEM = "northwest";
const char wn_NNW_l[] PROGMEM = "north-northwest";

const char *const wind_short[] PROGMEM = {
  wn_N_s, wn_NNE_s, wn_NE_s, wn_ENE_s,
  wn_E_s, wn_ESE_s, wn_SE_s, wn_SSE_s,
  wn_S_s, wn_SSW_s, wn_SW_s, wn_WSW_s,
  wn_W_s, wn_WNW_s, wn_NW_s, wn_NNW_s, wn_N_s
};

const char *const wind_long[] PROGMEM = {
  wn_N_l, wn_NNE_l, wn_NE_l, wn_ENE_l,
  wn_E_l, wn_ESE_l, wn_SE_l, wn_SSE_l,
  wn_S_l, wn_SSW_l, wn_SW_l, wn_WSW_l,
  wn_W_l, wn_WNW_l, wn_NW_l, wn_NNW_l, wn_N_l
};

static inline const char *const *getWindTable() {
  return config.store.shortWeather ? wind_short : wind_long;
}

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
