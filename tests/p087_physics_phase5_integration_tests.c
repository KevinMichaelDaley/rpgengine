#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/collider.h"
#include "ferrum/physics/query.h"
#include "ferrum/physics/world.h"

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

#define ASSERT_F32_NEAR(exp, act, eps)                                                                    \
    do {                                                                                                 \
        float _e = (exp);                                                                                \
        float _a = (act);                                                                                \
        if (fabsf(_e - _a) > (eps)) {                                                                     \
            fprintf(stderr, "ASSERT_F32_NEAR failed: %s:%d: expected %.6f got %.6f\n",                   \
                    __FILE__, __LINE__, (double)_e, (double)_a);                                          \
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
    cfg.max_bodies = 64u;
    cfg.max_colliders = 64u;
    return phys_world_init(world, &cfg);
}

static void alloc_query_sphere_(phys_world_t *world, float radius, phys_collider_t *out) {
    uint32_t si = world->sphere_count++;
    world->spheres[si].radius = radius;
    phys_collider_init_sphere(out, si, (phys_vec3_t){0, 0, 0});
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_raycast_through_stacked_bodies(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t bodies[5];
    for (uint32_t i = 0; i < 5; i++) {
        bodies[i] = phys_world_create_body(&world);
        ASSERT_TRUE(bodies[i] != UINT32_MAX);

        phys_body_t *b = phys_world_get_body(&world, bodies[i]);
        ASSERT_TRUE(b);
        b->position = (phys_vec3_t){1.0f + (float)i, 0.0f, 0.0f};

        phys_world_set_sphere_collider(&world, bodies[i], 0.5f, (phys_vec3_t){0, 0, 0});
    }

    phys_ray_t ray = {
        .origin = (phys_vec3_t){0.0f, 0.0f, 0.0f},
        .direction = (phys_vec3_t){1.0f, 0.0f, 0.0f},
        .max_distance = 10.0f,
    };

    phys_raycast_hit_t hits[8];
    memset(hits, 0, sizeof(hits));

    uint32_t n = phys_raycast_all(&world, &ray, hits, 8u, 0xFFFFFFFFu);
    ASSERT_INT_EQ(5, (int)n);
    ASSERT_INT_EQ((int)bodies[0], (int)hits[0].body_id);
    ASSERT_F32_NEAR(0.5f, hits[0].distance, 1e-5f);

    /* Also sanity-check closest-point against the same set. */
    phys_vec3_t closest = (phys_vec3_t){0};
    uint32_t closest_body = UINT32_MAX;
    ASSERT_TRUE(phys_closest_point(&world, (phys_vec3_t){0.0f, 0.0f, 0.0f},
                                  100.0f, &closest, &closest_body, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)bodies[0], (int)closest_body);
    ASSERT_F32_NEAR(0.5f, closest.x, 1e-5f);

    phys_world_destroy(&world);
    return 0;
}

static int test_overlap_trigger_volume(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body_in = phys_world_create_body(&world);
    uint32_t body_out = phys_world_create_body(&world);
    ASSERT_TRUE(body_in != UINT32_MAX && body_out != UINT32_MAX);

    phys_body_t *bi = phys_world_get_body(&world, body_in);
    phys_body_t *bo = phys_world_get_body(&world, body_out);
    ASSERT_TRUE(bi && bo);
    bi->position = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    bo->position = (phys_vec3_t){5.0f, 0.0f, 0.0f};

    phys_world_set_sphere_collider(&world, body_in, 1.0f, (phys_vec3_t){0, 0, 0});
    phys_world_set_sphere_collider(&world, body_out, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_collider_t trigger;
    alloc_query_sphere_(&world, 2.0f, &trigger);

    uint32_t out[8];
    memset(out, 0, sizeof(out));

    uint32_t n = phys_overlap(&world, &trigger,
                              (phys_vec3_t){0.0f, 0.0f, 0.0f},
                              (phys_quat_t){0, 0, 0, 1},
                              out, 8u, 0xFFFFFFFFu);

    ASSERT_INT_EQ(1, (int)n);
    ASSERT_INT_EQ((int)body_in, (int)out[0]);

    phys_world_destroy(&world);
    return 0;
}

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p087_physics_phase5_integration_tests:\n");

    RUN_TEST(test_raycast_through_stacked_bodies);
    RUN_TEST(test_overlap_trigger_volume);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
