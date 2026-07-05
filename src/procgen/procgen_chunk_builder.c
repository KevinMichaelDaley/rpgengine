/**
 * @file procgen_chunk_builder.c
 * @brief Chunk-based SVO rasterizer implementation.
 */
#include "ferrum/procgen/procgen_chunk_builder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void procgen_chunk_grid_init(procgen_chunk_grid_t *grid,
                              float chunk_size,
                              uint32_t max_depth,
                              float world_extent) {
    memset(grid, 0, sizeof(*grid));
    grid->chunk_size   = chunk_size;
    grid->max_depth    = max_depth;
    grid->world_extent = world_extent;

    float span    = world_extent * 2.0f;
    grid->count_x = (uint32_t)ceilf(span / chunk_size);
    grid->count_z = (uint32_t)ceilf(span / chunk_size);

    size_t total = (size_t)grid->count_x * (size_t)grid->count_z;
    grid->chunks = calloc(total, sizeof(procgen_chunk_t));
    if (!grid->chunks) return;

    for (size_t i = 0; i < total; i++) {
        uint32_t cx = (uint32_t)i % grid->count_x;
        uint32_t cz = (uint32_t)i / grid->count_x;
        grid->chunks[i].origin_x  = -world_extent + (float)cx * chunk_size;
        grid->chunks[i].origin_y  = -world_extent;
        grid->chunks[i].origin_z  = -world_extent + (float)cz * chunk_size;
        grid->chunks[i].max_depth = max_depth;
    }

    grid->initialized = 1;
}

void procgen_chunk_grid_destroy(procgen_chunk_grid_t *grid) {
    if (!grid || !grid->chunks) return;
    for (size_t i = 0; i < (size_t)grid->count_x * grid->count_z; i++) {
        if (grid->chunks[i].loaded) {
            procgen_mesh_destroy(&grid->chunks[i].mesh);
            npc_svo_grid_destroy(&grid->chunks[i].svo);
        }
    }
    free(grid->chunks);
    memset(grid, 0, sizeof(*grid));
}

int procgen_chunk_grid_chunk_at(const procgen_chunk_grid_t *grid,
                                 float wx, float wy, float wz) {
    if (!grid || !grid->initialized) return -1;
    (void)wy;
    int cx = (int)((wx + grid->world_extent) / grid->chunk_size);
    int cz = (int)((wz + grid->world_extent) / grid->chunk_size);
    if (cx < 0 || cx >= (int)grid->count_x || cz < 0 || cz >= (int)grid->count_z)
        return -1;
    return cz * (int)grid->count_x + cx;
}

static void load_chunk(procgen_chunk_t *chunk, float world_extent) {
    if (chunk->loaded) return;
    uint32_t cells = 1u << chunk->max_depth;
    phys_aabb_t bounds = {
        .min = { chunk->origin_x,
                 -world_extent,
                 chunk->origin_z },
        .max = { chunk->origin_x + (float)cells,
                  world_extent,
                 chunk->origin_z + (float)cells },
    };
    if (!npc_svo_grid_init(&chunk->svo, bounds, chunk->max_depth)) return;
    procgen_mesh_init(&chunk->mesh);
    chunk->loaded = 1;
}

uint32_t procgen_chunk_grid_unload_far(procgen_chunk_grid_t *grid,
                                        float cx, float cz,
                                        float radius) {
    if (!grid || !grid->chunks) return 0;
    uint32_t unloaded = 0;
    size_t total = (size_t)grid->count_x * grid->count_z;
    for (size_t i = 0; i < total; i++) {
        procgen_chunk_t *c = &grid->chunks[i];
        if (!c->loaded) continue;
        float mid_x = c->origin_x + grid->chunk_size * 0.5f;
        float mid_z = c->origin_z + grid->chunk_size * 0.5f;
        float dist = sqrtf((mid_x - cx) * (mid_x - cx) + (mid_z - cz) * (mid_z - cz));
        if (dist > radius) {
            procgen_mesh_destroy(&c->mesh);
            npc_svo_grid_destroy(&c->svo);
            c->loaded = 0;
            unloaded++;
        }
    }
    return unloaded;
}

/**
 * @brief Mark a block in a specific chunk.  Creates the chunk SVO on
 *        first access if not yet loaded.
 */
static void chunk_mark(procgen_chunk_grid_t       *grid,
                        int                         chunk_idx,
                        float                       world_x,
                        float                       world_y,
                        float                       world_z,
                        uint16_t                    material) {
    if (chunk_idx < 0 || !grid || !grid->chunks) return;
    procgen_chunk_t *c = &grid->chunks[chunk_idx];
    if (!c->loaded) load_chunk(c, grid->world_extent);
    if (!c->loaded) return;

    /* Convert world to voxel coordinates.
     * The chunk SVO covers: X/Z = chunk_size (cells²), Y = full world height.
     * voxel_size = 1.0m (chunk_size = cells). */
    uint32_t cells = 1u << c->max_depth;
    float    vs    = c->svo.voxel_size;
    int vx = (int)((world_x - c->origin_x) / vs);
    int vy = (int)((world_y - (-grid->world_extent)) / vs);
    int vz = (int)((world_z - c->origin_z) / vs);

    if (vx < 0 || vy < 0 || vz < 0) return;
    if ((uint32_t)vx >= cells || (uint32_t)vy >= cells || (uint32_t)vz >= cells) return;

    /* Walk/create octree path and set SOLID. */
    uint32_t n  = 0;
    uint32_t c2 = cells;
    for (uint32_t d = 0; d < c->max_depth; d++) {
        c2 >>= 1;
        uint32_t cx = ((uint32_t)vz / c2) & 1;
        uint32_t cy = ((uint32_t)vy / c2) & 1;
        uint32_t cz2 = ((uint32_t)vx / c2) & 1;
        uint32_t ci = (cx << 2) | (cy << 1) | cz2;
        npc_svo_node_t *node = &c->svo.nodes[n];
        uint32_t child = node->children[ci];
        if (child == NPC_SVO_INVALID_NODE) {
            child = npc_svo_alloc_node(&c->svo);
            if (child == NPC_SVO_INVALID_NODE) return;
            node->children[ci]   = child;
            c->svo.nodes[child].parent = n;
            node->occupancy     |= (1u << ci);
        }
        n = child;
    }
    c->svo.nodes[n].flags    |= NPC_SVO_FLAG_SOLID;
    c->svo.nodes[n].material  = material;
}

/* ── Bulk marking helpers ───────────────────────────────────────── */

static void chunk_line_mark(procgen_chunk_grid_t *grid,
                            float x1, float y1, float x2, float y2,
                            float w, float floor_z, float ceil_z,
                            uint16_t mat) {
    float hw = w * 0.5f;
    float dx = x2 - x1, dy2 = y2 - y1;
    float len = sqrtf(dx*dx + dy2*dy2);

    /* Walk along the corridor at 1m resolution using integer steps. */
    float perp_x = (len > 0.001f) ? -dy2 / len * hw : hw;
    float perp_y = (len > 0.001f) ?  dx / len * hw : 0;

    int x_min = (int)floorf(fminf(x1, x2) - fabsf(perp_x) - hw);
    int x_max = (int)ceilf(fmaxf(x1, x2) + fabsf(perp_x) + hw);
    int z_min = (int)floorf(fminf(y1, y2) - fabsf(perp_y) - hw);
    int z_max = (int)ceilf(fmaxf(y1, y2) + fabsf(perp_y) + hw);

    int iz_min = (int)floorf(floor_z), iz_max = (int)ceilf(ceil_z);

    for (int fy = iz_min; fy <= iz_max; fy++) {
        for (int wz = z_min; wz <= z_max; wz++) {
            for (int wx = x_min; wx <= x_max; wx++) {
                float px = (float)wx + 0.5f;
                float pz = (float)wz + 0.5f;

                float t = ((px - x1) * dx + (pz - y1) * dy2) / (len * len);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float cx = x1 + t * dx;
                float cz = y1 + t * dy2;
                float dist2 = (px - cx)*(px - cx) + (pz - cz)*(pz - cz);

                if (dist2 <= hw * hw) {
                    /* Only mark floor, ceiling, and the two lateral edges.
                       Leave interior hollow so it cuts through rooms. */
                    int on_floor   = (fy == iz_min);
                    int on_ceiling = (fy == iz_max);
                    int on_edge    = (wz == z_min || wz == z_max
                                      || wx == x_min || wx == x_max);
                    if (!on_floor && !on_ceiling && !on_edge) continue;

                    int ci = procgen_chunk_grid_chunk_at(grid, (float)wx, (float)fy, (float)wz);
                    if (ci >= 0) {
                        chunk_mark(grid, ci, (float)wx, (float)fy, (float)wz, mat);
                    }
                }
            }
        }
    }
}

static int is_in_corridor(const fr_dungeon_layout_t *layout,
                          float wx, float wy, float wz) {
    for (uint32_t ci = 0; ci < layout->corridor_count; ci++) {
        const fr_corridor_def_t *c = &layout->corridors[ci];
        float hw = c->width * 0.5f;
        if (wy < c->floor_z || wy > c->ceil_z) continue;
        float dx = c->to.x - c->from.x;
        float dy2 = c->to.y - c->from.y;
        float len2 = dx * dx + dy2 * dy2;
        float t = ((wx - c->from.x) * dx + (wz - c->from.y) * dy2) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float cx = c->from.x + t * dx;
        float cz = c->from.y + t * dy2;
        float d2 = (wx - cx) * (wx - cx) + (wz - cz) * (wz - cz);
        if (d2 <= hw * hw) return 1;
    }
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────── */

uint32_t procgen_chunk_grid_build(procgen_chunk_grid_t       *grid,
                                   const fr_dungeon_layout_t  *layout) {
    if (!grid || !layout) return 0;
    uint32_t active = 0;

    /* Pass 1: rooms — skip voxels that fall inside corridors.
       The corridor surfaces will fill the gap in pass 2. */
    for (uint32_t ri = 0; ri < layout->room_count; ri++) {
        const fr_room_def_t *r = &layout->rooms[ri];
        float xmin = r->vertices[0].x, xmax = xmin;
        float ymin = r->vertices[0].y, ymax = ymin;
        for (uint32_t j = 1; j < r->vertex_count; j++) {
            if (r->vertices[j].x < xmin) xmin = r->vertices[j].x;
            if (r->vertices[j].x > xmax) xmax = r->vertices[j].x;
            if (r->vertices[j].y < ymin) ymin = r->vertices[j].y;
            if (r->vertices[j].y > ymax) ymax = r->vertices[j].y;
        }
        int ix_min = (int)floorf(xmin), ix_max = (int)ceilf(xmax);
        int iy_min = (int)floorf(ymin), iy_max = (int)ceilf(ymax);
        int iz_min = (int)floorf(r->floor_z), iz_max = (int)ceilf(r->ceil_z);

        for (int fy = iz_min; fy <= iz_max; fy++) {
            for (int wz = iy_min; wz <= iy_max; wz++) {
                for (int wx = ix_min; wx <= ix_max; wx++) {
                    int on_floor   = (fy == iz_min);
                    int on_ceiling = (fy == iz_max);
                    int on_wall    = (wx == ix_min || wx == ix_max
                                      || wz == iy_min || wz == iy_max);
                    if (!on_floor && !on_ceiling && !on_wall) continue;
                    /* Skip positions that fall inside any corridor. */
                    if (is_in_corridor(layout, (float)wx, (float)fy, (float)wz)) continue;
                    int ci = procgen_chunk_grid_chunk_at(grid, (float)wx, (float)fy, (float)wz);
                    if (ci >= 0) {
                        chunk_mark(grid, ci, (float)wx, (float)fy, (float)wz, 1);
                        active = 1;
                    }
                }
            }
        }
    }

    /* Pass 2: corridors — fill the gaps left by pass 1. */
    for (uint32_t ci = 0; ci < layout->corridor_count; ci++) {
        const fr_corridor_def_t *c = &layout->corridors[ci];
        chunk_line_mark(grid, c->from.x, c->from.y, c->to.x, c->to.y,
                        c->width, c->floor_z, c->ceil_z, 2);
        active = 1;
    }

    for (uint32_t ri = 0; ri < layout->ramp_count; ri++) {
        const fr_ramp_def_t *r = &layout->ramps[ri];
        float lo = fminf(r->from.z, r->to.z);
        float hi = fmaxf(r->from.z, r->to.z);
        chunk_line_mark(grid, r->from.x, r->from.y, r->to.x, r->to.y,
                        r->width, lo, hi, 2);
        active = 1;
    }

    return active;
}
