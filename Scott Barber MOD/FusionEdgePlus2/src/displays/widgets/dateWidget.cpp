#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY

#include "../../core/config.h"
#ifdef NAMEDAYS_FILE
#include "../../core/namedays.h"
#endif
#include "../tools/language.h"
#include "dateWidget.h"

// ── Belső segédfüggvény: dátum formázás ────────────────────────────────────
// A ClockWidget::_formatDate() logikáját tükrözi, de kimenet char*-ba megy.
static void _formatDate(char* out, size_t outlen, const tm& t) {
#if defined(DSP_OLED) && (DSP_MODEL == DSP_SSD1322)
    switch (config.store.dateFormat) {
        case 0:  snprintf(out, outlen, "%04d.%02d.%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday); break;
        case 1:  snprintf(out, outlen, "%02d/%02d/%04d", t.tm_mon+1, t.tm_mday, t.tm_year+1900); break;
        case 2:  snprintf(out, outlen, "%02d-%02d-%04d", t.tm_mday, t.tm_mon+1, t.tm_year+1900); break;
        case 3:  snprintf(out, outlen, "%02d.%02d.%04d", t.tm_mday, t.tm_mon+1, t.tm_year+1900); break;
        case 4:  snprintf(out, outlen, "%02d/%02d/%04d", t.tm_mday, t.tm_mon+1, t.tm_year+1900); break;
        default: snprintf(out, outlen, "%04d-%02d-%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday); break;
    }
#else
    switch (config.store.dateFormat) {
        case 0:  snprintf(out, outlen, "%d. %s %2d. %s",   t.tm_year+1900, LANG::mnths[t.tm_mon], t.tm_mday, LANG::dowf[t.tm_wday]); break;
        case 1:  snprintf(out, outlen, "%2d %s %d",         t.tm_mday, LANG::mnths[t.tm_mon], t.tm_year+1900); break;
        case 2:  snprintf(out, outlen, "%s %2d %s %d",      LANG::dowf[t.tm_wday], t.tm_mday, LANG::mnths[t.tm_mon], t.tm_year+1900); break;
        case 3:  snprintf(out, outlen, "%s - %02d. %s. %04d", LANG::dowf[t.tm_wday], t.tm_mday, LANG::mnths[t.tm_mon], t.tm_year+1900); break;
        default: snprintf(out, outlen, "%s - %02d. %s. %d",   LANG::dowf[t.tm_wday], t.tm_mday, LANG::mnths[t.tm_mon], t.tm_year+1900); break;
    }
#endif
}

// ── DateWidget::update() ────────────────────────────────────────────────────

void DateWidget::update() {
    if (!_active) return;

    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    // Percenként frissítünk, de azonnal ha dateFormat vagy nameday kapcsoló változott
    static int8_t  _lastMinute = -1;
    static uint8_t _lastDateFormat = 0xFF;
    static bool    _lastNameday = false;
    const uint8_t  curFmt = config.store.dateFormat;
    const bool     curNd  = config.store.nameday;
    if (ti.tm_min == _lastMinute && curFmt == _lastDateFormat && curNd == _lastNameday) return;
    _lastMinute     = ti.tm_min;
    _lastDateFormat = curFmt;
    _lastNameday    = curNd;

    char dateBuf[64];
    _formatDate(dateBuf, sizeof(dateBuf), ti);

    // Dátum + névnap összefűzése
    char line[192];
    strlcpy(line, dateBuf, sizeof(line));

#ifdef NAMEDAYS_FILE
    if (_namedayEnabled && config.store.nameday) {
        char nd[128] = {0};
        if (namedays_get_str((uint8_t)ti.tm_mon + 1, (uint8_t)ti.tm_mday, nd, sizeof(nd)) && nd[0]) {
            strlcat(line, "  |  ", sizeof(line));
            strlcat(line, nd, sizeof(line));
        }
    }
#endif

    // A ScrollWidget belső változásdetektálása gondoskodik arról,
    // hogy csak akkor rajzol újra, ha a szöveg ténylegesen megváltozott.
    setText(line);
}

#endif // DSP_MODEL != DSP_DUMMY
