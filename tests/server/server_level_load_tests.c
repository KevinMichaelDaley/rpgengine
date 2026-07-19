/**
 * @file server_level_load_tests.c
 * @brief Tests for the server level loader (rpg-q1cp): descriptor collider set
 *        -> static physics bodies with the right collider pools.
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/scene/scene_desc.h"
#include "ferrum/server/server_level_load.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

static scene_desc_collider_t mkc(scene_desc_collider_kind_t k)
{
    scene_desc_collider_t c; memset(&c, 0, sizeof c);
    c.kind = k; c.rotation[3] = 1.0f; c.object_ref = -1; c.bone = -1; c.is_static = true;
    return c;
}

static int test_load_primitive_colliders(void)
{
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    scene_desc_collider_t cols[7];
    cols[0] = mkc(SCENE_DESC_COLLIDER_BOX);
    cols[0].half_extents[0] = cols[0].half_extents[1] = cols[0].half_extents[2] = 1.0f;
    cols[0].position[1] = 5.0f;
    cols[1] = mkc(SCENE_DESC_COLLIDER_SPHERE); cols[1].radius = 0.5f;
    cols[2] = mkc(SCENE_DESC_COLLIDER_CAPSULE); cols[2].radius = 0.4f; cols[2].half_height = 1.0f;
    cols[3] = mkc(SCENE_DESC_COLLIDER_HALFSPACE); cols[3].normal[1] = 1.0f; cols[3].plane_offset = 0.0f;
    cols[4] = mkc(SCENE_DESC_COLLIDER_POINT);
    cols[5] = mkc(SCENE_DESC_COLLIDER_MESH);   /* with a bound -> AABB box proxy */
    cols[5].half_extents[0] = cols[5].half_extents[1] = cols[5].half_extents[2] = 2.0f;
    cols[6] = mkc(SCENE_DESC_COLLIDER_MESH);   /* NO bound -> skipped */

    scene_desc_t d; memset(&d, 0, sizeof d);
    d.collider_count = 7; d.colliders = cols;

    uint32_t n = server_level_load_colliders(&world, &d);
    ASSERT_INT_EQ(n, 6);                          /* the bound-less mesh is skipped */
    ASSERT_INT_EQ(phys_world_body_count(&world), 6);
    /* Box collider + mesh-with-bound AABB proxy both allocate boxes. */
    ASSERT_INT_EQ(world.box_count, 2);
    ASSERT_INT_EQ(world.sphere_count, 1);
    ASSERT_INT_EQ(world.capsule_count, 1);
    ASSERT_INT_EQ(world.halfspace_count, 1);

    /* Loaded bodies are static and positioned from the descriptor. */
    phys_body_t *b0 = phys_world_get_body(&world, 0);
    ASSERT_TRUE(b0 != NULL);
    ASSERT_TRUE(b0->inv_mass == 0.0f);
    ASSERT_TRUE((b0->flags & PHYS_BODY_FLAG_STATIC) != 0u);

    phys_world_destroy(&world);
    return 0;
}

static int test_null_safe(void)
{
    ASSERT_INT_EQ(server_level_load_colliders(NULL, NULL), 0);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"load_primitive_colliders", test_load_primitive_colliders},
        {"null_safe",                test_null_safe},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\nserver_level_load_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
