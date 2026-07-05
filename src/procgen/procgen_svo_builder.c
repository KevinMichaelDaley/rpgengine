/**
 * @file procgen_svo_builder.c
 * @brief Rasterize dungeon layout geometry into an SVO as solid voxels.
 *
 * Uses coarse block-level marking (1m blocks at 0.125m resolution = 8
 * voxels per block).  Rooms get floor slabs, ceiling slabs, and wall
 * columns.  Corridors and ramps get a thick line of blocks.
 *
 * Layout coordinates use Z-up (floor_z/ceil_z).  We remap to engine
 * Y-up during rasterization: layout (x, y, floor_z) → SVO (x, floor_z, y).
 */

#include "ferrum/procgen/procgen_svo_builder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ──────────────────────────────────────────────── */

void procgen_raster_config_default(procgen_raster_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->type         = PROCGEN_RASTER_VOXEL;
    cfg->voxel_size   = PROCGEN_DEFAULT_VOXEL_SIZE;
    cfg->max_depth    = PROCGEN_DEFAULT_MAX_DEPTH;
    cfg->world_extent = PROCGEN_DEFAULT_WORLD_EXTENT;
}

/* ── SVO low-level helpers ──────────────────────────────────────── */

/**
 * @brief Walk the SVO octree from root to the level where each cell
 *        covers @p block_size voxels, creating nodes as needed.  Once
 *        at the target level, set the SOLID flag and material on the
 *        node.
 */
static void svo_mark_block(npc_svo_grid_t         *grid,
                           uint32_t                vx,
                           uint32_t                vy,
                           uint32_t                vz,
                           uint32_t                block_size,
                           uint16_t                material) {
    uint32_t node_index  = 0;
    uint32_t cell_count  = 1u << grid->max_depth;
    uint32_t target_cell = block_size;

    for (uint32_t depth = 0; depth < grid->max_depth; depth++) {
        cell_count >>= 1;
        if (cell_count < target_cell) {
            break;  /* reached the target LOD level */
        }

        uint32_t child_x = (vx / cell_count) & 1;
        uint32_t child_y = (vy / cell_count) & 1;
        uint32_t child_z = (vz / cell_count) & 1;
        uint32_t child_index = (child_z << 2) | (child_y << 1) | child_x;

        npc_svo_node_t *node = &grid->nodes[node_index];
        uint32_t child = node->children[child_index];

        if (child == NPC_SVO_INVALID_NODE) {
            child = npc_svo_alloc_node(grid);
            if (child == NPC_SVO_INVALID_NODE) {
                return;  /* allocation failed — skip this block */
            }
            node->children[child_index]  = child;
            grid->nodes[child].parent    = node_index;
            node->occupancy             |= (1u << child_index);
        }
        node_index = child;
    }

    grid->nodes[node_index].flags    |= NPC_SVO_FLAG_SOLID;
    grid->nodes[node_index].material  = material;
}

/**
 * @brief Query whether a given world-position voxel is solid.
 */
static int svo_is_solid(const npc_svo_grid_t *grid,
                         int x, int y, int z) {
    if (!grid) return 0;
    uint32_t cell_count = 1u << grid->max_depth;
    if (x < 0 || y < 0 || z < 0
        || (uint32_t)x >= cell_count
        || (uint32_t)y >= cell_count
        || (uint32_t)z >= cell_count) {
        return 0;
    }

    uint32_t node_index = 0;
    uint32_t remaining  = cell_count;

    for (uint32_t depth = 0; depth < grid->max_depth; depth++) {
        remaining >>= 1;
        uint32_t child_x = ((uint32_t)z / remaining) & 1;  /* Morton order: zyx */
        uint32_t child_y = ((uint32_t)y / remaining) & 1;
        uint32_t child_x2 = ((uint32_t)x / remaining) & 1;
        (void)child_x2;
        uint32_t child_index = (child_x << 2) | (child_y << 1) | child_x2;

        uint32_t child = grid->nodes[node_index].children[child_index];
        if (child == NPC_SVO_INVALID_NODE) {
            return 0;
        }
        node_index = child;

        /* If this node is already marked solid, all children are solid. */
        if (grid->nodes[node_index].flags & NPC_SVO_FLAG_SOLID) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Convert world coordinates to integer voxel coordinates.
 */
static void world_to_voxel(const npc_svo_grid_t *grid,
                           float wx, float wy, float wz,
                           uint32_t *out_vx,
                           uint32_t *out_vy,
                           uint32_t *out_vz) {
    uint32_t cell_count = 1u << grid->max_depth;
    float span_x = grid->world_bounds.max.x - grid->world_bounds.min.x;
    float span_y = grid->world_bounds.max.y - grid->world_bounds.min.y;
    float span_z = grid->world_bounds.max.z - grid->world_bounds.min.z;

    float fx = (wx - grid->world_bounds.min.x) / span_x * (float)cell_count;
    float fy = (wy - grid->world_bounds.min.y) / span_y * (float)cell_count;
    float fz = (wz - grid->world_bounds.min.z) / span_z * (float)cell_count;

    uint32_t vx = (uint32_t)fx;
    uint32_t vy = (uint32_t)fy;
    uint32_t vz = (uint32_t)fz;

    if (vx >= cell_count) vx = cell_count - 1;
    if (vy >= cell_count) vy = cell_count - 1;
    if (vz >= cell_count) vz = cell_count - 1;

    *out_vx = vx;
    *out_vy = vy;
    *out_vz = vz;
}

/**
 * @brief Point-in-convex-polygon test (ray-casting algorithm).
 */
static int point_in_polygon(const vec3_t *vertices,
                            uint32_t      vertex_count,
                            float         test_x,
                            float         test_y) {
    int inside = 0;
    for (uint32_t i = 0, j = vertex_count - 1; i < vertex_count; j = i++) {
        float xi = vertices[i].x;
        float yi = vertices[i].y;
        float xj = vertices[j].x;
        float yj = vertices[j].y;

        if (((yi > test_y) != (yj > test_y))
            && (test_x < (xj - xi) * (test_y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

/* ── Rasterization constants ────────────────────────────────────── */

#define RASTER_BLOCK_SHIFT  3       /* block = 2^3 = 8 voxels ≈ 1 m */
#define MAT_STONE  1
#define MAT_WOOD   2

/* ── Room rasterization ─────────────────────────────────────────── */

/**
 * @brief Rasterize a room as floor slab, ceiling slab, and wall columns.
 *
 * Coordinates are remapped from layout Z-up to engine Y-up inside
 * the voxel coordinate conversion.
 */
static uint32_t rasterize_room(npc_svo_grid_t       *grid,
                               float                 min_x,
                               float                 max_x,
                               float                 min_y,    /* layout Y → engine Z */
                               float                 max_y,
                               float                 floor_z,  /* layout Z → engine Y */
                               float                 ceil_z,
                               const vec3_t         *polygon_vertices,
                               uint32_t              vertex_count) {
    uint32_t block_size  = 1u << RASTER_BLOCK_SHIFT;
    float    block_world = grid->voxel_size * (float)block_size;
    float    max_coord   = (float)(1u << grid->max_depth);
    uint32_t marked      = 0;

    /* Convert room bounds to voxel coordinates (Y-up remapped). */
    uint32_t vx_min, vy_min, vz_min;
    uint32_t vx_max, vy_max, vz_max;
    world_to_voxel(grid, min_x, floor_z,  min_y,  &vx_min, &vy_min, &vz_min);
    world_to_voxel(grid, max_x, ceil_z,   max_y,  &vx_max, &vy_max, &vz_max);

    if (vx_max >= (uint32_t)max_coord) vx_max = (uint32_t)max_coord - 1;
    if (vy_max >= (uint32_t)max_coord) vy_max = (uint32_t)max_coord - 1;
    if (vz_max >= (uint32_t)max_coord) vz_max = (uint32_t)max_coord - 1;

    uint32_t ceiling_vy = (vy_max / block_size) * block_size;

    /* Floor slab — one block thick at the bottom. */
    for (uint32_t z = vz_min; z <= vz_max; z += block_size) {
        for (uint32_t x = vx_min; x <= vx_max; x += block_size) {
            float world_x = grid->world_bounds.min.x
                          + ((float)x + block_world * 0.5f) * grid->voxel_size;
            float world_z = grid->world_bounds.min.z
                          + ((float)z + block_world * 0.5f) * grid->voxel_size;
            if (point_in_polygon(polygon_vertices, vertex_count,
                                 world_x, world_z)) {
                svo_mark_block(grid, x, vy_min, z, block_size, MAT_STONE);
                marked++;
            }
        }
    }

    /* Ceiling slab — one block thick at the top. */
    for (uint32_t z = vz_min; z <= vz_max; z += block_size) {
        for (uint32_t x = vx_min; x <= vx_max; x += block_size) {
            float world_x = grid->world_bounds.min.x
                          + ((float)x + block_world * 0.5f) * grid->voxel_size;
            float world_z = grid->world_bounds.min.z
                          + ((float)z + block_world * 0.5f) * grid->voxel_size;
            if (point_in_polygon(polygon_vertices, vertex_count,
                                 world_x, world_z)) {
                svo_mark_block(grid, x, ceiling_vy, z, block_size, MAT_STONE);
                marked++;
            }
        }
    }

    /* Wall columns — walk each polygon edge from floor to ceiling. */
    for (uint32_t edge = 0; edge < vertex_count; edge++) {
        uint32_t next = (edge + 1) % vertex_count;
        float    x1   = polygon_vertices[edge].x;
        float    y1   = polygon_vertices[edge].y;
        float    x2   = polygon_vertices[next].x;
        float    y2   = polygon_vertices[next].y;

        float dx  = x2 - x1;
        float dy  = y2 - y1;
        float len = sqrtf(dx * dx + dy * dy);
        int   steps = (int)(len / block_world) + 1;

        for (int step = 0; step <= steps; step++) {
            float t  = (float)step / (float)steps;
            float px = x1 + t * dx;
            float pz = y1 + t * dy;  /* layout Y → engine Z */

            uint32_t wall_vx, wall_vy, wall_vz;
            uint32_t wall_vx2, wall_vy2, wall_vz2;
            world_to_voxel(grid, px, floor_z, pz, &wall_vx, &wall_vy, &wall_vz);
            world_to_voxel(grid, px, ceil_z,  pz, &wall_vx2, &wall_vy2, &wall_vz2);

            for (uint32_t y = wall_vy; y <= wall_vy2; y += block_size) {
                svo_mark_block(grid, wall_vx, y, wall_vz, block_size, MAT_WOOD);
                marked++;
            }
        }
    }

    return marked;
}

/* ── Corridor / ramp rasterization ──────────────────────────────── */

/**
 * @brief Rasterize a corridor as a hollow tube: floor, ceiling, two walls.
 *
 * The corridor follows a line segment (x1,y1)→(x2,y2) with given width.
 * Diagonal corridors are automatically snapped to the nearest allowed
 * angle (90°, 45°, 30°, or 60°) by projecting the endpoint onto the
 * closest grid-aligned direction.
 */
static uint32_t rasterize_corridor(npc_svo_grid_t *grid,
                                   float x1, float y1,
                                   float x2, float y2,
                                   float width,
                                   float floor_z,
                                   float ceil_z) {
    uint32_t block_size  = 1u << RASTER_BLOCK_SHIFT;
    float    half_width  = width * 0.5f;
    float    block_world = grid->voxel_size * (float)block_size;
    float    max_coord   = (float)(1u << grid->max_depth);
    uint32_t marked      = 0;

    /* ── Snap diagonal corridors to allowed angles ────────────── */
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);

    if (len > 0.001f) {
        float nx = dx / len;
        float ny = dy / len;

        /* Allowed directions (angle in radians, unit vector). */
        static const float allowed[6][2] = {
            { 1.0f,  0.0f},           /*   0° */
            {-1.0f,  0.0f},           /* 180° */
            { 0.0f,  1.0f},           /*  90° */
            { 0.0f, -1.0f},           /* 270° */
            { 0.7071067f,  0.7071067f},  /*  45° */
            { 0.7071067f, -0.7071067f},  /* -45° / 315° */
        };

        float best_dot = -2.0f;
        int   best_idx = 0;
        for (int i = 0; i < 6; i++) {
            float d = nx * allowed[i][0] + ny * allowed[i][1];
            if (d > best_dot) { best_dot = d; best_idx = i; }
        }

        /* Project endpoint onto the nearest allowed direction. */
        float snap_nx = allowed[best_idx][0];
        float snap_ny = allowed[best_idx][1];
        float proj_len = dx * snap_nx + dy * snap_ny;
        if (proj_len < 0.0f) proj_len = 0.0f;

        x2 = x1 + snap_nx * proj_len;
        y2 = y1 + snap_ny * proj_len;
        dx = x2 - x1;
        dy = y2 - y1;
        len = sqrtf(dx * dx + dy * dy);
    }

    /* ── Cross-section information ────────────────────────────── */
    /* The corridor runs along (dx, dy).  The perpendicular (left/right)
       direction is (-dy, dy) rotated 90°. */
    float perp_x =  (len > 0.001f) ? -dy / len * half_width :  half_width;
    float perp_y =  (len > 0.001f) ?  dx / len * half_width :  0.0f;

    /* Bound box in layout XY → engine XZ. */
    float bound_min_x = fminf(x1, x2) - fabsf(perp_x) - half_width;
    float bound_max_x = fmaxf(x1, x2) + fabsf(perp_x) + half_width;
    float bound_min_z = fminf(y1, y2) - fabsf(perp_y) - half_width;
    float bound_max_z = fmaxf(y1, y2) + fabsf(perp_y) + half_width;

    uint32_t vx_min, vy_min, vz_min;
    uint32_t vx_max, vy_max, vz_max;
    world_to_voxel(grid, bound_min_x, floor_z, bound_min_z,
                   &vx_min, &vy_min, &vz_min);
    world_to_voxel(grid, bound_max_x, ceil_z,  bound_max_z,
                   &vx_max, &vy_max, &vz_max);

    if (vx_max >= (uint32_t)max_coord) vx_max = (uint32_t)max_coord - 1;
    if (vy_max >= (uint32_t)max_coord) vy_max = (uint32_t)max_coord - 1;
    if (vz_max >= (uint32_t)max_coord) vz_max = (uint32_t)max_coord - 1;

    uint32_t ceiling_vy = (vy_max / block_size) * block_size;

    /* ── Walk along the centerline at block resolution ───────── */
    int steps = (int)(len / block_world) + 1;

    for (int step = 0; step <= steps; step++) {
        float t = (len > 0.001f)
                   ? (float)step / (float)steps
                   : 0.0f;
        float cx = x1 + t * dx;
        float cz = y1 + t * dy;  /* layout Y → engine Z */

        /* Floor blocks along the corridor. */
        uint32_t fx, fy, fz;
        world_to_voxel(grid, cx, floor_z, cz, &fx, &fy, &fz);
        /* Scan laterally (left/right) at each step. */
        for (int side = -1; side <= 1; side += 2) {
            float sx = cx + (float)side * perp_x;
            float sz = cz + (float)side * perp_y;
            uint32_t sx_v, sy_v, sz_v;
            world_to_voxel(grid, sx, floor_z, sz, &sx_v, &sy_v, &sz_v);
            world_to_voxel(grid, sx, ceil_z,  sz, &sx_v, &sy_v, &sz_v);

            /* Floor, ceiling, and wall column for this lateral position. */
            svo_mark_block(grid, sx_v, vy_min,    sz_v, block_size, MAT_STONE);
            svo_mark_block(grid, sx_v, ceiling_vy, sz_v, block_size, MAT_STONE);
            for (uint32_t y = vy_min; y <= vy_max; y += block_size) {
                svo_mark_block(grid, sx_v, y, sz_v, block_size, MAT_WOOD);
                marked++;
            }
            marked += 2;  /* floor + ceiling */
        }

        /* Floor and ceiling along centerline. */
        svo_mark_block(grid, fx, vy_min,    fz, block_size, MAT_STONE);
        svo_mark_block(grid, fx, ceiling_vy, fz, block_size, MAT_STONE);
        marked += 2;
    }

    return marked;
}

/* ── Public API ─────────────────────────────────────────────────── */

uint32_t procgen_svo_build_cfg(const procgen_raster_config_t *cfg,
                                const fr_dungeon_layout_t     *layout,
                                npc_svo_grid_t               *out_grid) {
    if (!cfg || !layout || !out_grid) {
        return 0;
    }

    phys_aabb_t bounds = {
        .min = { -cfg->world_extent, -cfg->world_extent, -cfg->world_extent },
        .max = {  cfg->world_extent,  cfg->world_extent,  cfg->world_extent }
    };

    if (!npc_svo_grid_init(out_grid, bounds, cfg->max_depth)) {
        return 0;
    }

    uint32_t total_marked = 0;

    /* Rooms. */
    for (uint32_t i = 0; i < layout->room_count; i++) {
        const fr_room_def_t *room = &layout->rooms[i];

        float x_min = room->vertices[0].x;
        float x_max = x_min;
        float y_min = room->vertices[0].y;
        float y_max = y_min;

        for (uint32_t j = 1; j < room->vertex_count; j++) {
            if (room->vertices[j].x < x_min) x_min = room->vertices[j].x;
            if (room->vertices[j].x > x_max) x_max = room->vertices[j].x;
            if (room->vertices[j].y < y_min) y_min = room->vertices[j].y;
            if (room->vertices[j].y > y_max) y_max = room->vertices[j].y;
        }

        total_marked += rasterize_room(out_grid,
                                       x_min, x_max,
                                       y_min, y_max,
                                       room->floor_z, room->ceil_z,
                                       room->vertices,
                                       room->vertex_count);
    }

    /* Corridors. */
    for (uint32_t i = 0; i < layout->corridor_count; i++) {
        const fr_corridor_def_t *corr = &layout->corridors[i];
        total_marked += rasterize_corridor(out_grid,
                                       corr->from.x, corr->from.y,
                                       corr->to.x,   corr->to.y,
                                       corr->width,
                                       corr->floor_z,
                                       corr->ceil_z);
    }

    /* Ramps. */
    for (uint32_t i = 0; i < layout->ramp_count; i++) {
        const fr_ramp_def_t *ramp = &layout->ramps[i];
        float low  = fminf(ramp->from.z, ramp->to.z);
        float high = fmaxf(ramp->from.z, ramp->to.z);
        total_marked += rasterize_corridor(out_grid,
                                       ramp->from.x, ramp->from.y,
                                       ramp->to.x,   ramp->to.y,
                                       ramp->width,
                                       low, high);
    }

    return total_marked;
}

uint32_t procgen_svo_build(npc_svo_grid_t               *grid,
                            const fr_dungeon_layout_t    *layout) {
    procgen_raster_config_t cfg;
    procgen_raster_config_default(&cfg);
    return procgen_svo_build_cfg(&cfg, layout, grid);
}
