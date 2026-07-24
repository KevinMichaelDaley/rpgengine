/**
 * @file refl_half.c
 * @brief IEEE half-float conversion (see refl_half.h).
 */
#include "ferrum/renderer/gi/refl_half.h"

#include <string.h>

uint16_t refl_f32_to_f16(float v)
{
    uint32_t f;
    memcpy(&f, &v, sizeof f);
    uint32_t sign = (f >> 16) & 0x8000u;
    int32_t exp = (int32_t)((f >> 23) & 0xFFu) - 127 + 15;
    uint32_t man = f & 0x7FFFFFu;
    if (((f >> 23) & 0xFFu) == 0xFFu)          /* inf / NaN */
        return (uint16_t)(man ? 0u : (sign | 0x7BFFu));
    if (exp >= 31)                             /* overflow -> max finite */
        return (uint16_t)(sign | 0x7BFFu);
    if (exp <= 0) {                            /* subnormal / underflow */
        if (exp < -10)
            return (uint16_t)sign;
        man |= 0x800000u;
        return (uint16_t)(sign | (man >> (uint32_t)(14 - exp + 10)));
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | (man >> 13));
}

float refl_f16_to_f32(uint16_t h)
{
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)h >> 10) & 0x1Fu;
    uint32_t man = (uint32_t)h & 0x3FFu;
    uint32_t f;
    if (exp == 0u) {
        if (man == 0u) {
            f = sign;
        } else {                               /* subnormal: normalise */
            int32_t e = -1;
            do {
                man <<= 1;
                ++e;
            } while ((man & 0x400u) == 0u);
            f = sign | ((uint32_t)(127 - 15 - e) << 23) |
                ((man & 0x3FFu) << 13);
        }
    } else if (exp == 0x1Fu) {
        f = sign | 0x7F800000u | (man << 13);
    } else {
        f = sign | ((exp - 15u + 127u) << 23) | (man << 13);
    }
    float out;
    memcpy(&out, &f, sizeof out);
    return out;
}
