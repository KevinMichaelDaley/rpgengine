#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/closest_point.h"
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

#define ASSERT_VEC3_NEAR(exp, act, eps)                                                                   \
    do {                                                                                                 \
        ASSERT_F32_NEAR((exp).x, (act).x, (eps));                                                         \
        ASSERT_F32_NEAR((exp).y, (act).y, (eps));                                                         \
        ASSERT_F32_NEAR((exp).z, (act).z, (eps));                                                         \
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

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_closest_point_on_sphere(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body = phys_world_create_body(&world);
    ASSERT_TRUE(body != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body);
    ASSERT_TRUE(b);
    b->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_world_set_sphere_collider(&world, body, 1.0f, (phys_vec3_t){0, 0, 0});

    phys_vec3_t closest = (phys_vec3_t){0};
    uint32_t hit_body = UINT32_MAX;

    ASSERT_TRUE(phys_closest_point(&world, (phys_vec3_t){3.0f, 0.0f, 0.0f},
                                  10.0f, &closest, &hit_body, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)body, (int)hit_body);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), closest, 1e-5f);

    /* Too small radius: distance to surface is 2.0. */
    ASSERT_TRUE(!phys_closest_point(&world, (phys_vec3_t){3.0f, 0.0f, 0.0f},
                                   1.9f, &closest, &hit_body, 0xFFFFFFFFu));

    phys_world_destroy(&world);
    return 0;
}

static int test_closest_point_on_box_face(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body = phys_world_create_body(&world);
    ASSERT_TRUE(body != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body);
    ASSERT_TRUE(b);
    b->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_world_set_box_collider(&world, body, (phys_vec3_t){1.0f, 2.0f, 3.0f},
                               (phys_vec3_t){0, 0, 0}, (phys_quat_t){0, 0, 0, 1});

    phys_vec3_t closest = (phys_vec3_t){0};
    uint32_t hit_body = UINT32_MAX;

    ASSERT_TRUE(phys_closest_point(&world, (phys_vec3_t){3.0f, 0.0f, 0.0f},
                                  10.0f, &closest, &hit_body, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)body, (int)hit_body);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), closest, 1e-5f);

    phys_world_destroy(&world);
    return 0;
}

static int test_closest_point_on_capsule(void) {
    phys_world_t world;
    ASSERT_INT_EQ(0, make_world_(&world));

    uint32_t body = phys_world_create_body(&world);
    ASSERT_TRUE(body != UINT32_MAX);

    phys_body_t *b = phys_world_get_body(&world, body);
    ASSERT_TRUE(b);
    b->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_world_set_capsule_collider(&world, body, 0.5f, 1.0f,
                                   (phys_vec3_t){0, 0, 0}, (phys_quat_t){0, 0, 0, 1});

    phys_vec3_t closest = (phys_vec3_t){0};
    uint32_t hit_body = UINT32_MAX;

    ASSERT_TRUE(phys_closest_point(&world, (phys_vec3_t){2.0f, 0.0f, 0.0f},
                                  10.0f, &closest, &hit_body, 0xFFFFFFFFu));
    ASSERT_INT_EQ((int)body, (int)hit_body);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.5f, 0.0f, 0.0f}), closest, 1e-5f);

    phys_world_destroy(&world);
    return 0;
}

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p086_physics_closest_point_tests:\n");

    RUN_TEST(test_closest_point_on_sphere);
    RUN_TEST(test_closest_point_on_box_face);
    RUN_TEST(test_closest_point_on_capsule);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
