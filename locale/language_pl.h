#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>
#include "../myoptions.h"
// clang-format off

const char mon[] PROGMEM = "pon";
const char tue[] PROGMEM = "wto";
const char wed[] PROGMEM = "śro";
const char thu[] PROGMEM = "czw";
const char fri[] PROGMEM = "pią";
const char sat[] PROGMEM = "sob";
const char sun[] PROGMEM = "nie";

const char monf[] PROGMEM = "poniedziałek";
const char tuef[] PROGMEM = "wtorek";
const char wedf[] PROGMEM = "środa";
const char thuf[] PROGMEM = "czwartek";
const char frif[] PROGMEM = "piątek";
const char satf[] PROGMEM = "sobota";
const char sunf[] PROGMEM = "niedziela";

const char jan[] PROGMEM = "stycznia";
const char feb[] PROGMEM = "lutego";
const char mar[] PROGMEM = "marca";
const char apr[] PROGMEM = "kwietnia";
const char may[] PROGMEM = "maja";
const char jun[] PROGMEM = "czerwca";
const char jul[] PROGMEM = "lipca";
const char aug[] PROGMEM = "sierpnia";
const char sep[] PROGMEM = "września";
const char octt[] PROGMEM = "października";
const char nov[] PROGMEM = "listopada";
const char decc[] PROGMEM = "grudnia";
/*
const char jan[] PROGMEM = "01";
const char feb[] PROGMEM = "02";
const char mar[] PROGMEM = "03";
const char apr[] PROGMEM = "04";
const char may[] PROGMEM = "05";
const char jun[] PROGMEM = "06";
const char jul[] PROGMEM = "07";
const char aug[] PROGMEM = "08";
const char sep[] PROGMEM = "09";
const char octt[] PROGMEM = "10";
const char nov[] PROGMEM = "11";
const char decc[] PROGMEM = "12";
*/

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
const char wn_N_l[]   PROGMEM = "północny";
const char wn_NNE_l[] PROGMEM = "północno-północno-wschodni";
const char wn_NE_l[]  PROGMEM = "północno-wschodni";
const char wn_ENE_l[] PROGMEM = "wschodnio-północno-wschodni";
const char wn_E_l[]   PROGMEM = "wschodni";
const char wn_ESE_l[] PROGMEM = "wschodnio-południowo-wschodni";
const char wn_SE_l[]  PROGMEM = "południowo-wschodni";
const char wn_SSE_l[] PROGMEM = "południowo-południowo-wschodni";
const char wn_S_l[]   PROGMEM = "południowy";
const char wn_SSW_l[] PROGMEM = "południowo-południowo-zachodni";
const char wn_SW_l[]  PROGMEM = "południowo-zachodni";
const char wn_WSW_l[] PROGMEM = "zachodnio-południowo-zachodni";
const char wn_W_l[]   PROGMEM = "zachodni";
const char wn_WNW_l[] PROGMEM = "zachodnio-północno-zachodni";
const char wn_NW_l[]  PROGMEM = "północno-zachodni";
const char wn_NNW_l[] PROGMEM = "północno-północno-zachodni";

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
const char       bootstrFmt[]    PROGMEM = "Connecting %s";
const char        apSettFmt[]    PROGMEM = "SETTINGS PAGE ON: HTTP://%s/";
// ============================================================
// WEATHER FORMAT STRINGS
// ============================================================
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · odczuwalna: %.1f°C · ciśnienie: %d hPa · wilgotność: %d%% · wiatr: %.1f km/h [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · %d hPa · %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric"; /* standard, metric, imperial */
const char weatherLang[]  PROGMEM = "pl";

const char prstAssigned[]     PROGMEM = "Przypisano";
const char prstDeleted[]      PROGMEM = "Preset usunięty";
const char prstNoUrl[]        PROGMEM = "Brak URL";
const char prstEmptyPreset[]  PROGMEM = "Pusty preset";
const char prstPlay[]         PROGMEM = "Odtwórz";
const char prstSave[]         PROGMEM = "Zapisz";
const char prstDel[]          PROGMEM = "Usuń";
const char prstSpace[]        PROGMEM = "Spacja";
const char prstCancel[]       PROGMEM = "Anuluj";
const char prstOk[]           PROGMEM = "OK";

#endif
