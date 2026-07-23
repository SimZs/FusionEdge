#pragma once

#include "../../core/options.h"
#include <LovyanGFX.hpp>
#if DSP_MODEL != DSP_DUMMY
#include "widgetsconfig.h"
#include "scrollWidget.h"

// ── DateWidget ──────────────────────────────────────────────────────────────
// Önálló widget: formázott dátum + névnap összefűzve egy ScrollWidget-ben.
// A Fusion DateWidget logikáját követi: a névnap nem külön widget,
// hanem a dátum szövegébe van fűzve ("  |  " elválasztóval).
// Ha nincs névnap (NAMEDAYS_FILE nincs definiálva, vagy a nap névtelen),
// csak a dátum jelenik meg.
//
// Pozíciója és mérete a ScrollConfig-on keresztül állítható (conf_480x320.h).
// update() hívásával frissíti a tartalmat (tipikusan percenként elegendő,
// de biztonságos sűrűbben is hívni – belső változásdetektálás van).
class DateWidget : public ScrollWidget {
public:
    using ScrollWidget::init;

    // Inicializálás: a ScrollConfig adja a pozíciót/méretet/scroll-paramétert.
    // A separator ("\007") a ScrollWidget belső reset-karaktere.
    void init(ScrollConfig conf, uint16_t fg, uint16_t bg) {
        ScrollWidget::init("\007", conf, fg, bg);
    }

    // Engedélyezi/letiltja a névnap megjelenítését.
    // Alapértelmezett: true (engedélyezett, ha NAMEDAYS_FILE definiált).
    void setNamedayEnabled(bool en) { _namedayEnabled = en; }

    // Frissíti a szöveget és kirajzolja a widgetet.
    // force=true: újrarajzol akkor is ha a szöveg nem változott.
    void draw(bool force = false) { update(); loop(); }

    // Frissíti a widget szövegét az aktuális dátum (és névnap) alapján.
    void update();

private:
    bool _namedayEnabled = true;
};

#endif // DSP_MODEL != DSP_DUMMY
