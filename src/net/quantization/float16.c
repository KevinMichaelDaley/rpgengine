/**
 * @file float16.c
 * @brief IEEE 754 binary16 (half-precision) float conversion.
 *
 * Provides lossless round-trip for values representable in float16.
 * Range: ±65504, smallest normal ~6.1e-5, 3-4 significant digits.
 *
 * Non-static functions: 2 (net_float16_from_float, net_float16_to_float).
 */

#include "ferrum/net/quantization.h"

#include <math.h>
#include <string.h>

/* ── float32 → float16 ────────────────────────────────────────── */

uint16_t net_float16_from_float(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));

    uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t  exp  = (int32_t)((bits >> 23) & 0xFFu) - 127;
    uint32_t mant = bits & 0x007FFFFFu;

    /* NaN → preserve NaN (set mantissa nonzero). */
    if (exp == 128 && mant != 0) {
        return (uint16_t)(sign | 0x7E00u);
    }
    /* Inf → float16 Inf. */
    if (exp == 128) {
        return (uint16_t)(sign | 0x7C00u);
    }
    /* Too large → clamp to float16 max (65504). */
    if (exp > 15) {
        return (uint16_t)(sign | 0x7BFFu);
    }
    /* Normal range for float16. */
    if (exp >= -14) {
        /* Round to nearest, ties to even. */
        uint32_t half_mant = mant >> 13;
        uint32_t remainder = mant & 0x1FFFu;
        if (remainder > 0x1000u ||
            (remainder == 0x1000u && (half_mant & 1u))) {
            half_mant++;
            if (half_mant > 0x3FFu) {
                half_mant = 0;
                exp++;
                if (exp > 15) {
                    return (uint16_t)(sign | 0x7BFFu);
                }
            }
        }
        return (uint16_t)(sign | (uint32_t)(exp + 15) << 10 | half_mant);
    }
    /* Subnormal range for float16 (exp < -14). */
    if (exp >= -24) {
        /* Denormalized: shift mantissa including implicit 1 bit. */
        mant |= 0x00800000u; /* add implicit leading 1 */
        uint32_t shift = (uint32_t)(-(exp + 14) + 13);
        uint32_t half_mant = mant >> shift;
        uint32_t remainder = mant & ((1u << shift) - 1u);
        uint32_t half_bit  = 1u << (shift - 1u);
        if (remainder > half_bit ||
            (remainder == half_bit && (half_mant & 1u))) {
            half_mant++;
        }
        return (uint16_t)(sign | half_mant);
    }
    /* Too small → zero. */
    return (uint16_t)sign;
}

/* ── float16 → float32 ────────────────────────────────────────── */

float net_float16_to_float(uint16_t h) {
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp  = ((uint32_t)h >> 10) & 0x1Fu;
    uint32_t mant = (uint32_t)h & 0x03FFu;

    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            /* Signed zero. */
            bits = sign;
        } else {
            /* Subnormal → normalize. */
            exp = 1;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x03FFu;
            bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        /* Inf or NaN. */
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        /* Normal. */
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }

    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
