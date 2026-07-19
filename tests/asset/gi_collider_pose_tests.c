/**
 * @file gi_collider_pose_tests.c
 * @brief Tests for the posed GI collider-proxy builder (rpg-85as): box AABB,
 *        sphere, capsule endpoints, bone-keyed posing vs owner, kind skipping,
 *        and the output cap.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/asset/collider_prim.h"
#include "ferrum/renderer/gi/gi_sdf.h"
#include "ferrum/renderer/gi/gi_collider_pose.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)
#define ASSERT_FLT_EQ(a,b) do { double _a=(double)(a),_b=(double)(b); if (fabs(_a-_b)>1e-4) { \
    fprintf(stderr,"  ASSERT_FLT_EQ failed: %g != %g (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

/* Column-major translation matrix. */
static void translate(float m[16], float x, float y, float z)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = x; m[13] = y; m[14] = z;
}

static fr_collider_prim_t prim(fr_collider_prim_kind_t kind)
{
    fr_collider_prim_t p; memset(&p, 0, sizeof p);
    p.kind = kind; p.bone = -1; p.rotation[3] = 1.0f;
    return p;
}

static int test_box_aabb_translated(void)
{
    float owner[16]; translate(owner, 10.0f, 0.0f, 0.0f);
    fr_collider_prim_t p = prim(FR_COLLIDER_PRIM_BOX);
    p.half_extents[0] = 1.0f; p.half_extents[1] = 2.0f; p.half_extents[2] = 3.0f;
    gi_collider_t out[4];
    uint32_t n = gi_collider_pose_build(&p, 1, owner, NULL, 0, out, 4);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(out[0].kind, GI_COLLIDER_BOX);
    ASSERT_FLT_EQ(out[0].a[0], 10.0f);
    ASSERT_FLT_EQ(out[0].ext[0], 1.0f);
    ASSERT_FLT_EQ(out[0].ext[1], 2.0f);
    ASSERT_FLT_EQ(out[0].ext[2], 3.0f);
    return 0;
}

static int test_sphere(void)
{
    float owner[16]; translate(owner, 0.0f, 5.0f, 0.0f);
    fr_collider_prim_t p = prim(FR_COLLIDER_PRIM_SPHERE);
    p.radius = 0.75f;
    gi_collider_t out[1];
    ASSERT_INT_EQ(gi_collider_pose_build(&p, 1, owner, NULL, 0, out, 1), 1);
    ASSERT_INT_EQ(out[0].kind, GI_COLLIDER_SPHERE);
    ASSERT_FLT_EQ(out[0].a[1], 5.0f);
    ASSERT_FLT_EQ(out[0].ext[0], 0.75f);
    return 0;
}

static int test_capsule_endpoints(void)
{
    float owner[16]; translate(owner, 0.0f, 10.0f, 0.0f);
    fr_collider_prim_t p = prim(FR_COLLIDER_PRIM_CAPSULE);
    p.radius = 0.5f; p.half_height = 2.0f;   /* along local Y */
    gi_collider_t out[1];
    ASSERT_INT_EQ(gi_collider_pose_build(&p, 1, owner, NULL, 0, out, 1), 1);
    ASSERT_INT_EQ(out[0].kind, GI_COLLIDER_CAPSULE);
    ASSERT_FLT_EQ(out[0].a[1], 8.0f);    /* 10 - 2 */
    ASSERT_FLT_EQ(out[0].b[1], 12.0f);   /* 10 + 2 */
    ASSERT_FLT_EQ(out[0].ext[0], 0.5f);
    return 0;
}

static int test_bone_keyed_uses_bone(void)
{
    float owner[16]; translate(owner, 10.0f, 0.0f, 0.0f);
    float bones[2 * 16];
    translate(&bones[0], 1.0f, 1.0f, 1.0f);
    translate(&bones[16], -3.0f, 4.0f, 0.0f);   /* bone 1 */
    fr_collider_prim_t p = prim(FR_COLLIDER_PRIM_SPHERE);
    p.radius = 1.0f; p.bone = 1;                 /* keyed to bone 1 */
    gi_collider_t out[1];
    ASSERT_INT_EQ(gi_collider_pose_build(&p, 1, owner, bones, 2, out, 1), 1);
    /* Posed by bone 1, not the owner. */
    ASSERT_FLT_EQ(out[0].a[0], -3.0f);
    ASSERT_FLT_EQ(out[0].a[1], 4.0f);
    return 0;
}

static int test_skip_and_cap(void)
{
    float owner[16]; translate(owner, 0, 0, 0);
    fr_collider_prim_t ps[4];
    ps[0] = prim(FR_COLLIDER_PRIM_HALFSPACE);  /* skipped */
    ps[1] = prim(FR_COLLIDER_PRIM_POINT);      /* skipped */
    ps[2] = prim(FR_COLLIDER_PRIM_BOX); ps[2].half_extents[0]=1;
    ps[3] = prim(FR_COLLIDER_PRIM_SPHERE); ps[3].radius=1;
    gi_collider_t out[4];
    /* Halfspace + point skipped -> only 2 proxies. */
    ASSERT_INT_EQ(gi_collider_pose_build(ps, 4, owner, NULL, 0, out, 4), 2);
    /* Cap respected: ask for 1. */
    ASSERT_INT_EQ(gi_collider_pose_build(ps, 4, owner, NULL, 0, out, 1), 1);
    return 0;
}

static int test_convex_mesh_box_proxy(void)
{
    float owner[16]; translate(owner, 0, 0, 0);
    fr_collider_prim_t p = prim(FR_COLLIDER_PRIM_CONVEX);
    p.half_extents[0]=2; p.half_extents[1]=1; p.half_extents[2]=1; /* local AABB bound */
    gi_collider_t out[1];
    ASSERT_INT_EQ(gi_collider_pose_build(&p, 1, owner, NULL, 0, out, 1), 1);
    ASSERT_INT_EQ(out[0].kind, GI_COLLIDER_BOX);  /* AABB proxy */
    ASSERT_FLT_EQ(out[0].ext[0], 2.0f);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"box_aabb_translated",   test_box_aabb_translated},
        {"sphere",                test_sphere},
        {"capsule_endpoints",     test_capsule_endpoints},
        {"bone_keyed_uses_bone",  test_bone_keyed_uses_bone},
        {"skip_and_cap",          test_skip_and_cap},
        {"convex_mesh_box_proxy", test_convex_mesh_box_proxy},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\ngi_collider_pose_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
