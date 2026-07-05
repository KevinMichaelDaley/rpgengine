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

static void load_chunk(procgen_chunk_t *chunk) {
    if (chunk->loaded) return;
    phys_aabb_t bounds = {
        .min = { chunk->origin_x,
                 chunk->origin_y,
                 chunk->origin_z },
        .max = { chunk->origin_x + chunk->svo.voxel_size * (float)(1u << chunk->max_depth),
                 chunk->origin_y + chunk->svo.voxel_size * (float)(1u << chunk->max_depth),
                 chunk->origin_z + chunk->svo.voxel_size * (float)(1u << chunk->max_depth) },
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
    if (!c->loaded) load_chunk(c);
    if (!c->loaded) return;

    /* Convert world to voxel within this chunk. */
    uint32_t cells = 1u << c->max_depth;
    float    span  = grid->chunk_size;
    if (world_x < c->origin_x || world_x >= c->origin_x + span) return;
    if (world_y < c->origin_y || world_y >= c->origin_y + span) return;
    if (world_z < c->origin_z || world_z >= c->origin_z + span) return;

    uint32_t vx = (uint32_t)((world_x - c->origin_x) / span * (float)cells);
    uint32_t vy = (uint32_t)((world_y - c->origin_y) / span * (float)cells);
    uint32_t vz = (uint32_t)((world_z - c->origin_z) / span * (float)cells);

    if (vx >= cells) vx = cells - 1;
    if (vy >= cells) vy = cells - 1;
    if (vz >= cells) vz = cells - 1;

    /* Walk/create octree path and set SOLID. */
    uint32_t n = 0;
    uint32_t c2 = cells;
    for (uint32_t d = 0; d < c->max_depth; d++) {
        c2 >>= 1;
        uint32_t cx2 = (vx / c2) & 1;
        uint32_t cy2 = (vy / c2) & 1;
        uint32_t cz2 = (vz / c2) & 1;
        uint32_t ci  = (cz2 << 2) | (cy2 << 1) | cx2;
        npc_svo_node_t *node = &c->svo.nodes[n];
        uint32_t child = node->children[ci];
        if (child == NPC_SVO_INVALID_NODE) {
            child = npc_svo_alloc_node(&c->svo);
            if (child == NPC_SVO_INVALID_NODE) return;
            node->children[ci]  = child;
            c->svo.nodes[child].parent = n;
            node->occupancy    |= (1u << ci);
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
    int steps = (int)len + 1;
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / (float)steps;
        float px = x1 + t * dx;
        float pz = y1 + t * dy2;
        float cx = px, cz = pz;
        for (int side = -1; side <= 1; side += 2) {
            float sx = px + side * (-dy2 / len * hw);
            float sz = pz + side * ( dx  / len * hw);
            int ci = procgen_chunk_grid_chunk_at(grid, sx, floor_z, sz);
            int ci2 = procgen_chunk_grid_chunk_at(grid, sx, ceil_z, sz);
            for (float fy = floor_z; fy <= ceil_z; fy += 1.0f) {
                chunk_mark(grid, ci,  sx,    fy, sz, mat);
                chunk_mark(grid, ci,  cx,    fy, cz, mat);
                chunk_mark(grid, ci2, sx,    fy, sz, mat);
            }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

uint32_t procgen_chunk_grid_build(procgen_chunk_grid_t       *grid,
                                   const fr_dungeon_layout_t  *layout) {
    if (!grid || !layout) return 0;
    uint32_t active = 0;

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
        /* Mark room solid blocks: floor, ceiling, walls. */
        for (float fy = r->floor_z; fy <= r->ceil_z; fy += 1.0f) {
            for (float wz = ymin; wz <= ymax; wz += 1.0f) {
                for (float wx = xmin; wx <= xmax; wx += 1.0f) {
                    int ci = procgen_chunk_grid_chunk_at(grid, wx, fy, wz);
                    if (ci >= 0) {
                        chunk_mark(grid, ci, wx, fy, wz, 1);
                        active = 1;
                    }
                }
            }
        }
    }

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
