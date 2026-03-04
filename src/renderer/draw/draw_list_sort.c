/**
 * @file draw_list_sort.c
 * @brief Radix sort for draw_list_t (8-pass LSB on 64-bit keys).
 *
 * Uses the scratch buffer allocated after the command array by
 * draw_list_init (commands + capacity offset).  Stable O(n) sort.
 */

#include "ferrum/renderer/draw/draw_list.h"

#include <string.h>

/* Number of radix sort passes (8 bytes × 1 pass per byte). */
#define RADIX_BITS  8
#define RADIX_SIZE  (1u << RADIX_BITS)    /* 256 */
#define RADIX_MASK  (RADIX_SIZE - 1u)     /* 0xFF */

/* ── draw_list_sort ───────────────────────────────────────────────── */

void draw_list_sort(draw_list_t *list)
{
    if (!list || list->count <= 1) { return; }

    uint32_t n = list->count;
    draw_command_t *src = list->commands;
    /* Scratch buffer sits right after the command array. */
    draw_command_t *dst = list->commands + list->capacity;

    /* 8-pass LSB radix sort over the 64-bit key. */
    for (int pass = 0; pass < 8; ++pass) {
        int shift = pass * RADIX_BITS;

        /* Build histogram. */
        uint32_t counts[RADIX_SIZE];
        memset(counts, 0, sizeof(counts));
        for (uint32_t i = 0; i < n; ++i) {
            uint8_t bucket = (uint8_t)((src[i].sort_key.key >> shift) & RADIX_MASK);
            ++counts[bucket];
        }

        /* Prefix sum → offsets. */
        uint32_t offsets[RADIX_SIZE];
        offsets[0] = 0;
        for (uint32_t b = 1; b < RADIX_SIZE; ++b) {
            offsets[b] = offsets[b - 1] + counts[b - 1];
        }

        /* Scatter into dst. */
        for (uint32_t i = 0; i < n; ++i) {
            uint8_t bucket = (uint8_t)((src[i].sort_key.key >> shift) & RADIX_MASK);
            dst[offsets[bucket]++] = src[i];
        }

        /* Swap src/dst for next pass. */
        draw_command_t *tmp = src;
        src = dst;
        dst = tmp;
    }

    /*
     * After 8 passes (even number), src points back to the original
     * commands array if we started there.  If not, copy back.
     */
    if (src != list->commands) {
        memcpy(list->commands, src, n * sizeof(draw_command_t));
    }
}
