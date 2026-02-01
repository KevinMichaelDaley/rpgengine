#include <stdint.h>

#include "ferrum/net/quantization.h"

uint16_t net_anim_time_u16_add_wrap(uint16_t t, uint16_t delta) {
    return (uint16_t)((uint16_t)(t + delta));
}

int net_anim_time_u16_delta_signed(uint16_t a, uint16_t b) {
    const int diff = (int)(uint16_t)(a - b);
    if (diff > 32767) {
        return diff - 65536;
    }
    return diff;
}
