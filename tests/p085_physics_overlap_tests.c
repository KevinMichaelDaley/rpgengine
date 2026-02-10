#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ferrum/physics/overlap.h"
#include "ferrum/physics/world.h"

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
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
    cfg.max_bodies = 32u;
    cfg.max_colliders = 32u;
    return phys_world_init(world, &cfg);
}

static int contains_u32_(const uint32_t *arr, uint32_t count, uint32_t v) {
    for (uint32_t i = 0; i < count; i++) {
        if (arr[i] == v) {
            return 1;
        }
    }
    return 0;
}

static void alloc_query_sphere_(phys_world_t *world, float radius, phys_collider_t *out) {
    uint32_t si = world->sphere_count++;
    world->spheres[si].radius = radius;
    phys_collider_init_sphere(out, si, (phys_vec3_t){0, 0, 0});
}

static void alloc_query_box_(phys_world_t *world, phys_vec3_t half_extents, phys_collider_t *out) {
    uint32_t bi = world->box_count++;
    world->boxes[bi].half_extents = half_extents;
    phys_collider_init_box(out, bi, (phys_vec3_t){0, 0, 0}, (phys_quat_t){0, 0, 0, 1});
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_overlap_sphere_finds_bodies(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_a = phys_world_create_body(&world);
    uint32_t body_b = phys_world_create_body(&world);
    ASSERT_TRUE(body_a != UINT32_MAX && body_b != UINT32_MAX);

    phys_body_t *ba = phys_world_get_body(&world, body_a);
    phys_body_t *bb = phys_world_get_body(&world, body_b);
    ASSERT_TRUE(ba && bb);
    ba->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    bb->position = (phys_vec3_t){5.0f, 0.0f, 0.0f};

    phys_world_set_sphere_collider(&world, body_a, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_world_set_sphere_collider(&world, body_b, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_collider_t query;
    alloc_query_sphere_(&world, 1.0f, &query);

    uint32_t out[8];
    memset(out, 0, sizeof(out));

    uint32_t n = phys_overlap(&world, &query,
                             (phys_vec3_t){0.5f, 0.0f, 0.0f},
                             (phys_quat_t){0, 0, 0, 1},
                             out, 8u, 0xFFFFFFFFu);

    ASSERT_INT_EQ(1, (int)n);
    ASSERT_TRUE(contains_u32_(out, n, body_a));

    phys_world_destroy(&world);
    return 0;
}

static int test_overlap_box_rotated(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body = phys_world_create_body(&world);
    ASSERT_TRUE(body != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body);
    ASSERT_TRUE(b);
    b->position = (phys_vec3_t){1.0f, 1.0f, 0.0f};

    phys_world_set_sphere_collider(&world, body, 0.1f, (phys_vec3_t){0, 0, 0});

    phys_collider_t query;
    alloc_query_box_(&world, (phys_vec3_t){2.0f, 0.2f, 0.2f}, &query);

    quat_t q = quat_from_axis_angle((vec3_t){0.0f, 0.0f, 1.0f}, (float)M_PI * 0.25f, 1e-6f);
    phys_quat_t rot = PHYS_QUAT_FROM_QUAT(q);

    uint32_t out[8];
    memset(out, 0, sizeof(out));

    uint32_t n = phys_overlap(&world, &query,
                             (phys_vec3_t){0.0f, 0.0f, 0.0f},
                             rot,
                             out, 8u, 0xFFFFFFFFu);

    ASSERT_INT_EQ(1, (int)n);
    ASSERT_TRUE(contains_u32_(out, n, body));

    phys_world_destroy(&world);
    return 0;
}

static int test_overlap_returns_nothing_for_empty_region(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body = phys_world_create_body(&world);
    ASSERT_TRUE(body != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body);
    ASSERT_TRUE(b);
    b->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_world_set_sphere_collider(&world, body, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_collider_t query;
    alloc_query_sphere_(&world, 1.0f, &query);

    uint32_t out[8];
    memset(out, 0, sizeof(out));

    uint32_t n = phys_overlap(&world, &query,
                             (phys_vec3_t){10.0f, 0.0f, 0.0f},
                             (phys_quat_t){0, 0, 0, 1},
                             out, 8u, 0xFFFFFFFFu);

    ASSERT_INT_EQ(0, (int)n);

    phys_world_destroy(&world);
    return 0;
}

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p085_physics_overlap_tests:\n");

    RUN_TEST(test_overlap_sphere_finds_bodies);
    RUN_TEST(test_overlap_box_rotated);
    RUN_TEST(test_overlap_returns_nothing_for_empty_region);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
