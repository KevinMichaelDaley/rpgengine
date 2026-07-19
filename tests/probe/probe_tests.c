/**
 * @file probe_tests.c
 * @brief Unit + regression tests for the headless probe placement + .probes file
 *        module (rpg-ft0g). Written before the implementation (TDD phase 1).
 *
 * Coverage: default auto-grid reproduces the hall_lit_dynamic layout, grid index
 * ordering, importance-box densification (distance/LOD resolution), chunk-box
 * gating, and .probes save/load round-trips (positions, grid params, baked SH),
 * plus IO failure modes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/memory/arena.h"
#include "ferrum/scene/scene_desc_probes.h"
#include "ferrum/probe/probe_set.h"
#include "ferrum/probe/probe_place.h"
#include "ferrum/probe/probe_file.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(e) ASSERT_TRUE(!(e))
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)
#define ASSERT_FLT_EQ(a,b) do { double _a=(double)(a),_b=(double)(b); if (fabs(_a-_b)>1e-3) { \
    fprintf(stderr,"  ASSERT_FLT_EQ failed: %g != %g (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

static uint8_t g_buf[8 * 1024 * 1024];

/* great_hall scene AABB (from the exporter: scene AABB print). */
static const float GH_MIN[3] = { -0.5f, -0.2f, -4.64f };
static const float GH_MAX[3] = { 18.5f, 11.7f,  4.64f };

static int test_grid_matches_great_hall(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 1.1f; spec.vspacing = 0.8f;
    probe_set_t s;
    ASSERT_TRUE(probe_place_grid(&spec, GH_MIN, GH_MAX, &a, &s));
    /* Same 16x8x8 = 1024 layout hall_lit_dynamic produces at GI_PSPACE=1.1/0.8. */
    ASSERT_INT_EQ(s.grid_dim[0], 16);
    ASSERT_INT_EQ(s.grid_dim[1], 8);
    ASSERT_INT_EQ(s.grid_dim[2], 8);
    ASSERT_INT_EQ(s.count, 1024);
    ASSERT_FLT_EQ(s.grid_origin[0], -0.5f + 19.0f * 0.04f); /* 0.26 */
    ASSERT_FLT_EQ(s.grid_cell[0], 19.0f * 0.92f / 15.0f);   /* ~1.165 */
    ASSERT_TRUE(s.positions != NULL);
    return 0;
}

static int test_grid_index_order(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 1.1f; spec.vspacing = 0.8f;
    probe_set_t s;
    ASSERT_TRUE(probe_place_grid(&spec, GH_MIN, GH_MAX, &a, &s));
    /* Index (z*dim1 + y)*dim0 + x -> reconstruct probe (2,1,3) position. */
    int x = 2, y = 1, z = 3;
    int idx = (z * s.grid_dim[1] + y) * s.grid_dim[0] + x;
    ASSERT_FLT_EQ(s.positions[idx * 3 + 0], s.grid_origin[0] + s.grid_cell[0] * x);
    ASSERT_FLT_EQ(s.positions[idx * 3 + 1], s.grid_origin[1] + s.grid_cell[1] * y);
    ASSERT_FLT_EQ(s.positions[idx * 3 + 2], s.grid_origin[2] + s.grid_cell[2] * z);
    return 0;
}

static int test_grid_default_spacing(void)
{
    /* No spec (spacing<=0) -> engine defaults, still a valid grid, not a crash. */
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_set_t s;
    ASSERT_TRUE(probe_place_grid(NULL, GH_MIN, GH_MAX, &a, &s));
    ASSERT_TRUE(s.grid_dim[0] >= 2 && s.grid_dim[1] >= 2 && s.grid_dim[2] >= 2);
    ASSERT_INT_EQ(s.count, (uint32_t)s.grid_dim[0] * s.grid_dim[1] * s.grid_dim[2]);
    return 0;
}

static int point_in_box(const float *p, const float *lo, const float *hi)
{
    return p[0] >= lo[0] && p[0] <= hi[0] && p[1] >= lo[1] && p[1] <= hi[1] &&
           p[2] >= lo[2] && p[2] <= hi[2];
}

static int test_importance_densify(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 1.1f; spec.vspacing = 0.8f;
    /* One importance box in the hall centre, 2x density. */
    spec.box_count = 1;
    spec.boxes[0].min[0] = 6; spec.boxes[0].min[1] = 0; spec.boxes[0].min[2] = -2;
    spec.boxes[0].max[0] = 12; spec.boxes[0].max[1] = 3; spec.boxes[0].max[2] = 2;
    spec.boxes[0].density_mult = 2.0f;
    spec.boxes[0].priority_bias = 1.0f;

    probe_set_t base;
    ASSERT_TRUE(probe_place_grid(&spec, GH_MIN, GH_MAX, &a, &base));
    probe_set_t ref;
    ASSERT_TRUE(probe_place_refine_importance(&base, &spec, GH_MIN, GH_MAX, &a, &ref));
    /* Densifying must ADD probes; the result is a point set (no single grid). */
    ASSERT_TRUE(ref.count > base.count);
    ASSERT_INT_EQ(ref.grid_dim[0], 0);
    /* At least one added probe must lie inside the importance box. */
    int inside = 0;
    for (uint32_t i = 0; i < ref.count; ++i) {
        if (point_in_box(&ref.positions[i * 3], spec.boxes[0].min, spec.boxes[0].max)) {
            inside++;
        }
    }
    /* Denser box => more probes inside than the base grid had there. */
    int base_inside = 0;
    for (uint32_t i = 0; i < base.count; ++i)
        if (point_in_box(&base.positions[i * 3], spec.boxes[0].min, spec.boxes[0].max))
            base_inside++;
    ASSERT_TRUE(inside > base_inside);
    return 0;
}

static int test_chunk_filter(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 1.1f; spec.vspacing = 0.8f;
    probe_set_t s;
    ASSERT_TRUE(probe_place_grid(&spec, GH_MIN, GH_MAX, &a, &s));

    /* One resident chunk covering the +x half of the hall. */
    float cmin[3] = { 9.0f, -1.0f, -5.0f };
    float cmax[3] = { 19.0f, 12.0f, 5.0f };
    probe_set_t kept;
    uint32_t n = probe_place_filter_chunks(&s, cmin, cmax, 1, &a, &kept);
    ASSERT_TRUE(n > 0 && n < s.count);      /* some kept, some dropped */
    ASSERT_INT_EQ(kept.count, n);
    for (uint32_t i = 0; i < kept.count; ++i)
        ASSERT_TRUE(point_in_box(&kept.positions[i * 3], cmin, cmax));
    return 0;
}

static int test_chunk_filter_empty(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 1.1f; spec.vspacing = 0.8f;
    probe_set_t s;
    ASSERT_TRUE(probe_place_grid(&spec, GH_MIN, GH_MAX, &a, &s));
    /* Chunk far away: nothing resident. */
    float cmin[3] = { 1000, 1000, 1000 }, cmax[3] = { 1001, 1001, 1001 };
    probe_set_t kept;
    uint32_t n = probe_place_filter_chunks(&s, cmin, cmax, 1, &a, &kept);
    ASSERT_INT_EQ(n, 0);
    ASSERT_INT_EQ(kept.count, 0);
    return 0;
}

static int test_probes_file_roundtrip(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    scene_desc_probes_t spec; memset(&spec, 0, sizeof spec);
    spec.spacing = 1.1f; spec.vspacing = 0.8f;
    probe_set_t s;
    ASSERT_TRUE(probe_place_grid(&spec, GH_MIN, GH_MAX, &a, &s));

    const char *path = "/tmp/probe_rt.probes";
    ASSERT_TRUE(probe_set_save(path, &s));

    arena_t a2; static uint8_t buf2[8 * 1024 * 1024];
    arena_init(&a2, buf2, sizeof buf2);
    probe_set_t r;
    ASSERT_TRUE(probe_set_load(path, &a2, &r));
    ASSERT_INT_EQ(r.count, s.count);
    ASSERT_INT_EQ(r.grid_dim[0], s.grid_dim[0]);
    ASSERT_INT_EQ(r.grid_dim[1], s.grid_dim[1]);
    ASSERT_INT_EQ(r.grid_dim[2], s.grid_dim[2]);
    ASSERT_FLT_EQ(r.grid_origin[0], s.grid_origin[0]);
    ASSERT_FLT_EQ(r.grid_cell[2], s.grid_cell[2]);
    ASSERT_INT_EQ(r.sh_coeffs, 0);
    for (uint32_t i = 0; i < s.count * 3; ++i)
        ASSERT_FLT_EQ(r.positions[i], s.positions[i]);
    remove(path);
    return 0;
}

static int test_probes_file_sh_roundtrip(void)
{
    /* A manual set carrying baked SH (12 floats/probe = SH4 rgb). */
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_set_t s; memset(&s, 0, sizeof s);
    s.count = 3;
    s.positions = arena_alloc(&a, 16, s.count * 3 * sizeof(float));
    s.sh_coeffs = 12;
    s.sh = arena_alloc(&a, 16, s.count * s.sh_coeffs * sizeof(float));
    ASSERT_TRUE(s.positions && s.sh);
    for (uint32_t i = 0; i < s.count * 3; ++i) s.positions[i] = (float)i * 0.5f;
    for (uint32_t i = 0; i < s.count * s.sh_coeffs; ++i) s.sh[i] = (float)i - 7.0f;

    const char *path = "/tmp/probe_sh.probes";
    ASSERT_TRUE(probe_set_save(path, &s));
    arena_t a2; static uint8_t buf2[1024 * 1024];
    arena_init(&a2, buf2, sizeof buf2);
    probe_set_t r;
    ASSERT_TRUE(probe_set_load(path, &a2, &r));
    ASSERT_INT_EQ(r.count, 3);
    ASSERT_INT_EQ(r.sh_coeffs, 12);
    ASSERT_TRUE(r.sh != NULL);
    for (uint32_t i = 0; i < r.count * r.sh_coeffs; ++i)
        ASSERT_FLT_EQ(r.sh[i], s.sh[i]);
    remove(path);
    return 0;
}

static int test_file_io_failures(void)
{
    arena_t a; arena_init(&a, g_buf, sizeof g_buf);
    probe_set_t r;
    /* Load a nonexistent file. */
    ASSERT_FALSE(probe_set_load("/tmp/does_not_exist_xyz.probes", &a, &r));
    /* Save to an unwritable path. */
    probe_set_t s; memset(&s, 0, sizeof s);
    float p[3] = { 0, 0, 0 }; s.count = 1; s.positions = p;
    ASSERT_FALSE(probe_set_save("/nonexistent_dir_xyz/x.probes", &s));
    /* Load a file with a bad magic. */
    FILE *f = fopen("/tmp/bad.probes", "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("XXXX0000", 1, 8, f); fclose(f);
    ASSERT_FALSE(probe_set_load("/tmp/bad.probes", &a, &r));
    remove("/tmp/bad.probes");
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"grid_matches_great_hall", test_grid_matches_great_hall},
        {"grid_index_order",        test_grid_index_order},
        {"grid_default_spacing",    test_grid_default_spacing},
        {"importance_densify",      test_importance_densify},
        {"chunk_filter",            test_chunk_filter},
        {"chunk_filter_empty",      test_chunk_filter_empty},
        {"probes_file_roundtrip",   test_probes_file_roundtrip},
        {"probes_file_sh_roundtrip",test_probes_file_sh_roundtrip},
        {"file_io_failures",        test_file_io_failures},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\nprobe_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
