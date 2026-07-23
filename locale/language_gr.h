#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>
#include "../myoptions.h"
// clang-format off

const char mon[] PROGMEM = "Δε";
const char tue[] PROGMEM = "Τρ";
const char wed[] PROGMEM = "Τε";
const char thu[] PROGMEM = "Πε";
const char fri[] PROGMEM = "Πα";
const char sat[] PROGMEM = "Σα";
const char sun[] PROGMEM = "Κυ";

const char monf[] PROGMEM = "Δευτέρα";
const char tuef[] PROGMEM = "Τρίτη";
const char wedf[] PROGMEM = "Τετάρτη";
const char thuf[] PROGMEM = "Πέμπτη";
const char frif[] PROGMEM = "Παρασκευή";
const char satf[] PROGMEM = "Σάββατο";
const char sunf[] PROGMEM = "Κυριακή";

const char jan[] PROGMEM = "Ιανουάριος";
const char feb[] PROGMEM = "Φεβρουάριος";
const char mar[] PROGMEM = "Μάρτιος";
const char apr[] PROGMEM = "Απρίλιος";
const char may[] PROGMEM = "Μάιος";
const char jun[] PROGMEM = "Ιούνιος";
const char jul[] PROGMEM = "Ιούλιος";
const char aug[] PROGMEM = "Αύγουστος";
const char sep[] PROGMEM = "Σεπτέμβριος";
const char octt[] PROGMEM = "Οκτώβριος";
const char nov[] PROGMEM = "Νοέμβριος";
const char decc[] PROGMEM = "Δεκέμβριος";


const char* const dow[]     PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char* const dowf[]    PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char* const mnths[]   PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octt, nov, decc };
const char* const wind[]    PROGMEM = { wn_N, wn_NNE, wn_NE, wn_ENE, wn_E, wn_ESE, wn_SE, wn_SSE, wn_S, wn_SSW, wn_SW, wn_WSW, wn_W, wn_WNW, wn_NW, wn_NNW, wn_N };

const char    const_PlReady[]    PROGMEM = "[έτοιμο]";
const char  const_PlStopped[]    PROGMEM = "[σταμάτησε]";
const char  const_PlConnect[]    PROGMEM = "[σύνδεση]";
const char  const_DlgVolume[]    PROGMEM = "ΕΝΤΑΣΗ";
const char    const_DlgLost[]    PROGMEM = "* χάθηκε το σήμα *";
const char  const_DlgUpdate[]    PROGMEM = "* ΕΝΗΜΕΡΩΣΗ *";
const char const_DlgNextion[]    PROGMEM = "* NEXTION *";
const char const_getWeather[]    PROGMEM = "";
const char  const_waitForSD[]    PROGMEM = "INDEX SD";

const char        apNameTxt[]    PROGMEM = "ΟΝΟΜΑ AP";
const char        apPassTxt[]    PROGMEM = "ΚΩΔΙΚΟΣ";
const char       bootstrFmt[]    PROGMEM = "σύνδεση με %s";
const char        apSettFmt[]    PROGMEM = "ΣΕΛΙΔΑ ΡΥΘΜΙΣΕΩΝ: HTTP://%s/";
// ============================================================
// WEATHER FORMAT STRINGS
// ============================================================
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · αίσθηση θερμοκρασίας: %.1f°C · πίεση: %d hPa · υγρασία: %d%% · άνεμος: %.1f km/h [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · Πίεση: %d hPa · Υγρασία: %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric"; /* standard, metric, imperial */
const char weatherLang[]  PROGMEM = "gr";

const char prstAssigned[]     PROGMEM = "Ανατέθηκε";
const char prstDeleted[]      PROGMEM = "Το preset διαγράφηκε";
const char prstNoUrl[]        PROGMEM = "Χωρίς URL";
const char prstEmptyPreset[]  PROGMEM = "Κενό preset";
const char prstPlay[]         PROGMEM = "Αναπαραγωγή";
const char prstSave[]         PROGMEM = "Αποθήκευση";
const char prstDel[]          PROGMEM = "Διαγραφή";
const char prstSpace[]        PROGMEM = "Κενό";
const char prstCancel[]       PROGMEM = "Ακύρωση";
const char prstOk[]           PROGMEM = "OK";

#endif
