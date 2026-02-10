#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/raycast.h"
#include "ferrum/physics/world.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FALSE(cond)                                                                               \
    do {                                                                                                 \
        if ((cond)) {                                                                                    \
            fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define RUN_TEST(fn)                                                                                     \
    do {                                                                                                 \
        printf("  %-45s", #fn);                                                                          \
        int _r = fn();                                                                                   \
        printf("%s\n", _r ? "FAIL" : "PASS");                                                         \
        if (_r) fail_count++;                                                                            \
        test_count++;                                                                                    \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static int make_world_(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16u;
    cfg.max_colliders = 16u;
    return phys_world_init(world, &cfg);
}

static phys_ray_t make_ray_z_(float max_dist) {
    phys_ray_t ray;
    ray.origin = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    ray.direction = (phys_vec3_t){0.0f, 0.0f, 1.0f};
    ray.max_distance = max_dist;
    return ray;
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_ray_hits_sphere(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_id = phys_world_create_body(&world);
    ASSERT_TRUE(body_id != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body_id);
    ASSERT_TRUE(b != NULL);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};

    phys_world_set_sphere_collider(&world, body_id, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(10.0f);
    ASSERT_TRUE(phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)body_id, (int)hit.body_id);
    ASSERT_FLOAT_NEAR(4.0f, hit.distance, 1e-3f);
    ASSERT_FLOAT_NEAR(0.0f, hit.point.x, 1e-3f);
    ASSERT_FLOAT_NEAR(0.0f, hit.point.y, 1e-3f);
    ASSERT_FLOAT_NEAR(4.0f, hit.point.z, 1e-3f);
    ASSERT_FLOAT_NEAR(0.0f, hit.normal.x, 1e-3f);
    ASSERT_FLOAT_NEAR(0.0f, hit.normal.y, 1e-3f);
    ASSERT_FLOAT_NEAR(-1.0f, hit.normal.z, 1e-3f);

    phys_world_destroy(&world);
    return 0;
}

static int test_ray_misses_sphere(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_id = phys_world_create_body(&world);
    ASSERT_TRUE(body_id != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body_id);
    ASSERT_TRUE(b != NULL);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){10.0f, 0.0f, 5.0f};

    phys_world_set_sphere_collider(&world, body_id, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(10.0f);
    ASSERT_FALSE(phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu));

    phys_world_destroy(&world);
    return 0;
}

static int test_ray_hits_box_face(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_id = phys_world_create_body(&world);
    ASSERT_TRUE(body_id != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body_id);
    ASSERT_TRUE(b != NULL);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};

    phys_world_set_box_collider(&world,
                               body_id,
                               (phys_vec3_t){1.0f, 1.0f, 1.0f},
                               (phys_vec3_t){0, 0, 0},
                               (phys_quat_t){0, 0, 0, 1});

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(10.0f);
    ASSERT_TRUE(phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)body_id, (int)hit.body_id);
    ASSERT_FLOAT_NEAR(4.0f, hit.distance, 1e-3f);
    ASSERT_FLOAT_NEAR(4.0f, hit.point.z, 1e-3f);
    ASSERT_FLOAT_NEAR(-1.0f, hit.normal.z, 1e-3f);

    phys_world_destroy(&world);
    return 0;
}

static int test_ray_hits_capsule(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_id = phys_world_create_body(&world);
    ASSERT_TRUE(body_id != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body_id);
    ASSERT_TRUE(b != NULL);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};

    phys_world_set_capsule_collider(&world,
                                   body_id,
                                   1.0f,
                                   1.0f,
                                   (phys_vec3_t){0, 0, 0},
                                   (phys_quat_t){0, 0, 0, 1});

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(10.0f);
    ASSERT_TRUE(phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)body_id, (int)hit.body_id);
    ASSERT_FLOAT_NEAR(4.0f, hit.distance, 1e-3f);
    ASSERT_FLOAT_NEAR(4.0f, hit.point.z, 1e-3f);
    ASSERT_FLOAT_NEAR(-1.0f, hit.normal.z, 1e-3f);

    phys_world_destroy(&world);
    return 0;
}

static int test_ray_respects_max_distance(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_id = phys_world_create_body(&world);
    ASSERT_TRUE(body_id != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body_id);
    ASSERT_TRUE(b != NULL);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};

    phys_world_set_sphere_collider(&world, body_id, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(3.5f);
    ASSERT_FALSE(phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu));

    phys_world_destroy(&world);
    return 0;
}

static int test_ray_returns_closest_hit(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t a = phys_world_create_body(&world);
    uint32_t b = phys_world_create_body(&world);
    ASSERT_TRUE(a != UINT32_MAX && b != UINT32_MAX);

    phys_body_t *ba = phys_world_get_body(&world, a);
    phys_body_t *bb = phys_world_get_body(&world, b);
    ASSERT_TRUE(ba && bb);
    phys_body_set_mass(ba, 1.0f);
    phys_body_set_mass(bb, 1.0f);
    ba->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};
    bb->position = (phys_vec3_t){0.0f, 0.0f, 8.0f};

    phys_world_set_sphere_collider(&world, a, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_world_set_sphere_collider(&world, b, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(20.0f);
    ASSERT_TRUE(phys_raycast(&world, &ray, &hit, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)a, (int)hit.body_id);

    phys_world_destroy(&world);
    return 0;
}

static int test_ray_all_multiple_hits_sorted(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t a = phys_world_create_body(&world);
    uint32_t b = phys_world_create_body(&world);
    ASSERT_TRUE(a != UINT32_MAX && b != UINT32_MAX);

    phys_body_t *ba = phys_world_get_body(&world, a);
    phys_body_t *bb = phys_world_get_body(&world, b);
    ASSERT_TRUE(ba && bb);
    phys_body_set_mass(ba, 1.0f);
    phys_body_set_mass(bb, 1.0f);
    ba->position = (phys_vec3_t){0.0f, 0.0f, 8.0f};
    bb->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};

    phys_world_set_sphere_collider(&world, a, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_world_set_sphere_collider(&world, b, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_raycast_hit_t hits[4];
    memset(hits, 0, sizeof(hits));

    phys_ray_t ray = make_ray_z_(20.0f);
    uint32_t n = phys_raycast_all(&world, &ray, hits, 4u, 0xFFFFFFFFu);
    ASSERT_INT_EQ(2, (int)n);
    ASSERT_INT_EQ((int)b, (int)hits[0].body_id);
    ASSERT_INT_EQ((int)a, (int)hits[1].body_id);
    ASSERT_TRUE(hits[0].distance <= hits[1].distance);

    phys_world_destroy(&world);
    return 0;
}

static int test_layer_mask_filtering(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t a = phys_world_create_body(&world);
    uint32_t b = phys_world_create_body(&world);
    ASSERT_TRUE(a != UINT32_MAX && b != UINT32_MAX);

    phys_body_t *ba = phys_world_get_body(&world, a);
    phys_body_t *bb = phys_world_get_body(&world, b);
    ASSERT_TRUE(ba && bb);
    phys_body_set_mass(ba, 1.0f);
    phys_body_set_mass(bb, 1.0f);
    ba->position = (phys_vec3_t){0.0f, 0.0f, 5.0f};
    bb->position = (phys_vec3_t){0.0f, 0.0f, 8.0f};

    phys_world_set_sphere_collider(&world, a, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_world_set_sphere_collider(&world, b, 1.0f, (phys_vec3_t){0, 0, 0});

    world.colliders[a].layer_id = 0u;
    world.colliders[b].layer_id = 1u;

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    phys_ray_t ray = make_ray_z_(20.0f);
    ASSERT_TRUE(phys_raycast(&world, &ray, &hit, 0x2u));
    ASSERT_INT_EQ((int)b, (int)hit.body_id);

    phys_world_destroy(&world);
    return 0;
}

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p084_physics_raycast_tests:\n");

    RUN_TEST(test_ray_hits_sphere);
    RUN_TEST(test_ray_misses_sphere);
    RUN_TEST(test_ray_hits_box_face);
    RUN_TEST(test_ray_hits_capsule);
    RUN_TEST(test_ray_respects_max_distance);
    RUN_TEST(test_ray_returns_closest_hit);
    RUN_TEST(test_ray_all_multiple_hits_sorted);
    RUN_TEST(test_layer_mask_filtering);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
