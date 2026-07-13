/**
 * @file lm_atlas.c
 * @brief Height-sorted shelf packer for lightmap atlas layout (see lm_atlas.h).
 */
#include "ferrum/lightmap/lm_atlas.h"

/* Insertion-sort the index array so rects[idx[.]].h is non-increasing. Stable,
 * and count = number of surfaces is small, so O(n^2) is fine for a prepass. */
static void lm_atlas_sort_by_height(const lm_atlas_rect_t *rects, uint32_t *idx,
                                    uint32_t count)
{
    for (uint32_t i = 1; i < count; ++i) {
        uint32_t v = idx[i];
        uint32_t hv = rects[v].h;
        uint32_t j = i;
        while (j > 0 && rects[idx[j - 1]].h < hv) {
            idx[j] = idx[j - 1];
            --j;
        }
        idx[j] = v;
    }
}

bool lm_atlas_pack(lm_atlas_rect_t *rects, uint32_t count, uint32_t max_width,
                   uint32_t padding, lm_atlas_t *out)
{
    out->width = max_width;
    out->height = 0;
    if (count == 0)
        return true;

    /* No dynamic allocation: sort a bounded on-stack index array. Fall back to
     * the natural order for very large counts (still correct, just less tidy). */
    enum { MAX_SORTED = 4096 };
    uint32_t order[MAX_SORTED];
    bool sorted = count <= MAX_SORTED;
    if (sorted) {
        for (uint32_t i = 0; i < count; ++i)
            order[i] = i;
        lm_atlas_sort_by_height(rects, order, count);
    }

    uint32_t cursor_x = padding;
    uint32_t cursor_y = padding;
    uint32_t shelf_h = 0;
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = sorted ? order[k] : k;
        lm_atlas_rect_t *r = &rects[i];
        if (r->w + 2u * padding > max_width)
            return false; /* cannot fit on any shelf */
        if (cursor_x + r->w + padding > max_width) {
            /* Start a new shelf below the current one. */
            cursor_y += shelf_h + padding;
            cursor_x = padding;
            shelf_h = 0;
        }
        r->x = cursor_x;
        r->y = cursor_y;
        cursor_x += r->w + padding;
        if (r->h > shelf_h)
            shelf_h = r->h;
    }
    out->height = cursor_y + shelf_h + padding;
    return true;
}
