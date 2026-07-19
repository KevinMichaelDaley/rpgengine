/**
 * @file collider_prim_tests.c
 * @brief Tests for the canonical collider-primitive schema + both source-channel
 *        converters (rpg-b5r3): BODY_SPAWN wire round-trip (incl. the new collider
 *        offset), net BODY_SPAWN -> primitive per shape, and scene-descriptor
 *        collider -> primitive (incl. convex/compound/point + bone keying).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/quantization.h"
#include "ferrum/scene/scene_desc_collider.h"
#include "ferrum/asset/collider_prim.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)
#define ASSERT_FLT_EQ(a,b) do { double _a=(double)(a),_b=(double)(b); if (fabs(_a-_b)>1e-2) { \
    fprintf(stderr,"  ASSERT_FLT_EQ failed: %g != %g (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

static int test_body_spawn_wire_roundtrip(void)
{
    net_repl_body_spawn_t m; memset(&m, 0, sizeof m);
    m.body_id = 4242; m.flags = 0x05; m.shape_type = 2; m.color_seed = 0xDEADBEEFu;
    m.pos_mm.x_mm = 1000; m.pos_mm.y_mm = -2500; m.pos_mm.z_mm = 12345;
    m.rot_x = 0.0f; m.rot_y = 0.0f; m.rot_z = 0.0f; m.rot_w = 1.0f;
    m.half_x_f16 = net_float16_from_float(0.5f);
    m.half_y_f16 = net_float16_from_float(1.5f);
    m.half_z_f16 = net_float16_from_float(0.5f);
    m.off_x_f16  = net_float16_from_float(0.25f);
    m.off_y_f16  = net_float16_from_float(-0.75f);
    m.off_z_f16  = net_float16_from_float(2.0f);

    uint8_t wire[NET_REPL_BODY_SPAWN_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_BODY_SPAWN_PAYLOAD_SIZE, 39);
    ASSERT_INT_EQ(net_repl_body_spawn_encode(&m, wire, sizeof wire), NET_REPL_OK);
    /* One byte short must fail. */
    ASSERT_INT_EQ(net_repl_body_spawn_encode(&m, wire, sizeof wire - 1), NET_REPL_ERR_SHORT);

    net_repl_body_spawn_t r; memset(&r, 0xAB, sizeof r);
    ASSERT_INT_EQ(net_repl_body_spawn_decode(&r, wire, sizeof wire), NET_REPL_OK);
    ASSERT_INT_EQ(net_repl_body_spawn_decode(&r, wire, sizeof wire - 1), NET_REPL_ERR_SHORT);

    ASSERT_INT_EQ(r.body_id, 4242);
    ASSERT_INT_EQ(r.flags, 0x05);
    ASSERT_INT_EQ(r.shape_type, 2);
    ASSERT_INT_EQ(r.color_seed, 0xDEADBEEFu);
    ASSERT_INT_EQ(r.pos_mm.x_mm, 1000);
    ASSERT_INT_EQ(r.pos_mm.y_mm, -2500);
    ASSERT_INT_EQ(r.pos_mm.z_mm, 12345);
    /* f16 fields are byte-identical through the wire. */
    ASSERT_INT_EQ(r.half_x_f16, m.half_x_f16);
    ASSERT_INT_EQ(r.off_x_f16, m.off_x_f16);
    ASSERT_INT_EQ(r.off_y_f16, m.off_y_f16);
    ASSERT_INT_EQ(r.off_z_f16, m.off_z_f16);
    /* Quat survives smallest-3 packing to good precision. */
    ASSERT_FLT_EQ(r.rot_w, 1.0f);
    return 0;
}

static net_repl_body_spawn_t mk(uint8_t shape, float hx, float hy, float hz,
                                float ox, float oy, float oz)
{
    net_repl_body_spawn_t m; memset(&m, 0, sizeof m);
    m.body_id = 77; m.shape_type = shape;
    m.half_x_f16 = net_float16_from_float(hx);
    m.half_y_f16 = net_float16_from_float(hy);
    m.half_z_f16 = net_float16_from_float(hz);
    m.off_x_f16  = net_float16_from_float(ox);
    m.off_y_f16  = net_float16_from_float(oy);
    m.off_z_f16  = net_float16_from_float(oz);
    return m;
}

static int test_netprim_shapes(void)
{
    fr_collider_prim_t p;

    net_repl_body_spawn_t box = mk(0, 1.0f, 2.0f, 3.0f, 0.1f, 0.2f, 0.3f);
    fr_collider_prim_from_body_spawn(&box, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_BOX);
    ASSERT_FLT_EQ(p.half_extents[1], 2.0f);
    ASSERT_FLT_EQ(p.offset[2], 0.3f);
    ASSERT_INT_EQ(p.bone, -1);

    net_repl_body_spawn_t sph = mk(1, 0.75f, 0, 0, 0, 0, 0);
    fr_collider_prim_from_body_spawn(&sph, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_SPHERE);
    ASSERT_FLT_EQ(p.radius, 0.75f);

    net_repl_body_spawn_t cap = mk(2, 0.4f, 1.2f, 0.4f, 0, 0, 0);
    fr_collider_prim_from_body_spawn(&cap, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_CAPSULE);
    ASSERT_FLT_EQ(p.radius, 0.4f);
    ASSERT_FLT_EQ(p.half_height, 1.2f);

    net_repl_body_spawn_t hs = mk(4, 0.0f, 1.0f, 0.0f, -0.5f, 0, 0); /* normal +Y, dist -0.5 */
    fr_collider_prim_from_body_spawn(&hs, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_HALFSPACE);
    ASSERT_FLT_EQ(p.normal[1], 1.0f);
    ASSERT_FLT_EQ(p.plane_offset, -0.5f);

    net_repl_body_spawn_t msh = mk(3, 0, 0, 0, 0, 0, 0);
    fr_collider_prim_from_body_spawn(&msh, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_MESH);
    ASSERT_INT_EQ(p.geom_asset, 77);   /* geometry keyed by body_id */

    net_repl_body_spawn_t cvx = mk(5, 0, 0, 0, 0, 0, 0);
    fr_collider_prim_from_body_spawn(&cvx, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_CONVEX);
    ASSERT_INT_EQ(p.geom_asset, 77);

    net_repl_body_spawn_t cmp = mk(6, 0, 0, 0, 0, 0, 0);
    fr_collider_prim_from_body_spawn(&cmp, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_COMPOUND);

    net_repl_body_spawn_t pt = mk(7, 0, 0, 0, 1, 2, 3);
    fr_collider_prim_from_body_spawn(&pt, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_POINT);
    ASSERT_FLT_EQ(p.offset[0], 1.0f);
    return 0;
}

static int test_descprim(void)
{
    /* A level-authored, bone-keyed convex collider on a dynamic object. */
    scene_desc_collider_t d; memset(&d, 0, sizeof d);
    d.kind = SCENE_DESC_COLLIDER_CONVEX;
    d.position[0] = 1; d.position[1] = 2; d.position[2] = 3;
    d.rotation[3] = 1.0f;
    d.geom_asset = 9001;
    d.bone = 5;
    d.object_ref = 2;
    d.is_static = false;

    fr_collider_prim_t p;
    fr_collider_prim_from_desc(&d, &p);
    ASSERT_INT_EQ(p.kind, FR_COLLIDER_PRIM_CONVEX);
    ASSERT_INT_EQ(p.bone, 5);                 /* bone keying carried */
    ASSERT_INT_EQ(p.geom_asset, 9001);        /* streamed geometry id */
    ASSERT_FLT_EQ(p.offset[0], 1.0f);
    ASSERT_FLT_EQ(p.offset[2], 3.0f);
    ASSERT_FLT_EQ(p.rotation[3], 1.0f);

    /* Kind numbering matches across the two enums for every kind. */
    scene_desc_collider_kind_t k[] = {
        SCENE_DESC_COLLIDER_BOX, SCENE_DESC_COLLIDER_SPHERE, SCENE_DESC_COLLIDER_CAPSULE,
        SCENE_DESC_COLLIDER_HALFSPACE, SCENE_DESC_COLLIDER_MESH, SCENE_DESC_COLLIDER_CONVEX,
        SCENE_DESC_COLLIDER_COMPOUND, SCENE_DESC_COLLIDER_POINT };
    for (int i = 0; i < 8; ++i) {
        scene_desc_collider_t c; memset(&c, 0, sizeof c); c.kind = k[i]; c.bone = -1;
        fr_collider_prim_t q; fr_collider_prim_from_desc(&c, &q);
        ASSERT_INT_EQ((int)q.kind, (int)k[i]);
    }
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"body_spawn_wire_roundtrip", test_body_spawn_wire_roundtrip},
        {"netprim_shapes",            test_netprim_shapes},
        {"descprim",                  test_descprim},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\ncollider_prim_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
