#pragma once

#include <FS.h>
#include <LittleFS.h>

#ifndef LGFX_WIDTH
#define LGFX_WIDTH 480
#endif

#ifndef LGFX_HEIGHT
#define LGFX_HEIGHT 320
#endif

#ifndef LGFX_ROTATION
#define LGFX_ROTATION 0
#endif

#include "lgfx/panel_axs15231b.h"
#include "lgfx/lgfx_base.h"

class LGFX_AXS15231B : public LGFX_Base<lgfx::Panel_AXS15231B> {
  public:
    LGFX_AXS15231B() : LGFX_Base(LGFX_WIDTH, LGFX_HEIGHT, LGFX_ROTATION) {}
};

typedef LGFX_AXS15231B yoDisplay;
typedef lgfx::LGFX_Sprite Canvas;

#include "tools/commongfx.h"

#include "conf/conf_480x320.h"
