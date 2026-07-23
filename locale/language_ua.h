#ifndef dsp_full_loc
#define dsp_full_loc
#include <pgmspace.h>
#include "../myoptions.h"
// clang-format off
const char mon[] PROGMEM = "пн";
const char tue[] PROGMEM = "вт";
const char wed[] PROGMEM = "ср";
const char thu[] PROGMEM = "чт";
const char fri[] PROGMEM = "пт";
const char sat[] PROGMEM = "сб";
const char sun[] PROGMEM = "нд";

const char monf[] PROGMEM = "понеділок";
const char tuef[] PROGMEM = "вівторок";
const char wedf[] PROGMEM = "середа";
const char thuf[] PROGMEM = "четвер";
const char frif[] PROGMEM = "п'ятниця";
const char satf[] PROGMEM = "субота";
const char sunf[] PROGMEM = "неділя";

const char jan[] PROGMEM = "січня";
const char feb[] PROGMEM = "лютого";
const char mar[] PROGMEM = "березня";
const char apr[] PROGMEM = "квітня";
const char may[] PROGMEM = "травня";
const char jun[] PROGMEM = "червня";
const char jul[] PROGMEM = "липня";
const char aug[] PROGMEM = "серпня";
const char sep[] PROGMEM = "вересня";
const char octt[] PROGMEM = "жовтня";
const char nov[] PROGMEM = "листопада";
const char decc[] PROGMEM = "грудня";


const char* const dow[]     PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char* const dowf[]    PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char* const mnths[]   PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octt, nov, decc };
const char* const wind[]    PROGMEM = { wn_N, wn_NNE, wn_NE, wn_ENE, wn_E, wn_ESE, wn_SE, wn_SSE, wn_S, wn_SSW, wn_SW, wn_WSW, wn_W, wn_WNW, wn_NW, wn_NNW, wn_N };

const char    const_PlReady[]    PROGMEM = "[готовий]";
const char  const_PlStopped[]    PROGMEM = "[зупинено]";
const char  const_PlConnect[]    PROGMEM = "[з'єднання]";
const char  const_DlgVolume[]    PROGMEM = "ГУЧНІСТЬ";
const char    const_DlgLost[]    PROGMEM = "ВИМКНЕНО";
const char  const_DlgUpdate[]    PROGMEM = "ОНОВЛЕННЯ";
const char const_DlgNextion[]    PROGMEM = "NEXTION";
const char const_getWeather[]    PROGMEM = "";
const char  const_waitForSD[]    PROGMEM = "ІНДЕКС SD";

const char        apNameTxt[]    PROGMEM = "ТОЧКА ДОСТУПУ";
const char        apPassTxt[]    PROGMEM = "ГАСЛО";
const char       bootstrFmt[]    PROGMEM = "З'єднуюсь з %s";
const char        apSettFmt[]    PROGMEM = "НАЛАШТУВАННЯ: HTTP://%s/";
// ============================================================
// WEATHER FORMAT STRINGS
// ============================================================
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · відчувається: %.1f°C · тиск: %d гПа · вологість: %d%% · вітер: %.1f км/год [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · тиск: %d hPa · вологість: %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric"; /* standard, metric, imperial */
const char weatherLang[]  PROGMEM = "ua";

const char prstAssigned[]     PROGMEM = "Призначено";
const char prstDeleted[]      PROGMEM = "Пресет видалено";
const char prstNoUrl[]        PROGMEM = "Немає URL";
const char prstEmptyPreset[]  PROGMEM = "Порожній пресет";
const char prstPlay[]         PROGMEM = "Відтворити";
const char prstSave[]         PROGMEM = "Зберегти";
const char prstDel[]          PROGMEM = "Видалити";
const char prstSpace[]        PROGMEM = "Пробіл";
const char prstCancel[]       PROGMEM = "Скасувати";
const char prstOk[]           PROGMEM = "OK";

#endif
