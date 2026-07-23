#pragma once

#include "../../core/options.h"
#include <LovyanGFX.hpp>
#if DSP_MODEL != DSP_DUMMY
#    include "widgetsconfig.h"
#    include "../display_select.h"
#    include "textWidget.h"

// ── PlayModeStationWidget ──────────────────────────────────────────────────
// Flat (lapos) doboz: bal fele kitöltve (playmode: WEB/SD/DLNA),
// jobb fele üres keret (állomásszám). Ellentétes a BitrateWidget-tel.
// Méretezés: BitrateBoxConfig dimension alapján (flat: 2*dim × dim/2).
class PlayModeWidget : public Widget {
  public:
    PlayModeWidget() {}
    ~PlayModeWidget();
    using Widget::init;

    void init(BitrateBoxConfig boxconf, uint16_t fgcolor, uint16_t bgcolor);
    void setState(uint8_t playMode, uint16_t num);
    void setMode(uint8_t playMode);   // PM_WEB=0, PM_SDCARD=1, + DLNA=2
    void setNum(uint16_t num);
    void invalidate();                // Force redraw with current theme color

  protected:
    LGFX_Sprite*    _spr = nullptr;
    BitrateBoxConfig _box;
    uint8_t         _mode = 0;
    uint16_t        _num  = 0;

    void _ensureSprite();
    void _draw();
    void _clear();
};

#endif // DSP_MODEL != DSP_DUMMY
