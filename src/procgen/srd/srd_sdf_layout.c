/**
 * @file srd_sdf_layout.c
 * @brief SDF layout: lifecycle, from_grid, copy, and adjacency helpers.
 *
 * Non-static functions (4): srd_sdf_layout_init, srd_sdf_layout_from_grid,
 *                            srd_sdf_layout_copy, srd_sdf_layout_from_grid (helper)
 */
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#include <string.h>

/* ── char → room type mapping ───────────────────────────────────── */

static srd_room_type_t char_to_room_type(char c) {
    switch (c) {
    case 'R': case 'r': return SRD_ROOM_GENERIC;
    case 'B': case 'b': return SRD_ROOM_BAR;
    case 'G': case 'g': return SRD_ROOM_ENTRANCE;
    case 'P': case 'p': return SRD_ROOM_PRIVATE;
    case '^':           return SRD_ROOM_STAIR_UP;
    case 'v':           return SRD_ROOM_STAIR_DOWN;
    default:            return SRD_ROOM_GENERIC;
    }
}

/* ── Lifecycle ──────────────────────────────────────────────────── */

void srd_sdf_layout_init(srd_sdf_layout_t *layout) {
    if (!layout) return;
    memset(layout, 0, sizeof(*layout));
}

int srd_sdf_layout_from_grid(srd_sdf_layout_t *layout,
                             const srd_grid_t *grid) {
    if (!layout || !grid) return -1;
    if (grid->n_regions > SRD_MAX_BOXES) return -1;

    srd_sdf_layout_init(layout);
    layout->bounds_w = (float)grid->width;
    layout->bounds_h = (float)grid->height;

    /* For each region, compute bounding box of its cells */
    for (int ri = 0; ri < grid->n_regions; ri++) {
        int xmin = grid->width, xmax = -1;
        int zmin = grid->height, zmax = -1;

        for (int z = 0; z < grid->height; z++) {
            for (int x = 0; x < grid->width; x++) {
                if (grid->region_ids[z * grid->width + x] == ri) {
                    if (x < xmin) xmin = x;
                    if (x > xmax) xmax = x;
                    if (z < zmin) zmin = z;
                    if (z > zmax) zmax = z;
                }
            }
        }
        if (xmax < 0) continue;  /* empty region */

        srd_sdf_box_t box;
        memset(&box, 0, sizeof(box));
        box.cx = (float)(xmin + xmax) * 0.5f;
        box.cz = (float)(zmin + zmax) * 0.5f;
        box.hw = (float)(xmax - xmin + 1) * 0.5f;
        box.hd = (float)(zmax - zmin + 1) * 0.5f;
        box.type = char_to_room_type(grid->regions[ri].type_char);
        box.flags = 0;

        srd_sdf_layout_add_box(layout, &box);
    }

    /* Set adjacency from grid edges */
    for (int e = 0; e < grid->n_edges; e++) {
        int a = grid->edges[e].a;
        int b = grid->edges[e].b;
        if (a >= 0 && a < layout->n_boxes &&
            b >= 0 && b < layout->n_boxes) {
            srd_sdf_layout_set_adj(layout, a, b, true);
        }
    }

    return 0;
}

void srd_sdf_layout_copy(srd_sdf_layout_t *dst,
                         const srd_sdf_layout_t *src) {
    if (!dst || !src) return;
    memcpy(dst, src, sizeof(*dst));
}
