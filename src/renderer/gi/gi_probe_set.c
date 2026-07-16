/**
 * @file gi_probe_set.c
 * @brief Adaptive irradiance probe set (see gi_probe_set.h).
 */
#include "ferrum/renderer/gi/gi_probe_set.h"

#include <stddef.h>

void gi_probe_set_init(gi_probe_set_t *set, float *pos_backing,
                       float *sh_backing, uint32_t capacity)
{
    if (set == NULL)
        return;
    set->pos = pos_backing;
    set->sh = sh_backing;
    set->count = 0u;
    set->capacity = capacity;
}

int32_t gi_probe_add(gi_probe_set_t *set, float x, float y, float z)
{
    if (set == NULL || set->pos == NULL || set->sh == NULL ||
        set->count >= set->capacity)
        return -1;
    uint32_t i = set->count++;
    set->pos[i * 3 + 0] = x;
    set->pos[i * 3 + 1] = y;
    set->pos[i * 3 + 2] = z;
    for (int c = 0; c < 27; ++c)
        set->sh[i * 27 + c] = 0.0f; /* cleared until the kernel fills it. */
    return (int32_t)i;
}

void gi_probe_set_reset(gi_probe_set_t *set)
{
    if (set != NULL)
        set->count = 0u;
}
