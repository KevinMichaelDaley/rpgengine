/**
 * @file chunk_table.c
 * @brief Spatial chunk table over the streaming manager (rpg-nbp2): register
 *        light-data chunks, prioritize by interest, report resident boxes.
 */
#include <string.h>

#include "ferrum/asset/chunk_table.h"

/* Squared distance from point p to AABB [lo,hi] (0 if inside). */
static float box_dist2(const float p[3], const float lo[3], const float hi[3])
{
    float d2 = 0.0f;
    for (int a = 0; a < 3; ++a) {
        float v = p[a];
        if (v < lo[a]) { float e = lo[a] - v; d2 += e * e; }
        else if (v > hi[a]) { float e = v - hi[a]; d2 += e * e; }
    }
    return d2;
}

void fr_chunk_table_init(fr_chunk_table_t *t, fr_asset_stream_t *stream,
                         fr_chunk_entry_t *storage, uint32_t cap)
{
    if (t == NULL) return;
    t->stream = stream;
    t->entries = storage;
    t->cap = cap;
    t->count = 0;
}

bool fr_chunk_table_add(fr_chunk_table_t *t, uint64_t id, fr_asset_class_t cls,
                        const float box_min[3], const float box_max[3],
                        size_t ram_size, size_t vram_size, void *user)
{
    if (t == NULL || t->entries == NULL || t->stream == NULL ||
        box_min == NULL || box_max == NULL)
        return false;
    if (t->count >= t->cap) return false;
    /* Priority is set later from interest; start neutral (0). */
    if (!fr_asset_stream_add(t->stream, id, cls, ram_size, vram_size, 0, user))
        return false;
    fr_chunk_entry_t *e = &t->entries[t->count++];
    e->id = id;
    memcpy(e->box_min, box_min, 3 * sizeof(float));
    memcpy(e->box_max, box_max, 3 * sizeof(float));
    return true;
}

void fr_chunk_table_set_interest(fr_chunk_table_t *t, const float point[3],
                                 float scale)
{
    if (t == NULL || point == NULL) return;
    for (uint32_t i = 0; i < t->count; ++i) {
        const fr_chunk_entry_t *e = &t->entries[i];
        float d2 = box_dist2(point, e->box_min, e->box_max);
        /* Nearer => higher priority. Priority decreases with distance. */
        int pri = -(int)(d2 * scale);
        fr_asset_stream_set_priority(t->stream, e->id, pri);
    }
}

uint32_t fr_chunk_table_resident_boxes(const fr_chunk_table_t *t, float *out_min,
                                       float *out_max, uint32_t cap)
{
    if (t == NULL || out_min == NULL || out_max == NULL) return 0u;
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->count && n < cap; ++i) {
        const fr_chunk_entry_t *e = &t->entries[i];
        fr_asset_residency_t r = fr_asset_stream_residency(t->stream, e->id);
        if (r == FR_RESIDENCY_RAM || r == FR_RESIDENCY_VRAM) {
            memcpy(&out_min[n * 3], e->box_min, 3 * sizeof(float));
            memcpy(&out_max[n * 3], e->box_max, 3 * sizeof(float));
            n++;
        }
    }
    return n;
}
