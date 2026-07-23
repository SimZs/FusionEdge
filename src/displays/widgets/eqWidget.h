#pragma once
#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY

#include <LovyanGFX.hpp>
#include "widgetsconfig.h"
#include "../display_select.h"
#include "widget.h"

// ── EqWidget ──────────────────────────────────────────────────────────────────
// Kis EQ ikon gomb (egy StatusWidget dot mérete: 50×26px).
// Megnyomásra aktívra vált (kitöltött), és az EQ overlay megjelenik a
// VOL_AREA sávban (top=137, height=149). Újbóli megnyomásra visszazár.
// Az overlay-t a Display kezeli (_eqInlineShow / _eqInlineHide).

class EqWidget : public Widget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t bgcolor);

    // Állapot lekérdezése (touchscreen.cpp használja)
    bool isEqActive() const { return _eqActive; }

    // Toggle – visszaadja az új állapotot
    bool toggle();

    // Kényszerzárás (pl. screensaver)
    void forceClose();

  protected:
    void _draw()  override;
    void _clear() override;
    void _reset() override {}

  private:
    bool     _eqActive = false;
    uint16_t _bgcolor  = 0;

    void _drawIcon(bool active);
};

#endif // DSP_MODEL != DSP_DUMMY
