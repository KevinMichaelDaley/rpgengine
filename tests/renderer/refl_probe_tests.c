/**
 * @file refl_probe_tests.c
 * @brief Unit tests for the sparse cubemap reflection probes (rpg-akwc):
 *        octahedral mapping, atlas layout, cube->octa resample, progressive
 *        prefilter, SDF-culled placement, specular-occlusion cone march,
 *        and the .rprobe file roundtrip. CPU only (the GL bake pass is
 *        covered by refl_probe_gl_tests.c under EGL).
 */
#define _DEFAULT_SOURCE 1
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/renderer/gi/refl_atlas.h"
#include "ferrum/renderer/gi/refl_file.h"
#include "ferrum/renderer/gi/refl_filter.h"
#include "ferrum/renderer/gi/refl_occl.h"
#include "ferrum/renderer/gi/refl_octa.h"
#include "ferrum/renderer/gi/refl_place.h"
#include "ferrum/renderer/gi/refl_probe.h"

#define ASSERT_TRUE(cond)                                                    \
    do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                               #cond); return 1; } } while (0)

/* ------------------------------------------------------------------ octa */

static int test_octa_roundtrip(void)
{
    const float dirs[][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 },
        { 0.577350f, 0.577350f, 0.577350f },
        { -0.267261f, 0.534522f, -0.801784f },
        { 0.1f, -0.2f, 0.97f },
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        float d[3] = { dirs[i][0], dirs[i][1], dirs[i][2] };
        float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        d[0] /= len; d[1] /= len; d[2] /= len;
        float uv[2], back[3];
        refl_octa_encode(d, uv);
        ASSERT_TRUE(uv[0] >= 0.0f && uv[0] <= 1.0f);
        ASSERT_TRUE(uv[1] >= 0.0f && uv[1] <= 1.0f);
        refl_octa_decode(uv, back);
        float dot = d[0]*back[0] + d[1]*back[1] + d[2]*back[2];
        ASSERT_TRUE(dot > 0.9999f);
    }
    return 0;
}

static int test_octa_degenerate(void)
{
    /* Zero vector must not NaN: encodes to SOME valid uv, decode is unit. */
    float d[3] = { 0, 0, 0 }, uv[2], back[3];
    refl_octa_encode(d, uv);
    ASSERT_TRUE(uv[0] == uv[0] && uv[1] == uv[1]);   /* not NaN */
    refl_octa_decode(uv, back);
    float len = sqrtf(back[0]*back[0] + back[1]*back[1] + back[2]*back[2]);
    ASSERT_TRUE(fabsf(len - 1.0f) < 1e-4f);
    /* Corners of the uv square decode to unit vectors too (the seam). */
    const float corners[][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 },
                                 { 0.5f, 0.0f }, { 0.0f, 0.5f } };
    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        float c[2] = { corners[i][0], corners[i][1] };
        refl_octa_decode(c, back);
        len = sqrtf(back[0]*back[0] + back[1]*back[1] + back[2]*back[2]);
        ASSERT_TRUE(fabsf(len - 1.0f) < 1e-4f);
    }
    return 0;
}

/* ----------------------------------------------------------------- atlas */

static int test_atlas_layout(void)
{
    refl_probe_set_t set;
    memset(&set, 0, sizeof set);
    set.tile_res = 32u;
    set.mips = 3u;
    set.tiles_x = 4u;
    set.tiles_y = 2u;

    uint32_t w, h;
    refl_atlas_dims(&set, 0, &w, &h);
    ASSERT_TRUE(w == 128u && h == 64u);
    refl_atlas_dims(&set, 2, &w, &h);
    ASSERT_TRUE(w == 32u && h == 16u);

    uint32_t x, y, r;
    ASSERT_TRUE(refl_atlas_tile_rect(&set, 0, 0, &x, &y, &r));
    ASSERT_TRUE(x == 0 && y == 0 && r == 32u);
    ASSERT_TRUE(refl_atlas_tile_rect(&set, 5, 0, &x, &y, &r));
    ASSERT_TRUE(x == 32u && y == 32u && r == 32u);        /* row-major */
    ASSERT_TRUE(refl_atlas_tile_rect(&set, 5, 2, &x, &y, &r));
    ASSERT_TRUE(x == 8u && y == 8u && r == 8u);           /* mip 2: /4 */
    /* tile out of range -> false. */
    ASSERT_TRUE(!refl_atlas_tile_rect(&set, 8, 0, &x, &y, &r));
    /* mip out of range -> false. */
    ASSERT_TRUE(!refl_atlas_tile_rect(&set, 0, 3, &x, &y, &r));
    return 0;
}

/* -------------------------------------------------------- cube resample */

static void fill_face(float *face, uint32_t res, float r, float g, float b,
                      float a)
{
    for (uint32_t i = 0; i < res * res; ++i) {
        face[i*4+0] = r; face[i*4+1] = g; face[i*4+2] = b; face[i*4+3] = a;
    }
}

static int test_cube_to_octa(void)
{
    enum { FR = 8, OR = 16 };
    static float faces[6][FR * FR * 4];
    static float octa[OR * OR * 4];
    /* GL face order: +x -x +y -y +z -z; tint each uniquely. */
    for (int f = 0; f < 6; ++f)
        fill_face(faces[f], FR, (float)(f == 0), (float)(f == 2),
                  (float)(f == 4), 1.0f);
    const float *fp[6] = { faces[0], faces[1], faces[2],
                           faces[3], faces[4], faces[5] };
    refl_octa_from_cube(fp, FR, octa, OR);

    /* The texel nearest +x must be red, nearest +y green, +z blue. */
    float uv[2];
    const float px[3] = { 1, 0, 0 };
    refl_octa_encode(px, uv);
    uint32_t tx = (uint32_t)(uv[0] * OR), ty = (uint32_t)(uv[1] * OR);
    if (tx >= OR) tx = OR - 1u;
    if (ty >= OR) ty = OR - 1u;
    const float *t = &octa[(ty * OR + tx) * 4];
    ASSERT_TRUE(t[0] > 0.9f && t[1] < 0.1f && t[2] < 0.1f);

    const float py[3] = { 0, 1, 0 };
    refl_octa_encode(py, uv);
    tx = (uint32_t)(uv[0] * OR); ty = (uint32_t)(uv[1] * OR);
    if (tx >= OR) tx = OR - 1u;
    if (ty >= OR) ty = OR - 1u;
    t = &octa[(ty * OR + tx) * 4];
    ASSERT_TRUE(t[1] > 0.9f && t[0] < 0.1f);

    const float pz[3] = { 0, 0, 1 };
    refl_octa_encode(pz, uv);
    tx = (uint32_t)(uv[0] * OR); ty = (uint32_t)(uv[1] * OR);
    if (tx >= OR) tx = OR - 1u;
    if (ty >= OR) ty = OR - 1u;
    t = &octa[(ty * OR + tx) * 4];
    ASSERT_TRUE(t[2] > 0.9f && t[0] < 0.1f);

    /* Constant cube -> constant octa (all channels + alpha preserved). */
    for (int f = 0; f < 6; ++f)
        fill_face(faces[f], FR, 0.25f, 0.5f, 0.75f, 0.6f);
    refl_octa_from_cube(fp, FR, octa, OR);
    for (uint32_t i = 0; i < OR * OR; ++i) {
        ASSERT_TRUE(fabsf(octa[i*4+0] - 0.25f) < 1e-4f);
        ASSERT_TRUE(fabsf(octa[i*4+1] - 0.50f) < 1e-4f);
        ASSERT_TRUE(fabsf(octa[i*4+2] - 0.75f) < 1e-4f);
        ASSERT_TRUE(fabsf(octa[i*4+3] - 0.60f) < 1e-4f);
    }
    return 0;
}

/* ---------------------------------------------------------------- filter */

static int test_filter_constant_invariant(void)
{
    enum { R = 16 };
    static float img[R * R * 4], tmp[R * R * 4], dst[(R/2) * (R/2) * 4];
    for (uint32_t i = 0; i < R * R; ++i) {
        img[i*4+0] = 0.3f; img[i*4+1] = 0.6f; img[i*4+2] = 0.9f;
        img[i*4+3] = 0.5f;
    }
    refl_filter_smooth(img, R, 8.0f, tmp);
    for (uint32_t i = 0; i < R * R; ++i) {
        ASSERT_TRUE(fabsf(img[i*4+0] - 0.3f) < 1e-3f);
        ASSERT_TRUE(fabsf(img[i*4+3] - 0.5f) < 1e-3f);
    }
    refl_filter_downsample(img, R, dst);
    for (uint32_t i = 0; i < (R/2) * (R/2); ++i) {
        ASSERT_TRUE(fabsf(dst[i*4+1] - 0.6f) < 1e-3f);
        ASSERT_TRUE(fabsf(dst[i*4+3] - 0.5f) < 1e-3f);
    }
    return 0;
}

static int test_filter_spreads_delta(void)
{
    enum { R = 16 };
    static float img[R * R * 4], tmp[R * R * 4];
    memset(img, 0, sizeof img);
    img[((R/2) * R + (R/2)) * 4 + 0] = 1.0f;      /* single bright texel */
    refl_filter_smooth(img, R, 8.0f, tmp);
    /* Peak drops, neighbours pick up energy. */
    float peak = img[((R/2) * R + (R/2)) * 4 + 0];
    float nb = img[((R/2) * R + (R/2 + 1)) * 4 + 0];
    ASSERT_TRUE(peak < 1.0f);
    ASSERT_TRUE(nb > 0.0f);
    /* A second, softer pass spreads further (progressive filtering). */
    float before = nb;
    refl_filter_smooth(img, R, 2.0f, tmp);
    float nb2 = img[((R/2) * R + (R/2 + 1)) * 4 + 0];
    ASSERT_TRUE(nb2 >= before * 0.5f);   /* still holds energy nearby */
    ASSERT_TRUE(img[((R/2) * R + (R/2)) * 4 + 0] < peak);
    return 0;
}

/* ------------------------------------------------------------- placement */

/* Synthetic SDF over a dims grid: distance to the plane z = 1 (positive
 * above), i.e. a floor slab filling z < 1. */
static void floor_sdf(float *dist, const int32_t dims[3],
                      const float origin[3], float voxel)
{
    for (int32_t z = 0; z < dims[2]; ++z)
        for (int32_t y = 0; y < dims[1]; ++y)
            for (int32_t x = 0; x < dims[0]; ++x) {
                float wz = origin[2] + ((float)z + 0.5f) * voxel;
                dist[(z * dims[1] + y) * dims[0] + x] = wz - 1.0f;
            }
}

static int test_place_grid_and_cull(void)
{
    enum { CAP = 64 };
    refl_probe_t probes[CAP];
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, CAP);

    float mn[3] = { 0, 0, 0 }, mx[3] = { 8, 8, 8 };
    /* No SDF: full 2x2x2 grid of cell centres. */
    uint32_t n = refl_place_grid(&set, mn, mx, 4.0f, NULL, NULL, NULL, 0.0f,
                                 0.5f);
    ASSERT_TRUE(n == 8u && set.count == 8u);
    ASSERT_TRUE(fabsf(set.probes[0].pos[0] - 2.0f) < 1e-5f);

    /* Floor slab z<1: clearance 0.5 keeps only probes with sdf > 0.5.
     * Cell centres sit at z=2 (sdf=1, keep) and z=6 (sdf=5, keep) -- all
     * clear. Shrink the box so centres land at z=0.5 (sdf=-0.5, culled). */
    int32_t dims[3] = { 16, 16, 16 };
    float origin[3] = { 0, 0, 0 };
    float voxel = 0.5f;
    static float dist[16 * 16 * 16];
    floor_sdf(dist, dims, origin, voxel);

    refl_probe_set_init(&set, probes, CAP);
    float mx2[3] = { 8, 8, 1 };     /* centres at z = 0.5: inside the slab */
    n = refl_place_grid(&set, mn, mx2, 4.0f, dist, dims, origin, voxel, 0.5f);
    ASSERT_TRUE(n == 0u);

    refl_probe_set_init(&set, probes, CAP);
    n = refl_place_grid(&set, mn, mx, 4.0f, dist, dims, origin, voxel, 0.5f);
    ASSERT_TRUE(n == 8u);            /* z=2 and z=6 layers both clear */

    /* Capacity clamp: 1-slot set stops at 1 without overflow. */
    refl_probe_set_init(&set, probes, 1u);
    n = refl_place_grid(&set, mn, mx, 4.0f, NULL, NULL, NULL, 0.0f, 0.5f);
    ASSERT_TRUE(n == 1u && set.count == 1u);
    return 0;
}

/* ----------------------------------------------------- specular occlusion */

static int test_occl_cone(void)
{
    int32_t dims[3] = { 16, 16, 16 };
    float origin[3] = { 0, 0, 0 };
    float voxel = 0.5f;
    static float dist[16 * 16 * 16];
    floor_sdf(dist, dims, origin, voxel);

    /* Straight up from above the floor: unoccluded. */
    float p[3] = { 4, 4, 3 };
    float up[3] = { 0, 0, 1 };
    float v = refl_occl_cone(dist, dims, origin, voxel, p, up, 0.05f, 8.0f);
    ASSERT_TRUE(v > 0.9f);

    /* Straight down into the slab: fully occluded. */
    float dn[3] = { 0, 0, -1 };
    v = refl_occl_cone(dist, dims, origin, voxel, p, dn, 0.05f, 8.0f);
    ASSERT_TRUE(v < 0.1f);

    /* Horizontal ray grazing 2 m above the floor: a wide cone clips the
     * slab and loses visibility; a narrow one keeps more. */
    float hz[3] = { 1, 0, 0 };
    float wide = refl_occl_cone(dist, dims, origin, voxel, p, hz, 0.9f, 8.0f);
    float narrow = refl_occl_cone(dist, dims, origin, voxel, p, hz, 0.02f,
                                  8.0f);
    ASSERT_TRUE(narrow >= wide);
    ASSERT_TRUE(narrow > 0.8f);

    /* NULL SDF: no occlusion data -> fully visible. */
    v = refl_occl_cone(NULL, NULL, NULL, 0.0f, p, dn, 0.1f, 8.0f);
    ASSERT_TRUE(v == 1.0f);
    return 0;
}

/* ------------------------------------------- callback-sampler variants */

/* Callback SDF: distance to the plane z = 1 (matches floor_sdf). */
static float cb_floor(const float p[3], void *user)
{
    (void)user;
    return p[2] - 1.0f;
}

static int test_fn_variants(void)
{
    enum { CAP = 16 };
    refl_probe_t probes[CAP];
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, CAP);
    float mn[3] = { 0, 0, 0 }, mx[3] = { 8, 8, 1 };
    /* Centres at z=0.5 -> cb says -0.5 -> all culled. */
    uint32_t n = refl_place_grid_fn(&set, mn, mx, 4.0f, cb_floor, NULL,
                                    0.5f, 0.0f);
    ASSERT_TRUE(n == 0u);
    float mx2[3] = { 8, 8, 8 };
    refl_probe_set_init(&set, probes, CAP);
    n = refl_place_grid_fn(&set, mn, mx2, 4.0f, cb_floor, NULL, 0.5f, 0.0f);
    ASSERT_TRUE(n == 8u);

    /* near_max: cells far above the floor (sdf > near_max) are pruned --
     * big open-world scenes keep probes near geometry only. Centres sit at
     * z=2 (sdf 1, kept) and z=6 (sdf 5, pruned by near_max 3). */
    refl_probe_set_init(&set, probes, CAP);
    n = refl_place_grid_fn(&set, mn, mx2, 4.0f, cb_floor, NULL, 0.5f, 3.0f);
    ASSERT_TRUE(n == 4u);
    for (uint32_t i = 0; i < set.count; ++i)
        ASSERT_TRUE(set.probes[i].pos[2] < 4.0f);

    float p[3] = { 4, 4, 3 };
    float up[3] = { 0, 0, 1 }, dn[3] = { 0, 0, -1 };
    ASSERT_TRUE(refl_occl_cone_fn(cb_floor, NULL, p, up, 0.05f, 8.0f) >
                0.9f);
    ASSERT_TRUE(refl_occl_cone_fn(cb_floor, NULL, p, dn, 0.05f, 8.0f) <
                0.1f);
    /* NULL fn -> fully visible / nothing placed but no crash. */
    ASSERT_TRUE(refl_occl_cone_fn(NULL, NULL, p, dn, 0.05f, 8.0f) == 1.0f);
    return 0;
}

/* ------------------------------------------------------------------ file */

static int test_file_roundtrip(void)
{
    enum { N = 3, TR = 8, MIPS = 2 };
    refl_probe_t probes[N];
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, N);
    set.tile_res = TR;
    set.mips = MIPS;
    set.tiles_x = 2u;
    set.tiles_y = 2u;
    set.depth_res = 4u;
    for (uint32_t i = 0; i < N; ++i) {
        refl_probe_t *pr = &set.probes[set.count++];
        pr->pos[0] = (float)i; pr->pos[1] = 2.0f * (float)i;
        pr->pos[2] = -1.5f;
        pr->ao = 0.25f * (float)(i + 1);
        pr->tile = i;
    }
    /* Mip payloads: full atlas per mip, distinctive values; plus the RG
     * visibility-depth atlas (tiles_x*depth_res x tiles_y*depth_res). */
    static float mip0[16 * 16 * 4], mip1[8 * 8 * 4];
    static float depth[8 * 8 * 2];
    for (uint32_t i = 0; i < 16 * 16 * 4; ++i) mip0[i] = (float)i * 0.5f;
    for (uint32_t i = 0; i < 8 * 8 * 4; ++i) mip1[i] = 100.0f + (float)i;
    for (uint32_t i = 0; i < 8 * 8 * 2; ++i) depth[i] = 7.0f + (float)i;
    const float *mips[MIPS] = { mip0, mip1 };

    const char *path = "build/refl_probe_test.rprobe";
    ASSERT_TRUE(refl_file_save(path, &set, mips, depth));
    /* NULL depth with nonzero depth_res -> rejected. */
    ASSERT_TRUE(!refl_file_save(path, &set, mips, NULL));

    refl_probe_t lp[8];
    refl_probe_set_t ls;
    refl_probe_set_init(&ls, lp, 8u);
    float *lmips[REFL_PROBE_MAX_MIPS] = { 0 };
    float *ldepth = NULL;
    ASSERT_TRUE(refl_file_load(path, &ls, lmips, &ldepth));
    ASSERT_TRUE(ls.count == N && ls.tile_res == TR && ls.mips == MIPS);
    ASSERT_TRUE(ls.tiles_x == 2u && ls.tiles_y == 2u);
    ASSERT_TRUE(ls.depth_res == 4u);
    for (uint32_t i = 0; i < N; ++i) {
        ASSERT_TRUE(fabsf(ls.probes[i].pos[1] - 2.0f * (float)i) < 1e-6f);
        ASSERT_TRUE(fabsf(ls.probes[i].ao - 0.25f * (float)(i + 1)) < 1e-6f);
        ASSERT_TRUE(ls.probes[i].tile == i);
    }
    ASSERT_TRUE(lmips[0] != NULL && lmips[1] != NULL && ldepth != NULL);
    ASSERT_TRUE(memcmp(lmips[0], mip0, sizeof mip0) == 0);
    ASSERT_TRUE(memcmp(lmips[1], mip1, sizeof mip1) == 0);
    ASSERT_TRUE(memcmp(ldepth, depth, sizeof depth) == 0);
    for (uint32_t m = 0; m < MIPS; ++m) free(lmips[m]);
    free(ldepth);

    /* Corrupt magic -> rejected. */
    FILE *f = fopen(path, "r+b");
    ASSERT_TRUE(f != NULL);
    fwrite("XXXX", 1, 4, f);
    fclose(f);
    refl_probe_set_init(&ls, lp, 8u);
    ASSERT_TRUE(!refl_file_load(path, &ls, lmips, &ldepth));

    /* Truncated file -> rejected (no partial buffers leaked back). */
    ASSERT_TRUE(refl_file_save(path, &set, mips, depth));
    f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    long full = ftell(f);
    fclose(f);
    ASSERT_TRUE(truncate(path, full / 2) == 0);
    refl_probe_set_init(&ls, lp, 8u);
    ASSERT_TRUE(!refl_file_load(path, &ls, lmips, &ldepth));
    return 0;
}

/* ------------------------------------------------- streaming primitives */
#include "ferrum/renderer/gi/refl_half.h"
#include "ferrum/renderer/gi/refl_index.h"
#include "ferrum/renderer/gi/refl_slots.h"

static int test_half_roundtrip(void)
{
    const float vals[] = { 0.0f, 1.0f, -1.0f, 0.5f, 0.1f, 100.0f,
                           0.0009765625f, 65504.0f };
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i) {
        uint16_t h = refl_f32_to_f16(vals[i]);
        float back = refl_f16_to_f32(h);
        float tol = (vals[i] == 0.0f) ? 1e-6f : fabsf(vals[i]) * 2e-3f;
        ASSERT_TRUE(fabsf(back - vals[i]) <= tol);
    }
    /* Overflow clamps to max half, never inf. */
    ASSERT_TRUE(refl_f16_to_f32(refl_f32_to_f16(1e9f)) <= 65504.0f);
    return 0;
}

static int test_slot_pool(void)
{
    uint16_t mem[8];
    refl_slot_pool_t pool;
    refl_slot_pool_init(&pool, mem, 8u);
    uint32_t a = refl_slot_alloc(&pool);
    uint32_t b = refl_slot_alloc(&pool);
    ASSERT_TRUE(a != b && a < 8u && b < 8u);
    for (int i = 0; i < 6; ++i)
        ASSERT_TRUE(refl_slot_alloc(&pool) < 8u);
    ASSERT_TRUE(refl_slot_alloc(&pool) == REFL_SLOT_NONE);   /* exhausted */
    refl_slot_free(&pool, b);
    uint32_t c = refl_slot_alloc(&pool);
    ASSERT_TRUE(c == b);                                     /* reused */
    /* Double free is ignored; alloc still exhausted after. */
    refl_slot_free(&pool, c);
    refl_slot_free(&pool, c);
    ASSERT_TRUE(refl_slot_alloc(&pool) == c);
    ASSERT_TRUE(refl_slot_alloc(&pool) == REFL_SLOT_NONE);
    return 0;
}

static int test_grid_index(void)
{
    enum { CAP = 8 };
    refl_probe_t probes[CAP];
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, CAP);
    /* Two probes in one cell, one in another. */
    float ps[3][3] = { { 1, 1, 1 }, { 2, 2, 2 }, { 30, 1, 1 } };
    for (int i = 0; i < 3; ++i) {
        refl_probe_t *p = &set.probes[set.count++];
        p->pos[0] = ps[i][0]; p->pos[1] = ps[i][1]; p->pos[2] = ps[i][2];
        p->tile = (uint32_t)i;
    }
    float mn[3] = { 0, 0, 0 }, mx[3] = { 64, 8, 64 };
    int32_t cells[8 * 1 * 8 * REFL_INDEX_PER_CELL];
    int32_t dims[3];
    refl_index_build(&set, mn, mx, 8.0f, cells, 8 * 1 * 8, dims);
    ASSERT_TRUE(dims[0] == 8 && dims[1] == 1 && dims[2] == 8);
    /* Cell (0,0,0) holds probes 0+1; cell (3,0,0) holds probe 2. */
    const int32_t *c0 = &cells[0];
    ASSERT_TRUE(c0[0] == 0 && c0[1] == 1 && c0[2] < 0);
    const int32_t *c3 = &cells[(size_t)3 * REFL_INDEX_PER_CELL];
    ASSERT_TRUE(c3[0] == 2 && c3[1] < 0);
    /* Out-of-grid probe is dropped without crashing. */
    refl_probe_t *p = &set.probes[set.count++];
    p->pos[0] = -50.0f; p->pos[1] = 0.0f; p->pos[2] = 0.0f;
    refl_index_build(&set, mn, mx, 8.0f, cells, 8 * 1 * 8, dims);
    ASSERT_TRUE(c0[0] == 0 && c0[1] == 1);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_octa_roundtrip();
    rc |= test_octa_degenerate();
    rc |= test_atlas_layout();
    rc |= test_cube_to_octa();
    rc |= test_filter_constant_invariant();
    rc |= test_filter_spreads_delta();
    rc |= test_place_grid_and_cull();
    rc |= test_occl_cone();
    rc |= test_fn_variants();
    rc |= test_file_roundtrip();
    rc |= test_half_roundtrip();
    rc |= test_slot_pool();
    rc |= test_grid_index();
    if (rc == 0)
        printf("  OK: all refl_probe tests passed\n");
    return rc;
}

