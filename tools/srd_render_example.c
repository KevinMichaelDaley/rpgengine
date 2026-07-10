/**
 * @file srd_render_example.c
 * @brief Render a complex dungeon layout to srd_example_render.ppm.
 *
 * Uses 0.0125m voxels (10x finer than the default 0.125m) for smooth
 * surfaces and accurate pillar geometry. Grid covers the great hall
 * area; out-of-bounds stamps are clipped automatically.
 */
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_sdf_raycast.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void place_light(srd_raycast_config_t *cfg,
                        float x, float y, float z,
                        float r, float g, float b,
                        float radius) {
    if (cfg->n_lights >= SRD_MAX_LIGHTS) return;
    int i = cfg->n_lights++;
    cfg->lights[i].pos[0] = x;
    cfg->lights[i].pos[1] = y;
    cfg->lights[i].pos[2] = z;
    cfg->lights[i].color[0] = r;
    cfg->lights[i].color[1] = g;
    cfg->lights[i].color[2] = b;
    cfg->lights[i].radius = radius;
}

int main(void) {
    /* Great hall area: 11.2m x 5.6m x 8.8m at 0.0125m voxels (10x finer).
     * 896 x 448 x 704 = ~283M voxels ≈ 1.1 GB. */
    srd_sdf_grid_t grid;
    float origin[3] = {-5.6f, -2.8f, -4.4f};
    srd_sdf_grid_init(&grid, 896, 448, 704, 0.0125f, origin);
    fprintf(stderr, "Grid: %dx%dx%d @ %.4fm (%.0f MB)\n",
            896, 448, 704, 0.0125f,
            (float)(896 * 448 * 704) * 4.0f / (1024.0f * 1024.0f));

    /* ── Great hall ──────────────────────────────────────────── */
    fprintf(stderr, "Stamping geometry...\n");
    srd_sdf_grid_stamp_box(&grid, 0.0f, 0.0f, 0.0f, 5.0f, 2.5f, 4.0f);

    /* Pillars along the hall (4 per side) */
    for (int i = 0; i < 4; i++) {
        float z = -2.4f + (float)i * 1.6f;
        srd_sdf_grid_subtract_box(&grid, -3.5f, 0.0f, z, 0.25f, 2.5f, 0.25f);
        srd_sdf_grid_subtract_box(&grid,  3.5f, 0.0f, z, 0.25f, 2.5f, 0.25f);
    }

    /* Raised platform at far end of hall */
    srd_sdf_grid_subtract_box(&grid, 0.0f, -2.0f, 3.0f, 3.0f, 0.3f, 0.8f);

    /* ── Side chapel doorway (partially in grid) ─────────────── */
    srd_sdf_grid_stamp_box(&grid, -8.0f, 0.0f, 0.0f, 2.5f, 2.0f, 3.0f);
    srd_sdf_grid_stamp_box(&grid, -5.5f, -0.5f, 0.0f, 0.6f, 1.2f, 0.8f);

    /* ── Crypt passage entrance (partially in grid) ──────────── */
    srd_sdf_grid_stamp_box(&grid, 0.0f, -0.5f, 5.5f, 0.6f, 1.0f, 1.5f);

    /* ── South corridor entrance ─────────────────────────────── */
    srd_sdf_grid_stamp_box(&grid, 0.0f, -0.3f, -4.5f, 0.8f, 1.2f, 0.5f);

    fprintf(stderr, "Stamping done.\n");

    /* ── Render ──────────────────────────────────────────────── */
    int W = 1024, H = 512;
    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = W;
    cfg.height = H;
    cfg.max_steps = 512;

    cfg.cam_pos[0] = -3.0f;
    cfg.cam_pos[1] = -0.5f;
    cfg.cam_pos[2] = -2.5f;

    float dx = 3.5f, dy = 0.3f, dz = 3.5f;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    cfg.cam_dir[0] = dx / len;
    cfg.cam_dir[1] = dy / len;
    cfg.cam_dir[2] = dz / len;

    cfg.cam_up[0] = 0.0f;
    cfg.cam_up[1] = 1.0f;
    cfg.cam_up[2] = 0.0f;
    cfg.fov_y = 1.2f;
    cfg.ambient = 0.08f;
    cfg.hit_epsilon = 0.0005f;

    /* Torches: radius = luminous power, atten = radius / (d² + 1) */
    place_light(&cfg, -3.0f, 0.8f, -1.0f,  1.0f, 0.85f, 0.55f, 5.0f);
    place_light(&cfg,  3.0f, 0.8f,  1.0f,  1.0f, 0.80f, 0.50f, 4.0f);
    place_light(&cfg, -5.0f, 0.3f,  0.0f,  0.5f, 0.6f, 0.9f, 3.0f);
    place_light(&cfg,  0.0f, -0.8f, 3.0f,  0.9f, 0.6f, 0.3f, 3.0f);
    place_light(&cfg,  0.0f, 0.2f, -3.5f,  0.7f, 0.25f, 0.15f, 2.0f);

    int n_bytes = W * H * 3;
    uint8_t *buf = (uint8_t *)calloc(1, (size_t)n_bytes);
    if (!buf) { fprintf(stderr, "alloc failed\n"); return 1; }

    fprintf(stderr, "Rendering %dx%d with %d point lights (4x SSAA)...\n",
            W, H, cfg.n_lights);
    srd_sdf_raycast(&grid, &cfg, buf);

    FILE *f = fopen("srd_example_render.ppm", "wb");
    if (!f) { fprintf(stderr, "can't open output\n"); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(buf, 1, (size_t)n_bytes, f);
    fclose(f);
    fprintf(stderr, "Wrote srd_example_render.ppm\n");

    free(buf);
    srd_sdf_grid_destroy(&grid);
    return 0;
}
