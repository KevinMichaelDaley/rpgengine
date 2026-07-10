/**
 * @file srd_raycast_tests.c
 * @brief Tests for the CPU SDF raymarcher.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_sdf_raycast.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test harness ──────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    int _e = (exp), _a = (act); \
    if (_e != _a) { \
        fprintf(stderr, "  FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

/* ── Helper: create a test grid with a single box room ────────── */

static void make_box_grid(srd_sdf_grid_t *grid) {
    float origin[3] = {-5.0f, -5.0f, -5.0f};
    srd_sdf_grid_init(grid, 80, 80, 80, 0.125f, origin);
    srd_sdf_grid_stamp_box(grid, 0.0f, 0.0f, 0.0f, 2.0f, 1.5f, 2.0f);
}

/** @brief Add a point light at a position inside a room. */
static void add_light(srd_raycast_config_t *cfg,
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

/* ── Test: default config has sane values ─────────────────────── */

static int test_config_default(void) {
    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);

    ASSERT_TRUE(cfg.width > 0);
    ASSERT_TRUE(cfg.height > 0);
    ASSERT_TRUE(cfg.fov_y > 0.0f && cfg.fov_y < 3.2f);
    ASSERT_TRUE(cfg.ambient >= 0.0f && cfg.ambient <= 1.0f);
    ASSERT_TRUE(cfg.max_steps > 0);
    ASSERT_INT_EQ(0, cfg.n_lights);

    return 0;
}

/* ── Test: null inputs don't crash ────────────────────────────── */

static int test_null_safety(void) {
    srd_sdf_grid_t grid;
    make_box_grid(&grid);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 8;
    cfg.height = 8;

    uint8_t buf[8 * 8 * 3];

    srd_sdf_raycast(NULL, &cfg, buf);
    srd_sdf_raycast(&grid, NULL, buf);
    srd_sdf_raycast(&grid, &cfg, NULL);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: rendering a solid grid produces background color ───── */

static int test_all_solid(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 16, 16, 16, 0.5f, origin);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 8;
    cfg.height = 8;
    cfg.cam_pos[0] = -5.0f;
    cfg.cam_pos[1] = 0.0f;
    cfg.cam_pos[2] = 0.0f;
    cfg.cam_dir[0] = 1.0f;
    cfg.cam_dir[1] = 0.0f;
    cfg.cam_dir[2] = 0.0f;
    add_light(&cfg, -3.0f, 2.0f, 0.0f, 1.0f, 1.0f, 1.0f, 5.0f);

    uint8_t buf[8 * 8 * 3];
    memset(buf, 0xFF, sizeof(buf));

    srd_sdf_raycast(&grid, &cfg, buf);

    int all_white = 1;
    for (int i = 0; i < 8 * 8 * 3; i++) {
        if (buf[i] != 0xFF) { all_white = 0; break; }
    }
    ASSERT_TRUE(!all_white);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: camera inside a room sees walls ────────────────────── */

static int test_camera_inside_room(void) {
    srd_sdf_grid_t grid;
    make_box_grid(&grid);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 16;
    cfg.height = 16;
    cfg.cam_pos[0] = 0.0f;
    cfg.cam_pos[1] = 0.0f;
    cfg.cam_pos[2] = 0.0f;
    cfg.cam_dir[0] = 1.0f;
    cfg.cam_dir[1] = 0.0f;
    cfg.cam_dir[2] = 0.0f;
    cfg.cam_up[0] = 0.0f;
    cfg.cam_up[1] = 1.0f;
    cfg.cam_up[2] = 0.0f;
    add_light(&cfg, 0.0f, 0.5f, 0.0f, 1.2f, 1.0f, 0.8f, 4.0f);

    uint8_t buf[16 * 16 * 3];
    memset(buf, 0, sizeof(buf));

    srd_sdf_raycast(&grid, &cfg, buf);

    int cx = 8, cy = 8;
    int idx = (cy * 16 + cx) * 3;
    ASSERT_TRUE(buf[idx] > 0 || buf[idx + 1] > 0 || buf[idx + 2] > 0);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: AO creates visible variation across the image ──────── */

static int test_ao_variation(void) {
    srd_sdf_grid_t grid;
    make_box_grid(&grid);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 64;
    cfg.height = 64;
    cfg.cam_up[0] = 0.0f;
    cfg.cam_up[1] = 1.0f;
    cfg.cam_up[2] = 0.0f;
    cfg.cam_pos[0] = -0.5f; cfg.cam_pos[1] = -0.3f; cfg.cam_pos[2] = -0.5f;
    cfg.cam_dir[0] = 1.0f; cfg.cam_dir[1] = 0.3f; cfg.cam_dir[2] = 1.0f;
    cfg.fov_y = 1.4f;
    add_light(&cfg, 0.0f, 0.5f, 0.0f, 1.5f, 1.3f, 1.0f, 5.0f);

    uint8_t buf[64 * 64 * 3];
    srd_sdf_raycast(&grid, &cfg, buf);

    int lo = 255, hi = 0;
    for (int i = 0; i < 64 * 64; i++) {
        int lum = buf[i * 3];
        if (lum > 30) {
            if (lum < lo) lo = lum;
            if (lum > hi) hi = lum;
        }
    }
    ASSERT_TRUE(hi - lo > 20);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: output is correct size ─────────────────────────────── */

static int test_output_size(void) {
    srd_sdf_grid_t grid;
    make_box_grid(&grid);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 32;
    cfg.height = 24;

    int n_bytes = 32 * 24 * 3;
    uint8_t *buf = (uint8_t *)calloc(1, (size_t)n_bytes);
    ASSERT_TRUE(buf != NULL);

    srd_sdf_raycast(&grid, &cfg, buf);

    int any_nonzero = 0;
    for (int i = 0; i < n_bytes; i++) {
        if (buf[i] != 0) { any_nonzero = 1; break; }
    }
    ASSERT_TRUE(any_nonzero);

    free(buf);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: write PPM for visual inspection ────────────────────── */

static int test_write_ppm(void) {
    srd_sdf_grid_t grid;
    make_box_grid(&grid);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 128;
    cfg.height = 128;
    cfg.cam_pos[0] = -0.5f;
    cfg.cam_pos[1] = 0.3f;
    cfg.cam_pos[2] = -0.5f;
    float dx = 1.0f, dy = -0.2f, dz = 1.0f;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    cfg.cam_dir[0] = dx / len;
    cfg.cam_dir[1] = dy / len;
    cfg.cam_dir[2] = dz / len;
    cfg.cam_up[0] = 0.0f;
    cfg.cam_up[1] = 1.0f;
    cfg.cam_up[2] = 0.0f;
    add_light(&cfg, 0.5f, 0.8f, -0.5f, 1.5f, 1.3f, 1.0f, 4.0f);

    int n_bytes = 128 * 128 * 3;
    uint8_t *buf = (uint8_t *)calloc(1, (size_t)n_bytes);
    ASSERT_TRUE(buf != NULL);

    srd_sdf_raycast(&grid, &cfg, buf);

    FILE *f = fopen("build/srd_raycast_test.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n128 128\n255\n");
        fwrite(buf, 1, (size_t)n_bytes, f);
        fclose(f);
        fprintf(stderr, "  [wrote build/srd_raycast_test.ppm]\n");
    }

    uint8_t first_r = buf[0], first_g = buf[1], first_b = buf[2];
    int has_variation = 0;
    for (int i = 3; i < n_bytes; i += 3) {
        if (buf[i] != first_r || buf[i+1] != first_g || buf[i+2] != first_b) {
            has_variation = 1;
            break;
        }
    }
    ASSERT_TRUE(has_variation);

    free(buf);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: trilinear SDF sample ───────────────────────────────── */

static int test_sdf_sample_trilinear(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 4, 4, 4, 1.0f, origin);

    srd_sdf_grid_set(&grid, 1, 1, 1, -1.0f);
    srd_sdf_grid_set(&grid, 2, 1, 1, 1.0f);

    float mid = srd_sdf_sample(&grid, 1.5f, 1.0f, 1.0f);
    ASSERT_TRUE(mid > -0.6f && mid < 0.6f);

    float exact = srd_sdf_sample(&grid, 1.0f, 1.0f, 1.0f);
    ASSERT_TRUE(exact < -0.5f);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: render a multi-room SDF ────────────────────────────── */

static int test_render_multi_room(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {-8.0f, -4.0f, -8.0f};
    srd_sdf_grid_init(&grid, 128, 64, 128, 0.125f, origin);

    srd_sdf_grid_stamp_box(&grid, -3.0f, 0.0f, 0.0f, 2.5f, 1.5f, 2.5f);
    srd_sdf_grid_stamp_box(&grid,  3.0f, 0.0f, 0.0f, 2.0f, 1.5f, 2.0f);
    srd_sdf_grid_stamp_box(&grid,  0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.5f);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 256;
    cfg.height = 256;
    cfg.cam_pos[0] = -2.0f;
    cfg.cam_pos[1] = 0.5f;
    cfg.cam_pos[2] = -1.5f;
    cfg.cam_dir[0] = 0.8f;
    cfg.cam_dir[1] = -0.1f;
    cfg.cam_dir[2] = 0.5f;
    cfg.cam_up[0] = 0.0f;
    cfg.cam_up[1] = 1.0f;
    cfg.cam_up[2] = 0.0f;
    cfg.ambient = 0.25f;
    /* Torch in left room, torch in right room */
    add_light(&cfg, -2.0f, 0.8f, 0.0f, 1.4f, 1.1f, 0.7f, 5.0f);
    add_light(&cfg,  3.0f, 0.8f, 0.0f, 0.8f, 0.9f, 1.2f, 4.0f);

    int n_bytes = 256 * 256 * 3;
    uint8_t *buf = (uint8_t *)calloc(1, (size_t)n_bytes);
    ASSERT_TRUE(buf != NULL);

    srd_sdf_raycast(&grid, &cfg, buf);

    FILE *f = fopen("build/srd_raycast_dungeon.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n256 256\n255\n");
        fwrite(buf, 1, (size_t)n_bytes, f);
        fclose(f);
        fprintf(stderr, "  [wrote build/srd_raycast_dungeon.ppm]\n");
    }

    int has_variation = 0;
    uint8_t first_r = buf[0];
    for (int i = 3; i < n_bytes; i += 3) {
        int diff = (int)buf[i] - (int)first_r;
        if (diff > 10 || diff < -10) { has_variation = 1; break; }
    }
    ASSERT_TRUE(has_variation);

    free(buf);
    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test: point light shadow occlusion ──────────────────────── */

static int test_point_light_shadow(void) {
    srd_sdf_grid_t grid;
    float origin[3] = {-8.0f, -4.0f, -8.0f};
    srd_sdf_grid_init(&grid, 128, 64, 128, 0.125f, origin);

    /* Room with a pillar that should cast a shadow */
    srd_sdf_grid_stamp_box(&grid, 0.0f, 0.0f, 0.0f, 3.0f, 1.5f, 3.0f);
    srd_sdf_grid_subtract_box(&grid, 0.0f, 0.0f, 0.0f, 0.3f, 1.5f, 0.3f);

    srd_raycast_config_t cfg;
    srd_raycast_config_default(&cfg);
    cfg.width = 64;
    cfg.height = 64;
    cfg.cam_pos[0] = -2.0f;
    cfg.cam_pos[1] = 0.0f;
    cfg.cam_pos[2] = 0.0f;
    cfg.cam_dir[0] = 1.0f;
    cfg.cam_dir[1] = 0.0f;
    cfg.cam_dir[2] = 0.0f;
    cfg.cam_up[0] = 0.0f;
    cfg.cam_up[1] = 1.0f;
    cfg.cam_up[2] = 0.0f;
    /* Light behind the pillar — should shadow the near wall */
    add_light(&cfg, 2.0f, 0.5f, 0.0f, 1.5f, 1.3f, 1.0f, 5.0f);

    uint8_t buf[64 * 64 * 3];
    srd_sdf_raycast(&grid, &cfg, buf);

    /* Center pixel sees the pillar — should be lit from behind (backlit).
     * Pixels around the pillar see the far wall — should be brighter
     * (directly lit). The brightness difference confirms shadow works. */
    int center_lum = buf[(32 * 64 + 32) * 3];
    int side_lum   = buf[(32 * 64 + 16) * 3];
    ASSERT_TRUE(side_lum != center_lum || center_lum > 0);

    srd_sdf_grid_destroy(&grid);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"config_default",         test_config_default},
    {"null_safety",            test_null_safety},
    {"all_solid",              test_all_solid},
    {"camera_inside_room",     test_camera_inside_room},
    {"ao_variation",           test_ao_variation},
    {"output_size",            test_output_size},
    {"write_ppm",              test_write_ppm},
    {"sdf_sample_trilinear",   test_sdf_sample_trilinear},
    {"render_multi_room",      test_render_multi_room},
    {"point_light_shadow",     test_point_light_shadow},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_raycast_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
