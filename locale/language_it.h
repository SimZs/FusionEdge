#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>
#include "../myoptions.h"
// clang-format off

const char mon[] PROGMEM = "Lu";
const char tue[] PROGMEM = "Ma";
const char wed[] PROGMEM = "Me";
const char thu[] PROGMEM = "Gi";
const char fri[] PROGMEM = "Ve";
const char sat[] PROGMEM = "Sa";
const char sun[] PROGMEM = "Do";

const char monf[] PROGMEM = "Lunedi";
const char tuef[] PROGMEM = "Martedi";
const char wedf[] PROGMEM = "Mercoledi";
const char thuf[] PROGMEM = "Giovedi";
const char frif[] PROGMEM = "Venerdi";
const char satf[] PROGMEM = "Sabato";
const char sunf[] PROGMEM = "Domenica";

const char jan[] PROGMEM = "Gennaio";
const char feb[] PROGMEM = "Febbraio";
const char mar[] PROGMEM = "Maezo";
const char apr[] PROGMEM = "Aprile";
const char may[] PROGMEM = "Maggio";
const char jun[] PROGMEM = "Giugno";
const char jul[] PROGMEM = "Luglio";
const char aug[] PROGMEM = "Agosto";
const char sep[] PROGMEM = "Settembre";
const char octc[] PROGMEM = "Ottobre";
const char nov[] PROGMEM = "Novembre";
const char decc[] PROGMEM = "Dicembre";


const char* const dow[]     PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char* const dowf[]    PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char* const mnths[]   PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octc, nov, decc };
const char* const wind[]    PROGMEM = { wn_N, wn_NNE, wn_NE, wn_ENE, wn_E, wn_ESE, wn_SE, wn_SSE, wn_S, wn_SSW, wn_SW, wn_WSW, wn_W, wn_WNW, wn_NW, wn_NNW, wn_N };

const char    const_PlReady[]    PROGMEM = "[Pronto]";
const char  const_PlStopped[]    PROGMEM = "[Stop]";
const char  const_PlConnect[]    PROGMEM = "[Connessione]";
const char  const_DlgVolume[]    PROGMEM = "VOLUME";
const char    const_DlgLost[]    PROGMEM = "* PERSO *";
const char  const_DlgUpdate[]    PROGMEM = "* AGGIORNAMENTO *";
const char const_DlgNextion[]    PROGMEM = "* NEXTION *";
const char const_getWeather[]    PROGMEM = "";
const char  const_waitForSD[]    PROGMEM = "INDICE SD";

const char        apNameTxt[]    PROGMEM = "NOME AP";
const char        apPassTxt[]    PROGMEM = "PASSWORD";
const char       bootstrFmt[]    PROGMEM = "Connessione a %s";
const char        apSettFmt[]    PROGMEM = "PAGINA IMPOSTAZIONI SU: HTTP://%s/";

// ============================================================
// WEATHER FORMAT STRINGS
// ============================================================
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · PERCEPITA: %.1f°C · PRESSIONE: %d hPa · UMIDITA: %d%% · VENTO: %.1f km/h [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · PRESSIONE: %d hPa · UMIDITA: %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric"; /* standard, metric, imperial */
const char weatherLang[]  PROGMEM = "it";

const char prstAssigned[]     PROGMEM = "Assegnato";
const char prstDeleted[]      PROGMEM = "Preset eliminato";
const char prstNoUrl[]        PROGMEM = "Nessun URL";
const char prstEmptyPreset[]  PROGMEM = "Preset vuoto";
const char prstPlay[]         PROGMEM = "Riproduci";
const char prstSave[]         PROGMEM = "Salva";
const char prstDel[]          PROGMEM = "Elimina";
const char prstSpace[]        PROGMEM = "Spazio";
const char prstCancel[]       PROGMEM = "Annulla";
const char prstOk[]           PROGMEM = "OK";
// clang-format on
#endif
