#include "../../core/player.h"

extern Player player;

extern "C" uint8_t fusion_led_vu_left()
{
    uint16_t vu = player.getVUlevel();
    return (vu >> 8) & 0xFF;
}

extern "C" uint8_t fusion_led_vu_right()
{
    uint16_t vu = player.getVUlevel();
    return vu & 0xFF;
}