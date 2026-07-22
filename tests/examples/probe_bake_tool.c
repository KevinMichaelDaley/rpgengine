/**
 * @file probe_bake_tool.c
 * @brief Offline probe placement pass -- the second bake step (rpg-pjkb).
 *
 * Usage: probe_bake <level.scene>
 *
 * Reads the level descriptor for the baked-SDF prefix, <dir>/render.json
 * for the placement knobs (gi_brick_* / gi_fixup_*, engine defaults when the
 * file or keys are absent), samples the baked _cNNN.sdf chunks on the CPU, runs
 * ternary brick placement + virtual-offset fix-up, and writes the result to
 * <sdf_prefix>.probes -- which the client loads as the level's MANUAL probes
 * (the loader never re-places: at load time it only sees resident chunks).
 * Hand-placed extras can be appended to the same file afterwards.
 *
 * Headless: no GL, safe to run right after the baker in the export pipeline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_place.h"
#include "ferrum/scene/render_config.h"
#include "ferrum/scene/scene_desc.h"

static uint8_t g_desc_buf[8 * 1024 * 1024];
static uint8_t g_place_buf[512 * 1024 * 1024];

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <level.scene>\n", argv[0]);
        return 2;
    }
    /* base_dir = the level file's directory (relative paths resolve under it,
     * exactly as the client does). */
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", argv[1]);
    { char *slash = strrchr(dir, '/');
      if (slash != NULL) *slash = '\0'; else snprintf(dir, sizeof dir, "."); }
    char path[1024];

    /* Scene descriptor: the baked-SDF prefix lives in lightdata. */
    arena_t da; arena_init(&da, g_desc_buf, sizeof g_desc_buf);
    scene_desc_t desc;
    if (!scene_desc_load(argv[1], &da, &desc)) {
        fprintf(stderr, "probe_bake: cannot load %s\n", argv[1]);
        return 1;
    }
    if (desc.lightdata.sdf_prefix[0] == '\0') {
        fprintf(stderr, "probe_bake: %s has no lightdata.sdf_prefix (bake the SDF first)\n", argv[1]);
        return 1;
    }
    char sdf_prefix[1024];
    snprintf(sdf_prefix, sizeof sdf_prefix, "%s/%s", dir, desc.lightdata.sdf_prefix);

    /* Placement knobs (engine defaults unless render.json overrides). */
    render_config_t rc;
    snprintf(path, sizeof path, "%s/render.json", dir);
    if (!render_config_load(path, &da, &rc)) render_config_defaults(&rc);

    /* Baked field, CPU-side. The union of chunk boxes IS the placement AABB:
     * exactly the geometry the baked SDF knows about. */
    probe_chunk_sdf_t cs;
    if (!probe_chunk_sdf_open(sdf_prefix, &cs)) {
        fprintf(stderr, "probe_bake: no SDF chunks at %s_cNNN.sdf\n", sdf_prefix);
        return 1;
    }
    probe_brick_config_t brick;
    memset(&brick, 0, sizeof brick);
    /* Placement AABB = bounds of the GEOMETRY, not of the chunk tiling: chunks
     * pad far beyond the scene (fixed 128^3 tiles), and placing over the padding
     * put ~40% of the probe budget below the floor / above the roof / outside
     * the walls (measured on the great hall). Scan for near-surface voxels
     * (|d| <= 2 voxels) and bound those, plus a 1 m apron for the near band. */
    for (int a = 0; a < 3; ++a) { brick.aabb_min[a] = 1e30f; brick.aabb_max[a] = -1e30f; }
    for (uint32_t i = 0; i < cs.count; ++i) {
        const lm_sdf_data_t *c = &cs.chunks[i];
        for (int32_t vz = 0; vz < c->dims[2]; ++vz)
            for (int32_t vy = 0; vy < c->dims[1]; ++vy)
                for (int32_t vx = 0; vx < c->dims[0]; ++vx) {
                    float d = c->dist[((size_t)vz * c->dims[1] + vy) * c->dims[0] + vx];
                    if (d > 2.0f * c->voxel || d < -2.0f * c->voxel) continue;
                    float w[3] = { c->origin[0] + ((float)vx + 0.5f) * c->voxel,
                                   c->origin[1] + ((float)vy + 0.5f) * c->voxel,
                                   c->origin[2] + ((float)vz + 0.5f) * c->voxel };
                    for (int a = 0; a < 3; ++a) {
                        if (w[a] < brick.aabb_min[a]) brick.aabb_min[a] = w[a];
                        if (w[a] > brick.aabb_max[a]) brick.aabb_max[a] = w[a];
                    }
                }
    }
    if (brick.aabb_min[0] > brick.aabb_max[0]) {
        fprintf(stderr, "probe_bake: SDF has no surface voxels\n");
        probe_chunk_sdf_close(&cs);
        return 1;
    }
    for (int a = 0; a < 3; ++a) { brick.aabb_min[a] -= 1.0f; brick.aabb_max[a] += 1.0f; }
    brick.coarse_brick = rc.gi_brick_coarse > 0.0f ? rc.gi_brick_coarse : 9.0f;
    brick.levels = rc.gi_brick_levels;
    if (brick.levels < 1) brick.levels = 1;
    if (brick.levels > PROBE_BRICK_MAX_LEVELS) brick.levels = PROBE_BRICK_MAX_LEVELS;
    brick.fill_empty = rc.gi_brick_fill;
    brick.buried_frac = rc.gi_brick_buried;
    brick.sdf = probe_chunk_sdf_sample;
    brick.sdf_user = &cs;

    probe_fixup_config_t fix;
    memset(&fix, 0, sizeof fix);
    fix.clearance = rc.gi_fixup_clearance;
    fix.bias = 0.02f;
    fix.max_push = rc.gi_fixup_max_push;
    fix.sdf = probe_chunk_sdf_sample;
    fix.sdf_user = &cs;

    char out_path[1040], bricks_path[1040];
    snprintf(out_path, sizeof out_path, "%s.probes", sdf_prefix);
    snprintf(bricks_path, sizeof bricks_path, "%s.bricks", sdf_prefix);
    arena_t pa; arena_init(&pa, g_place_buf, sizeof g_place_buf);
    uint32_t n = 0;
    bool ok = probe_bake_place_run(&brick, fix.clearance > 0.0f ? &fix : NULL,
                                   &pa, out_path, bricks_path, &n);
    probe_chunk_sdf_close(&cs);
    if (!ok) {
        fprintf(stderr, "probe_bake: placement failed (arena or IO)\n");
        return 1;
    }
    printf("probe_bake: %u probes (coarse %.1f m, %d levels, clearance %.2f m,"
           " aabb %.1fx%.1fx%.1f m) -> %s (+ .bricks)\n",
           n, (double)brick.coarse_brick, brick.levels, (double)fix.clearance,
           (double)(brick.aabb_max[0] - brick.aabb_min[0]),
           (double)(brick.aabb_max[1] - brick.aabb_min[1]),
           (double)(brick.aabb_max[2] - brick.aabb_min[2]), out_path);
    return 0;
}
