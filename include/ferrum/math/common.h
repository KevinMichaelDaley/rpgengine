#ifndef FERRUM_MATH_COMMON_H
#define FERRUM_MATH_COMMON_H

/** @file
 * @brief Common math utility functions.
 */

/**
 * @brief Clamp a float value to [lo, hi].
 * @param val Value to clamp.
 * @param lo  Lower bound.
 * @param hi  Upper bound.
 * @return Clamped value.
 */
static inline float fr_clampf(float val, float lo, float hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

#endif /* FERRUM_MATH_COMMON_H */
