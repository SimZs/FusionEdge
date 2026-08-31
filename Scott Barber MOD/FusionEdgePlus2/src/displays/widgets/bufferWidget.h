#pragma once

#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY
#include <LovyanGFX.hpp>
#include "widgetsconfig.h"
#include "../display_select.h"
#include "widget.h"

// ── BufferWidget ──────────────────────────────────────────────────────────
// Audio buffer töltöttség widget. A VolumeWidget mintájára épül:
// szegmenses megjelenítés ikonnal, de csak egy aktív szín (nem 3 fokozat).
// Ikon: hengerstack (cylinder stack) – buffer/storage asszociáció.
// Értéke: 0..segments (setValue arányosítva hívandó kívülről).
class BufferWidget : public Widget {
  public:
    BufferWidget(yoDisplay* dsp, const VolumeWidgetConfig& conf);

    // val: aktuális töltöttség bájtban, maxVal: maximális puffer méret
    void setValue(uint32_t val, uint32_t maxVal);
    void clear(); // Fizikailag törli a widget területét a háttérszínnel

  protected:
    void _draw() override;

  private:
    void _drawIcon(int startX, int startY, int areaH);
    void _drawSegments(int startX, int startY, int areaH);

    yoDisplay*  _dsp;
    LGFX_Sprite _spr;

    VolumeWidgetConfig _conf;
    int8_t _filled; // 0.._conf.segments
};

#endif // DSP_MODEL != DSP_DUMMY
