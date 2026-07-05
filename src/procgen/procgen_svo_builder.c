/**
 * @file procgen_svo_builder.c
 * @brief Configurable SVO rasterizer + greedy mesh generation.
 */

#include "ferrum/procgen/procgen_svo_builder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Config ────────────────────────────────────────────────────── */

void procgen_raster_config_default(procgen_raster_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->type         = PROCGEN_RASTER_VOXEL;
    cfg->voxel_size   = PROCGEN_DEFAULT_VOXEL_SIZE;
    cfg->max_depth    = PROCGEN_DEFAULT_MAX_DEPTH;
    cfg->world_extent = 128.0f;
}

/* ── SVO leaf helpers ──────────────────────────────────────────── */

static uint32_t ensure_leaf_(npc_svo_grid_t *grid,
                             uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t node_idx = 0;
    uint32_t cells = 1u << grid->max_depth;
    for (uint32_t d = 0; d < grid->max_depth; d++) {
        cells >>= 1;
        uint32_t cx = (vx / cells) & 1;
        uint32_t cy = (vy / cells) & 1;
        uint32_t cz = (vz / cells) & 1;
        uint32_t ci = (cz << 2) | (cy << 1) | cx;
        npc_svo_node_t *n = &grid->nodes[node_idx];
        uint32_t ch = n->children[ci];
        if (ch == NPC_SVO_INVALID_NODE) {
            ch = npc_svo_alloc_node(grid);
            if (ch == NPC_SVO_INVALID_NODE) return NPC_SVO_INVALID_NODE;
            n->children[ci] = ch;
            grid->nodes[ch].parent = node_idx;
            n->occupancy |= (1u << ci);
        }
        node_idx = ch;
    }
    return node_idx;
}

static void mark_solid_(npc_svo_grid_t *g, uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t leaf = ensure_leaf_(g, vx, vy, vz);
    if (leaf != NPC_SVO_INVALID_NODE) g->nodes[leaf].flags |= NPC_SVO_FLAG_SOLID;
}

static int is_solid_(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t cells = 1u << g->max_depth;
    if (x < 0 || y < 0 || z < 0 || (uint32_t)x >= cells || (uint32_t)y >= cells || (uint32_t)z >= cells)
        return 0;
    uint32_t node_idx = 0;
    uint32_t c = cells;
    for (uint32_t d = 0; d < g->max_depth; d++) {
        c >>= 1;
        uint32_t cx = ((uint32_t)x / c) & 1;
        uint32_t cy = ((uint32_t)y / c) & 1;
        uint32_t cz = ((uint32_t)z / c) & 1;
        uint32_t ci = (cz << 2) | (cy << 1) | cx;
        uint32_t ch = g->nodes[node_idx].children[ci];
        if (ch == NPC_SVO_INVALID_NODE) return 0;
        node_idx = ch;
    }
    return (g->nodes[node_idx].flags & NPC_SVO_FLAG_SOLID) != 0;
}

/* ── Voxel coordinate conversion ──────────────────────────────── */

static void voxel_from_world_(const npc_svo_grid_t *g,
                              float wx, float wy, float wz,
                              uint32_t *vx, uint32_t *vy, uint32_t *vz) {
    uint32_t cells = 1u << g->max_depth;
    float sx = g->world_bounds.max.x - g->world_bounds.min.x;
    float sy = g->world_bounds.max.y - g->world_bounds.min.y;
    float sz = g->world_bounds.max.z - g->world_bounds.min.z;
    float cx = (wx - g->world_bounds.min.x) / sx * (float)cells;
    float cy = (wy - g->world_bounds.min.y) / sy * (float)cells;
    float cz = (wz - g->world_bounds.min.z) / sz * (float)cells;
    *vx = (uint32_t)(cx < 0 ? 0 : (cx >= (float)cells ? cells - 1 : cx));
    *vy = (uint32_t)(cy < 0 ? 0 : (cy >= (float)cells ? cells - 1 : cy));
    *vz = (uint32_t)(cz < 0 ? 0 : (cz >= (float)cells ? cells - 1 : cz));
}

/* ── Point-in-polygon ──────────────────────────────────────────── */

static int point_in_poly_(const vec3_t *v, uint32_t n, float px, float py) {
    int inside = 0;
    for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
        float xi = v[i].x, yi = v[i].y;
        float xj = v[j].x, yj = v[j].y;
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

/* ── Rasterize volumes ─────────────────────────────────────────── */

static uint32_t rasterize_volume_(npc_svo_grid_t *g,
                                  float xmin, float xmax,
                                  float ymin, float ymax,
                                  float zmin, float zmax,
                                  const vec3_t *poly, uint32_t pn) {
    uint32_t marked = 0, cells = 1u << g->max_depth;
    uint32_t mvx, mvy, mvz, Mvx, Mvy, Mvz;
    voxel_from_world_(g, xmin, ymin, zmin, &mvx, &mvy, &mvz);
    voxel_from_world_(g, xmax, ymax, zmax, &Mvx, &Mvy, &Mvz);
    if (Mvx >= cells) Mvx = cells - 1;
    if (Mvy >= cells) Mvy = cells - 1;
    if (Mvz >= cells) Mvz = cells - 1;
    float vs = g->voxel_size;
    for (uint32_t vz = mvz; vz <= Mvz; vz++)
        for (uint32_t vy = mvy; vy <= Mvy; vy++)
            for (uint32_t vx = mvx; vx <= Mvx; vx++) {
                float cx = g->world_bounds.min.x + ((float)vx + 0.5f) * vs;
                float cy = g->world_bounds.min.y + ((float)vy + 0.5f) * vs;
                int ok = pn > 0 ? point_in_poly_(poly, pn, cx, cy) : 1;
                if (ok) { mark_solid_(g, vx, vy, vz); marked++; }
            }
    return marked;
}

static uint32_t rasterize_line_(npc_svo_grid_t *g,
                                float x1, float y1, float x2, float y2,
                                float width, float fz, float cz) {
    uint32_t marked = 0, cells = 1u << g->max_depth;
    float hw = width * 0.5f;
    float xmin = fminf(x1,x2)-hw, xmax = fmaxf(x1,x2)+hw;
    float ymin = fminf(y1,y2)-hw, ymax = fmaxf(y1,y2)+hw;
    uint32_t mvx, mvy, mvz, Mvx, Mvy, Mvz;
    voxel_from_world_(g, xmin, ymin, fz, &mvx, &mvy, &mvz);
    voxel_from_world_(g, xmax, ymax, cz, &Mvx, &Mvy, &Mvz);
    if (Mvx >= cells) Mvx = cells - 1;
    if (Mvy >= cells) Mvy = cells - 1;
    if (Mvz >= cells) Mvz = cells - 1;
    float vs = g->voxel_size;
    float dx = x2-x1, dy = y2-y1, len2 = dx*dx+dy*dy;
    for (uint32_t vz = mvz; vz <= Mvz; vz++)
        for (uint32_t vy = mvy; vy <= Mvy; vy++)
            for (uint32_t vx = mvx; vx <= Mvx; vx++) {
                float px = g->world_bounds.min.x + ((float)vx + 0.5f) * vs;
                float py = g->world_bounds.min.y + ((float)vy + 0.5f) * vs;
                float t = ((px-x1)*dx + (py-y1)*dy)/len2;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float cx = x1+t*dx, cy = y1+t*dy;
                float d2 = (px-cx)*(px-cx)+(py-cy)*(py-cy);
                if (d2 <= hw*hw) { mark_solid_(g, vx, vy, vz); marked++; }
            }
    return marked;
}

/* ── Public SVO build ──────────────────────────────────────────── */

uint32_t procgen_svo_build_cfg(const procgen_raster_config_t *cfg,
                                const fr_dungeon_layout_t *layout,
                                npc_svo_grid_t *out_grid) {
    if (!cfg || !layout || !out_grid) return 0;

    phys_aabb_t bounds;
    bounds.min = (phys_vec3_t){-cfg->world_extent, -cfg->world_extent, -cfg->world_extent};
    bounds.max = (phys_vec3_t){ cfg->world_extent,  cfg->world_extent,  cfg->world_extent};
    if (!npc_svo_grid_init(out_grid, bounds, cfg->max_depth)) return 0;

    uint32_t total = 0;
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
        total += rasterize_volume_(out_grid, xmin, xmax, ymin, ymax,
                                   r->floor_z, r->ceil_z,
                                   r->vertices, r->vertex_count);
    }
    for (uint32_t ci = 0; ci < layout->corridor_count; ci++) {
        const fr_corridor_def_t *c = &layout->corridors[ci];
        total += rasterize_line_(out_grid, c->from.x, c->from.y,
                                 c->to.x, c->to.y,
                                 c->width, c->floor_z, c->ceil_z);
    }
    for (uint32_t ri = 0; ri < layout->ramp_count; ri++) {
        const fr_ramp_def_t *r = &layout->ramps[ri];
        float lo = fminf(r->from.z, r->to.z), hi = fmaxf(r->from.z, r->to.z);
        total += rasterize_line_(out_grid, r->from.x, r->from.y,
                                 r->to.x, r->to.y, r->width, lo, hi);
    }
    return total;
}

uint32_t procgen_svo_build(npc_svo_grid_t *grid,
                           const fr_dungeon_layout_t *layout) {
    procgen_raster_config_t cfg;
    procgen_raster_config_default(&cfg);
    return procgen_svo_build_cfg(&cfg, layout, grid);
}

/* ═══════════════════════════════════════════════════════════════
   Greedy mesh generation from SVO
   ═══════════════════════════════════════════════════════════════ */

void procgen_mesh_init(procgen_mesh_t *m) {
    memset(m, 0, sizeof(*m));
}

void procgen_mesh_destroy(procgen_mesh_t *m) {
    free(m->vertices);
    memset(m, 0, sizeof(*m));
}

static void mesh_add_quad(procgen_mesh_t *m,
                          float x0, float y0, float z0,
                          float x1, float y1, float z1,
                          int axis) {
    /* Expand capacity if needed (6 floats per quad = 2 triangles * 3 verts * 3 coords / 2). */
    uint32_t needed = m->vertex_count + 18; /* 6 vertices * 3 floats */
    if (needed > m->vertex_cap) {
        uint32_t nc = m->vertex_cap ? m->vertex_cap * 2 : 1024;
        if (nc < needed) nc = needed;
        float *nd = realloc(m->vertices, nc * sizeof(float));
        if (!nd) return;
        m->vertices = nd;
        m->vertex_cap = nc;
    }

    float *v = m->vertices + m->vertex_count;
    /* Axis determines which face. 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z */
    switch (axis) {
    case 0: /* +X */ /* (x1,y0,z0) (x1,y1,z0) (x1,y1,z1) (x1,y0,z1) ccw */
        v[0]=x1;v[1]=y0;v[2]=z0; v[3]=x1;v[4]=y1;v[5]=z0;
        v[6]=x1;v[7]=y1;v[8]=z1; v[9]=x1;v[10]=y0;v[11]=z0;
        v[12]=x1;v[13]=y1;v[14]=z1; v[15]=x1;v[16]=y0;v[17]=z1;
        break;
    case 1: /* -X */
        v[0]=x0;v[1]=y1;v[2]=z0; v[3]=x0;v[4]=y0;v[5]=z0;
        v[6]=x0;v[7]=y0;v[8]=z1; v[9]=x0;v[10]=y1;v[11]=z0;
        v[12]=x0;v[13]=y0;v[14]=z1; v[15]=x0;v[16]=y1;v[17]=z1;
        break;
    case 2: /* +Y */
        v[0]=x1;v[1]=y1;v[2]=z0; v[3]=x0;v[4]=y1;v[5]=z0;
        v[6]=x0;v[7]=y1;v[8]=z1; v[9]=x1;v[10]=y1;v[11]=z0;
        v[12]=x0;v[13]=y1;v[14]=z1; v[15]=x1;v[16]=y1;v[17]=z1;
        break;
    case 3: /* -Y */
        v[0]=x0;v[1]=y0;v[2]=z0; v[3]=x1;v[4]=y0;v[5]=z0;
        v[6]=x1;v[7]=y0;v[8]=z1; v[9]=x0;v[10]=y0;v[11]=z0;
        v[12]=x1;v[13]=y0;v[14]=z1; v[15]=x0;v[16]=y0;v[17]=z1;
        break;
    case 4: /* +Z */
        v[0]=x1;v[1]=y0;v[2]=z1; v[3]=x0;v[4]=y0;v[5]=z1;
        v[6]=x0;v[7]=y1;v[8]=z1; v[9]=x1;v[10]=y0;v[11]=z1;
        v[12]=x0;v[13]=y1;v[14]=z1; v[15]=x1;v[16]=y1;v[17]=z1;
        break;
    case 5: /* -Z */
        v[0]=x0;v[1]=y0;v[2]=z0; v[3]=x1;v[4]=y0;v[5]=z0;
        v[6]=x1;v[7]=y1;v[8]=z0; v[9]=x0;v[10]=y0;v[11]=z0;
        v[12]=x1;v[13]=y1;v[14]=z0; v[15]=x0;v[16]=y1;v[17]=z0;
        break;
    }
    m->vertex_count += 18;
}

uint32_t procgen_mesh_from_svo(const npc_svo_grid_t *g, procgen_mesh_t *mesh) {
    uint32_t cells = 1u << g->max_depth;
    float vs = g->voxel_size;
    float ox = g->world_bounds.min.x, oy = g->world_bounds.min.y, oz = g->world_bounds.min.z;
    uint32_t tri_count = 0;

    for (uint32_t vz = 0; vz < cells; vz++) {
        for (uint32_t vy = 0; vy < cells; vy++) {
            for (uint32_t vx = 0; vx < cells; vx++) {
                if (!is_solid_(g, (int)vx, (int)vy, (int)vz)) continue;

                float x0 = ox + (float)vx * vs,  x1 = x0 + vs;
                float y0 = oy + (float)vy * vs,  y1 = y0 + vs;
                float z0 = oz + (float)vz * vs,  z1 = z0 + vs;

                /* Check 6 neighbors — emit face if neighbor is air/void. */
                if (!is_solid_(g, (int)vx+1, (int)vy,   (int)vz))   { mesh_add_quad(mesh, x0,y0,z0, x1,y1,z1, 0); tri_count+=2; }
                if (!is_solid_(g, (int)vx-1, (int)vy,   (int)vz))   { mesh_add_quad(mesh, x0,y0,z0, x1,y1,z1, 1); tri_count+=2; }
                if (!is_solid_(g, (int)vx,   (int)vy+1, (int)vz))   { mesh_add_quad(mesh, x0,y0,z0, x1,y1,z1, 2); tri_count+=2; }
                if (!is_solid_(g, (int)vx,   (int)vy-1, (int)vz))   { mesh_add_quad(mesh, x0,y0,z0, x1,y1,z1, 3); tri_count+=2; }
                if (!is_solid_(g, (int)vx,   (int)vy,   (int)vz+1)) { mesh_add_quad(mesh, x0,y0,z0, x1,y1,z1, 4); tri_count+=2; }
                if (!is_solid_(g, (int)vx,   (int)vy,   (int)vz-1)) { mesh_add_quad(mesh, x0,y0,z0, x1,y1,z1, 5); tri_count+=2; }
            }
        }
    }
    return tri_count;
}
