#pragma once

#include "../../pluginsManager/pluginsManager.h"

class LedStripPlugin : public Plugin {
  public:
    LedStripPlugin();

    void on_setup() override;
    void on_connect() override;
    void on_start_play() override;
    void on_stop_play() override;
    void on_station_change() override;
    void on_track_change() override;
    void on_ticker() override;
    void on_loop() override;  // display.cpp loop()-ban hívandó
    //void on_display_queue(requestParams_t& request, bool& result) override;
};

void ledstripPluginInit();
