#pragma once
#include "../../core/options.h"
#if DSP_MODEL != DSP_DUMMY

#include <LovyanGFX.hpp>
#include "widgetsconfig.h"
#include "../display_select.h"
#include "widget.h"

// ── StatusWidget ─────────────────────────────────────────────────────────────
// 3 lekerekített téglalap egymás mellett: FADE | TTS | RGB
// Aktív: kitöltött (status_active szín), Inaktív: csak keret (status_inactive)
// Méret: 50×26px, 4px gap, 4px saroklekerekítés (Fusion stílus)
class StatusWidget : public Widget {
  public:
    using Widget::init;
    void init(WidgetConfig wconf, uint16_t bgcolor);

    void setFade(bool enabled);
    void setTts(bool enabled, bool onlyNoStream = false);
    void setRgb(bool enabled, bool duringScreensaver = false);

  protected:
    void _draw()  override;
    void _clear() override;
    void _reset() override {}

  private:
    bool     _fade       = false;
    bool     _tts        = false;
    bool     _ttsExtra   = false;   // OnlyWhenNoStream
    bool     _rgb        = false;
    bool     _rgbExtra   = false;   // DuringScreensaver
    uint16_t _bgcolor = 0;

    // mode: 0=disabled, 1=enabled_only (keret, fg szín), 2=full_active (kitöltött)
    void _drawDot(uint8_t idx, uint8_t mode, const char* label);
};

#endif // DSP_MODEL != DSP_DUMMY
