#include "../../core/player.h"
#include "../../core/config.h"
#ifdef USE_BLUETOOTH
#include "../../core/bluetooth.h"
#endif

extern Player player;

extern "C" uint8_t fusion_led_vu_left()
{
#ifdef USE_BLUETOOTH
    if (config.getMode() == PM_BLUETOOTH && bluetooth.bridgeRunning()) {
        return bluetooth.vuLevel() & 0xFF;
    }
#endif
    uint16_t vu = player.getVUlevel();
    return (vu >> 8) & 0xFF;
}

extern "C" uint8_t fusion_led_vu_right()
{
#ifdef USE_BLUETOOTH
    if (config.getMode() == PM_BLUETOOTH && bluetooth.bridgeRunning()) {
        return (bluetooth.vuLevel() >> 8) & 0xFF;
    }
#endif
    uint16_t vu = player.getVUlevel();
    return vu & 0xFF;
}
