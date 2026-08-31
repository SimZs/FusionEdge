#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>
#include "../src/core/config.h"
#include "../myoptions.h"
// clang-format off
const char mon[] PROGMEM = "lu";
const char tue[] PROGMEM = "ma";
const char wed[] PROGMEM = "mi";
const char thu[] PROGMEM = "ju";
const char fri[] PROGMEM = "vi";
const char sat[] PROGMEM = "sa";
const char sun[] PROGMEM = "do";

const char monf[] PROGMEM = "lunes";
const char tuef[] PROGMEM = "martes";
const char wedf[] PROGMEM = "miercoles";
const char thuf[] PROGMEM = "jueves";
const char frif[] PROGMEM = "viernes";
const char satf[] PROGMEM = "sabado";
const char sunf[] PROGMEM = "domingo";

const char jan[]  PROGMEM = "enero";
const char feb[]  PROGMEM = "febrero";
const char mar[]  PROGMEM = "marzo";
const char apr[]  PROGMEM = "abril";
const char may[]  PROGMEM = "mayo";
const char jun[]  PROGMEM = "junio";
const char jul[]  PROGMEM = "julio";
const char aug[]  PROGMEM = "agosto";
const char sep[]  PROGMEM = "septiembre";
const char octc[] PROGMEM = "octubre";
const char nov[]  PROGMEM = "noviembre";
const char decc[] PROGMEM = "diciembre";

// WIND DIRECTIONS – SHORT
const char wn_N_s[]   PROGMEM = "N";
const char wn_NNE_s[] PROGMEM = "NNE";
const char wn_NE_s[]  PROGMEM = "NE";
const char wn_ENE_s[] PROGMEM = "ENE";
const char wn_E_s[]   PROGMEM = "E";
const char wn_ESE_s[] PROGMEM = "ESE";
const char wn_SE_s[]  PROGMEM = "SE";
const char wn_SSE_s[] PROGMEM = "SSE";
const char wn_S_s[]   PROGMEM = "S";
const char wn_SSW_s[] PROGMEM = "SSO";
const char wn_SW_s[]  PROGMEM = "SO";
const char wn_WSW_s[] PROGMEM = "OSO";
const char wn_W_s[]   PROGMEM = "O";
const char wn_WNW_s[] PROGMEM = "ONO";
const char wn_NW_s[]  PROGMEM = "NO";
const char wn_NNW_s[] PROGMEM = "NNO";

const char *const wind_short[] PROGMEM = {
  wn_N_s, wn_NNE_s, wn_NE_s, wn_ENE_s,
  wn_E_s, wn_ESE_s, wn_SE_s, wn_SSE_s,
  wn_S_s, wn_SSW_s, wn_SW_s, wn_WSW_s,
  wn_W_s, wn_WNW_s, wn_NW_s, wn_NNW_s, wn_N_s
};

// WIND DIRECTIONS – LONG
const char wn_N_l[]   PROGMEM = "norte";
const char wn_NNE_l[] PROGMEM = "norte-noreste";
const char wn_NE_l[]  PROGMEM = "noreste";
const char wn_ENE_l[] PROGMEM = "este-noreste";
const char wn_E_l[]   PROGMEM = "este";
const char wn_ESE_l[] PROGMEM = "este-sureste";
const char wn_SE_l[]  PROGMEM = "sureste";
const char wn_SSE_l[] PROGMEM = "sur-sureste";
const char wn_S_l[]   PROGMEM = "sur";
const char wn_SSW_l[] PROGMEM = "sur-suroeste";
const char wn_SW_l[]  PROGMEM = "suroeste";
const char wn_WSW_l[] PROGMEM = "oeste-suroeste";
const char wn_W_l[]   PROGMEM = "oeste";
const char wn_WNW_l[] PROGMEM = "oeste-noroeste";
const char wn_NW_l[]  PROGMEM = "noroeste";
const char wn_NNW_l[] PROGMEM = "norte-noroeste";

const char *const wind_long[] PROGMEM = {
  wn_N_l, wn_NNE_l, wn_NE_l, wn_ENE_l,
  wn_E_l, wn_ESE_l, wn_SE_l, wn_SSE_l,
  wn_S_l, wn_SSW_l, wn_SW_l, wn_WSW_l,
  wn_W_l, wn_WNW_l, wn_NW_l, wn_NNW_l, wn_N_l
};

static inline const char *const *getWindTable() {
  return config.store.shortWeather ? wind_short : wind_long;
}

const char *const dow[]  PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char *const dowf[] PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char *const mnths[] PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octc, nov, decc };

const char const_PlReady[]   PROGMEM = "[listo]";
const char const_PlStopped[] PROGMEM = "[detenido]";
const char const_PlConnect[] PROGMEM = "";
const char const_DlgVolume[] PROGMEM = "VOLUMEN";
const char const_DlgLost[]   PROGMEM = "* PERDIDO *";
const char const_DlgUpdate[] PROGMEM = "* ACTUALIZANDO *";
const char const_DlgNextion[] PROGMEM = "* NEXTION *";
const char const_getWeather[] PROGMEM = "";
const char const_waitForSD[] PROGMEM = "ÍNDICE SD";

const char apNameTxt[]  PROGMEM = "NOMBRE AP";
const char apPassTxt[]  PROGMEM = "CONTRASEÑA";
const char bootstrFmt[] PROGMEM = "CONECTANDO CON %s";
const char apSettFmt[]  PROGMEM = "PAGINA DE CONFIGURACION: HTTP://%s/";

// WEATHER FORMAT STRINGS
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · sensación térmica: %.1f°C · presión: %d hPa · humedad: %d%% · viento: %.1f km/h [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · %d hPa · %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric";
const char weatherLang[]  PROGMEM = "es";

// ---- Presets screen ----
const char prstAssigned[]    PROGMEM = "Asignado";
const char prstDeleted[]     PROGMEM = "Preset borrado";
const char prstNoUrl[]       PROGMEM = "Sin URL";
const char prstEmptyPreset[] PROGMEM = "Preset vacío";
const char prstPlay[]        PROGMEM = "Reproducir";
const char prstSave[]        PROGMEM = "Guardar";
const char prstDel[]         PROGMEM = "Borrar";
const char prstSpace[]       PROGMEM = "Espacio";
const char prstCancel[]      PROGMEM = "Cancelar";
const char prstOk[]          PROGMEM = "OK";

#endif
