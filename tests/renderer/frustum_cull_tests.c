/**
 * @file frustum_cull_tests.c
 * @brief Unit + micro-benchmark for the shared frustum-cull helper (rpg-0rs4).
 *        Written before the implementation (TDD). Pure CPU math -- no GL context.
 *
 * The AABB-vs-frustum test is the exact "positive vertex" plane test hoisted
 * out of shadow_csm_render.c so the forward / depth-pre / shadow loops can all
 * skip off-screen geometry. These tests pin the visible/culled decision for a
 * standard perspective camera and the draw_distance variant, then time a large
 * batch so a future regression in the hot path is visible.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/renderer/cull/frustum_cull.h"

#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n", \
        #expr, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

/* proj*view for a camera at the origin looking down -Z (60 deg, 1:1, 0.1..100). */
static mat4_t test_view_proj(void)
{
    mat4_t view, proj;
    vec3_t eye = { 0.0f, 0.0f, 0.0f };
    vec3_t target = { 0.0f, 0.0f, -1.0f };
    vec3_t up = { 0.0f, 1.0f, 0.0f };
    mat4_look_at(eye, target, up, &view);
    mat4_perspective(1.04719755f /* 60 deg */, 1.0f, 0.1f, 100.0f, &proj);
    return mat4_mul(proj, view);
}

/* A unit AABB centred at (cx,cy,cz): local box [-0.5,0.5]^3, model = translate. */
static void unit_box_at(float cx, float cy, float cz, float model[16],
                        float lmin[3], float lmax[3])
{
    mat4_t t = mat4_translation(cx, cy, cz);
    memcpy(model, t.m, sizeof(float) * 16);
    lmin[0] = lmin[1] = lmin[2] = -0.5f;
    lmax[0] = lmax[1] = lmax[2] = 0.5f;
}

static int test_visible_box_in_front(void)
{
    mat4_t vp = test_view_proj();
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);
    float model[16], lmin[3], lmax[3];
    unit_box_at(0.0f, 0.0f, -10.0f, model, lmin, lmax);
    ASSERT_FALSE(frustum_cull_aabb(pl, model, lmin, lmax)); /* in view -> keep */
    return 0;
}

static int test_box_behind_camera_culled(void)
{
    mat4_t vp = test_view_proj();
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);
    float model[16], lmin[3], lmax[3];
    unit_box_at(0.0f, 0.0f, 10.0f, model, lmin, lmax); /* +Z is behind */
    ASSERT_TRUE(frustum_cull_aabb(pl, model, lmin, lmax));
    return 0;
}

static int test_box_off_to_side_culled(void)
{
    mat4_t vp = test_view_proj();
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);
    float model[16], lmin[3], lmax[3];
    unit_box_at(1000.0f, 0.0f, -10.0f, model, lmin, lmax); /* far right */
    ASSERT_TRUE(frustum_cull_aabb(pl, model, lmin, lmax));
    return 0;
}

static int test_box_beyond_far_plane_culled(void)
{
    mat4_t vp = test_view_proj();
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);
    float model[16], lmin[3], lmax[3];
    unit_box_at(0.0f, 0.0f, -200.0f, model, lmin, lmax); /* far=100 */
    ASSERT_TRUE(frustum_cull_aabb(pl, model, lmin, lmax));
    return 0;
}

static int test_huge_box_straddling_origin_kept(void)
{
    mat4_t vp = test_view_proj();
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);
    mat4_t id = mat4_identity();
    float model[16]; memcpy(model, id.m, sizeof model);
    float lmin[3] = { -1000, -1000, -1000 }, lmax[3] = { 1000, 1000, 1000 };
    ASSERT_FALSE(frustum_cull_aabb(pl, model, lmin, lmax)); /* encloses frustum */
    return 0;
}

static int test_draw_distance(void)
{
    mat4_t vp = test_view_proj();
    float eye[3] = { 0.0f, 0.0f, 0.0f };
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);
    float model[16], lmin[3], lmax[3];

    /* In frustum (far=100) but past a 50 m draw distance -> culled by _ex only. */
    unit_box_at(0.0f, 0.0f, -80.0f, model, lmin, lmax);
    ASSERT_FALSE(frustum_cull_aabb(pl, model, lmin, lmax));           /* frustum keeps */
    ASSERT_TRUE(frustum_cull_aabb_ex(pl, model, lmin, lmax, eye, 50.0f)); /* dist culls */
    ASSERT_FALSE(frustum_cull_aabb_ex(pl, model, lmin, lmax, eye, 0.0f)); /* 0 = unlimited */

    /* Inside both frustum and the distance band -> kept. */
    unit_box_at(0.0f, 0.0f, -10.0f, model, lmin, lmax);
    ASSERT_FALSE(frustum_cull_aabb_ex(pl, model, lmin, lmax, eye, 50.0f));
    return 0;
}

/* sphere_cull_aabb: a point light's range vs a caster's world AABB. */
static int test_sphere_cull(void)
{
    float model[16], lmin[3], lmax[3];
    float light[3] = { 0.0f, 0.0f, 0.0f };

    /* Unit box 3 m away: within a 5 m range -> keep; outside a 2 m range -> cull. */
    unit_box_at(3.0f, 0.0f, 0.0f, model, lmin, lmax);   /* nearest face at 2.5 m */
    ASSERT_FALSE(sphere_cull_aabb(light, 5.0f, model, lmin, lmax));
    ASSERT_TRUE(sphere_cull_aabb(light, 2.0f, model, lmin, lmax));

    /* Range 0 (unbounded) never culls, even very far away. */
    unit_box_at(1000.0f, 0.0f, 0.0f, model, lmin, lmax);
    ASSERT_FALSE(sphere_cull_aabb(light, 0.0f, model, lmin, lmax));
    ASSERT_TRUE(sphere_cull_aabb(light, 10.0f, model, lmin, lmax));

    /* Light inside the box -> distance 0 -> always kept. */
    unit_box_at(0.0f, 0.0f, 0.0f, model, lmin, lmax);
    ASSERT_FALSE(sphere_cull_aabb(light, 0.1f, model, lmin, lmax));
    return 0;
}

/* frustum_extract_planes_vp(proj,view) must match frustum_extract_planes(proj*view). */
static int test_extract_vp_matches_mvp(void)
{
    mat4_t view, proj;
    vec3_t eye = { 1.0f, 2.0f, 3.0f };
    vec3_t target = { 0.0f, 0.0f, -1.0f };
    vec3_t up = { 0.0f, 1.0f, 0.0f };
    mat4_look_at(eye, target, up, &view);
    mat4_perspective(1.0f, 1.6f, 0.2f, 250.0f, &proj);
    mat4_t mvp = mat4_mul(proj, view);

    float a[6][4], b[6][4];
    frustum_extract_planes(mvp.m, a);
    frustum_extract_planes_vp(proj.m, view.m, b);
    for (int i = 0; i < 6; ++i)
        for (int k = 0; k < 4; ++k) {
            float d = a[i][k] - b[i][k];
            if (d < 0) d = -d;
            ASSERT_TRUE(d < 1e-5f);
        }
    return 0;
}

/* Not an assertion -- times a realistic per-frame cull batch so a hot-path
 * regression shows up in the test log. */
static int bench_cull_batch(void)
{
    mat4_t vp = test_view_proj();
    float pl[6][4];
    frustum_extract_planes(vp.m, pl);

    enum { N = 20000, ITERS = 50 };
    static float models[N][16];
    static float mins[N][3], maxs[N][3];
    unsigned seed = 12345u;
    for (int i = 0; i < N; ++i) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 16) & 0x3ff) - 512.0f;
        seed = seed * 1103515245u + 12345u;
        float z = -(float)((seed >> 16) & 0x1ff);
        unit_box_at(x, 0.0f, z, models[i], mins[i], maxs[i]);
    }
    volatile uint32_t kept = 0;
    clock_t t0 = clock();
    for (int it = 0; it < ITERS; ++it)
        for (int i = 0; i < N; ++i)
            if (!frustum_cull_aabb(pl, models[i], mins[i], maxs[i])) ++kept;
    clock_t t1 = clock();
    double ns = (double)(t1 - t0) / CLOCKS_PER_SEC * 1e9 / ((double)N * ITERS);
    printf("  bench: %d boxes x %d iters, %.1f ns/box, kept=%u/frame\n",
           N, ITERS, ns, (unsigned)kept / ITERS);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "visible_box_in_front",        test_visible_box_in_front },
        { "box_behind_camera_culled",    test_box_behind_camera_culled },
        { "box_off_to_side_culled",      test_box_off_to_side_culled },
        { "box_beyond_far_plane_culled", test_box_beyond_far_plane_culled },
        { "huge_box_straddling_kept",    test_huge_box_straddling_origin_kept },
        { "draw_distance",               test_draw_distance },
        { "extract_vp_matches_mvp",      test_extract_vp_matches_mvp },
        { "sphere_cull",                 test_sphere_cull },
        { "bench_cull_batch",            bench_cull_batch },
    };
    int n = (int)(sizeof tests / sizeof tests[0]), pass = 0;
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        printf("[%s] %s\n", rc == 0 ? "ok  " : "FAIL", tests[i].name);
        pass += (rc == 0);
    }
    printf("\nfrustum_cull_tests: %d/%d passed\n", pass, n);
    return pass == n ? 0 : 1;
}
