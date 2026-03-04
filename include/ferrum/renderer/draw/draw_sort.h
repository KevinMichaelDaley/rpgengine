#ifndef FERRUM_RENDERER_DRAW_SORT_H
#define FERRUM_RENDERER_DRAW_SORT_H

/**
 * @file draw_sort.h
 * @brief 64-bit sort key for draw command ordering.
 *
 * Bit layout (MSB → LSB):
 *   [63..48] shader   (16 bits) — most expensive state change
 *   [47..32] material (16 bits) — texture binds
 *   [31..16] mesh     (16 bits) — VAO bind
 *   [15.. 0] depth    (16 bits) — front-to-back (opaque) or back-to-front
 *
 * Sorting by the packed uint64_t gives the correct priority order.
 *
 * @note For transparent passes, invert the depth field (0xFFFF - depth)
 *       before building the key to achieve back-to-front ordering.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Type ─────────────────────────────────────────────────────────── */

/**
 * @brief Packed 64-bit sort key for draw command ordering.
 */
typedef struct draw_sort_key {
    uint64_t key;
} draw_sort_key_t;

/* ── Construction ─────────────────────────────────────────────────── */

/**
 * @brief Build a sort key from individual fields.
 *
 * @param shader    Shader program ID (0..65535).
 * @param material  Material slot ID (0..65535).
 * @param mesh      Mesh handle index (0..65535).
 * @param depth     Quantized depth (0..65535).
 * @return Packed sort key.
 */
static inline draw_sort_key_t draw_sort_key_build(
    uint16_t shader, uint16_t material, uint16_t mesh, uint16_t depth)
{
    draw_sort_key_t k;
    k.key = ((uint64_t)shader   << 48) |
            ((uint64_t)material << 32) |
            ((uint64_t)mesh     << 16) |
            ((uint64_t)depth);
    return k;
}

/* ── Extraction ───────────────────────────────────────────────────── */

/** @brief Extract shader field from a sort key. */
static inline uint16_t draw_sort_key_shader(draw_sort_key_t k) {
    return (uint16_t)(k.key >> 48);
}

/** @brief Extract material field from a sort key. */
static inline uint16_t draw_sort_key_material(draw_sort_key_t k) {
    return (uint16_t)(k.key >> 32);
}

/** @brief Extract mesh field from a sort key. */
static inline uint16_t draw_sort_key_mesh(draw_sort_key_t k) {
    return (uint16_t)(k.key >> 16);
}

/** @brief Extract depth field from a sort key. */
static inline uint16_t draw_sort_key_depth(draw_sort_key_t k) {
    return (uint16_t)(k.key);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_DRAW_SORT_H */
