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
const char sun[] PROGMEM = "вс";

const char monf[] PROGMEM = "понедельник";
const char tuef[] PROGMEM = "вторник";
const char wedf[] PROGMEM = "среда";
const char thuf[] PROGMEM = "четверг";
const char frif[] PROGMEM = "пятница";
const char satf[] PROGMEM = "суббота";
const char sunf[] PROGMEM = "воскресенье";

const char jan[] PROGMEM = "января";
const char feb[] PROGMEM = "февраля";
const char mar[] PROGMEM = "марта";
const char apr[] PROGMEM = "апреля";
const char may[] PROGMEM = "мая";
const char jun[] PROGMEM = "июня";
const char jul[] PROGMEM = "июля";
const char aug[] PROGMEM = "августа";
const char sep[] PROGMEM = "сентября";
const char octt[] PROGMEM = "октября";
const char nov[] PROGMEM = "ноября";
const char decc[] PROGMEM = "декабря";


const char* const dow[]     PROGMEM = { sun, mon, tue, wed, thu, fri, sat };
const char* const dowf[]    PROGMEM = { sunf, monf, tuef, wedf, thuf, frif, satf };
const char* const mnths[]   PROGMEM = { jan, feb, mar, apr, may, jun, jul, aug, sep, octt, nov, decc };
const char* const wind[]    PROGMEM = { wn_N, wn_NNE, wn_NE, wn_ENE, wn_E, wn_ESE, wn_SE, wn_SSE, wn_S, wn_SSW, wn_SW, wn_WSW, wn_W, wn_WNW, wn_NW, wn_NNW, wn_N };

const char    const_PlReady[]    PROGMEM = "[готов]";
const char  const_PlStopped[]    PROGMEM = "[остановлено]";
const char  const_PlConnect[]    PROGMEM = "[соединение]";
const char  const_DlgVolume[]    PROGMEM = "ГРОМКОСТЬ";
const char    const_DlgLost[]    PROGMEM = "ОТКЛЮЧЕНО";
const char  const_DlgUpdate[]    PROGMEM = "ОБНОВЛЕНИЕ";
const char const_DlgNextion[]    PROGMEM = "NEXTION";
const char const_getWeather[]    PROGMEM = "";
const char  const_waitForSD[]    PROGMEM = "ИНДЕКС SD";

const char        apNameTxt[]    PROGMEM = "ТОЧКА ДОСТУПА";
const char        apPassTxt[]    PROGMEM = "ПАРОЛЬ";
const char       bootstrFmt[]    PROGMEM = "Соединяюсь с %s";
const char        apSettFmt[]    PROGMEM = "НАСТРОЙКИ: HTTP://%s/";
// ============================================================
// WEATHER FORMAT STRINGS
// ============================================================
const char weatherFmtShort[] PROGMEM =
  "%d hPa · %d%% RH · %.1f km/h [%s]";

#if EXT_WEATHER
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · ощущается: %.1f°C · давление: %d гПа · влажность: %d%% · ветер: %.1f км/год [%s]";
#else
const char weatherFmtLong[] PROGMEM =
  "%s, %.1f°C · давление: %d гПа · влажность: %d%%";
#endif

static inline const char* getWeatherFmt() {
  return config.store.shortWeather ? weatherFmtShort : weatherFmtLong;
}

const char weatherUnits[] PROGMEM = "metric"; /* standard, metric, imperial */
const char weatherLang[]  PROGMEM = "ru";

const char prstAssigned[]     PROGMEM = "Назначено";
const char prstDeleted[]      PROGMEM = "Пресет удалён";
const char prstNoUrl[]        PROGMEM = "Нет URL";
const char prstEmptyPreset[]  PROGMEM = "Пустой пресет";
const char prstPlay[]         PROGMEM = "Воспроизв.";
const char prstSave[]         PROGMEM = "Сохранить";
const char prstDel[]          PROGMEM = "Удалить";
const char prstSpace[]        PROGMEM = "Пробел";
const char prstCancel[]       PROGMEM = "Отмена";
const char prstOk[]           PROGMEM = "OK";

#endif
