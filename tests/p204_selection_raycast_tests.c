/**
 * @file p204_selection_raycast_tests.c
 * @brief Tests for selection raycast math: ray-AABB, ray-sphere,
 *        frustum-AABB intersection tests.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/selection_raycast.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _d = (float)(exp) - (float)(act);                                \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tol)) {                                                     \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "              \
                    "expected %.6f got %.6f (tol %.6f)\n",                    \
                    __FILE__, __LINE__, (double)(exp), (double)(act),          \
                    (double)(tol));                                            \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Ray-AABB tests ---- */

static int test_ray_aabb_hit_center(void) {
    /* Ray from (0,0,5) pointing at origin hits AABB centered at origin. */
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };
    vec3_t aabb_min = {-1.0f, -1.0f, -1.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 1.0f};
    float t_hit;

    ASSERT_TRUE(ray_intersect_aabb(&ray, aabb_min, aabb_max, &t_hit));
    ASSERT_TRUE(t_hit > 0.0f);
    ASSERT_FLOAT_NEAR(4.0f, t_hit, 0.01f);

    return 0;
}

static int test_ray_aabb_miss(void) {
    /* Ray going away from AABB. */
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, 1.0f}
    };
    vec3_t aabb_min = {-1.0f, -1.0f, -1.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 1.0f};
    float t_hit;

    ASSERT_TRUE(!ray_intersect_aabb(&ray, aabb_min, aabb_max, &t_hit));

    return 0;
}

static int test_ray_aabb_miss_parallel(void) {
    /* Ray parallel to AABB face, outside. */
    editor_ray_t ray = {
        .origin = {5.0f, 0.0f, 0.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };
    vec3_t aabb_min = {-1.0f, -1.0f, -1.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 1.0f};
    float t_hit;

    ASSERT_TRUE(!ray_intersect_aabb(&ray, aabb_min, aabb_max, &t_hit));

    return 0;
}

static int test_ray_aabb_origin_inside(void) {
    /* Ray origin inside AABB — should still hit (t >= 0). */
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 0.0f},
        .direction = {1.0f, 0.0f, 0.0f}
    };
    vec3_t aabb_min = {-1.0f, -1.0f, -1.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 1.0f};
    float t_hit;

    ASSERT_TRUE(ray_intersect_aabb(&ray, aabb_min, aabb_max, &t_hit));
    /* t_hit should be 0 or the exit distance. */
    ASSERT_TRUE(t_hit >= 0.0f);

    return 0;
}

static int test_ray_aabb_grazing(void) {
    /* Ray grazing the edge of the AABB. */
    editor_ray_t ray = {
        .origin = {1.0f, 1.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };
    vec3_t aabb_min = {-1.0f, -1.0f, -1.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 1.0f};
    float t_hit;

    /* On the edge — either hit or miss is acceptable, just no crash. */
    (void)ray_intersect_aabb(&ray, aabb_min, aabb_max, &t_hit);

    return 0;
}

/* ---- Ray-Sphere tests ---- */

static int test_ray_sphere_hit(void) {
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };
    vec3_t center = {0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    float t_hit;

    ASSERT_TRUE(ray_intersect_sphere(&ray, center, radius, &t_hit));
    ASSERT_FLOAT_NEAR(4.0f, t_hit, 0.01f);

    return 0;
}

static int test_ray_sphere_miss(void) {
    editor_ray_t ray = {
        .origin = {5.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };
    vec3_t center = {0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    float t_hit;

    ASSERT_TRUE(!ray_intersect_sphere(&ray, center, radius, &t_hit));

    return 0;
}

static int test_ray_sphere_origin_inside(void) {
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 0.0f},
        .direction = {1.0f, 0.0f, 0.0f}
    };
    vec3_t center = {0.0f, 0.0f, 0.0f};
    float radius = 2.0f;
    float t_hit;

    ASSERT_TRUE(ray_intersect_sphere(&ray, center, radius, &t_hit));
    ASSERT_TRUE(t_hit >= 0.0f);

    return 0;
}

static int test_ray_sphere_tangent(void) {
    /* Ray tangent to sphere — borderline. */
    editor_ray_t ray = {
        .origin = {1.0f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };
    vec3_t center = {0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    float t_hit;

    /* Tangent — either hit or miss is acceptable, no crash. */
    (void)ray_intersect_sphere(&ray, center, radius, &t_hit);

    return 0;
}

/* ---- Frustum-AABB tests ---- */

static int test_frustum_aabb_inside(void) {
    /* Build a view frustum that looks at origin from (0,0,10). */
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_frustum_t frustum;
    float aspect = 16.0f / 9.0f;
    int rc = editor_frustum_from_camera(&cam, aspect, &frustum);
    ASSERT_TRUE(rc == 0);

    /* AABB at origin should be inside frustum. */
    vec3_t aabb_min = {-1.0f, -1.0f, -1.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 1.0f};

    ASSERT_TRUE(frustum_intersect_aabb(&frustum, aabb_min, aabb_max));

    return 0;
}

static int test_frustum_aabb_outside_behind(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_frustum_t frustum;
    int rc = editor_frustum_from_camera(&cam, 1.0f, &frustum);
    ASSERT_TRUE(rc == 0);

    /* AABB far behind the camera. */
    vec3_t aabb_min = {-1.0f, -1.0f, 50.0f};
    vec3_t aabb_max = {1.0f, 1.0f, 52.0f};

    ASSERT_TRUE(!frustum_intersect_aabb(&frustum, aabb_min, aabb_max));

    return 0;
}

static int test_frustum_aabb_outside_side(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_frustum_t frustum;
    int rc = editor_frustum_from_camera(&cam, 1.0f, &frustum);
    ASSERT_TRUE(rc == 0);

    /* AABB far to the side. */
    vec3_t aabb_min = {1000.0f, 0.0f, -5.0f};
    vec3_t aabb_max = {1001.0f, 1.0f, -4.0f};

    ASSERT_TRUE(!frustum_intersect_aabb(&frustum, aabb_min, aabb_max));

    return 0;
}

static int test_frustum_aabb_partial_overlap(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_frustum_t frustum;
    int rc = editor_frustum_from_camera(&cam, 1.0f, &frustum);
    ASSERT_TRUE(rc == 0);

    /* Large AABB that encompasses the near plane area. */
    vec3_t aabb_min = {-100.0f, -100.0f, -100.0f};
    vec3_t aabb_max = {100.0f, 100.0f, 100.0f};

    ASSERT_TRUE(frustum_intersect_aabb(&frustum, aabb_min, aabb_max));

    return 0;
}

/* ---- Pick nearest entity ---- */

static int test_pick_nearest_basic(void) {
    /* Two AABBs along the -Z axis. Closer one should be picked. */
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 10.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };

    pick_candidate_t candidates[2] = {
        {.entity_id = 0, .aabb_min = {-1,-1,4}, .aabb_max = {1,1,6}},
        {.entity_id = 1, .aabb_min = {-1,-1,-1}, .aabb_max = {1,1,1}},
    };

    uint32_t hit_id;
    ASSERT_TRUE(pick_nearest_entity(&ray, candidates, 2, &hit_id));
    ASSERT_TRUE(hit_id == 0);

    return 0;
}

static int test_pick_nearest_miss_all(void) {
    editor_ray_t ray = {
        .origin = {100.0f, 0.0f, 10.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };

    pick_candidate_t candidates[1] = {
        {.entity_id = 0, .aabb_min = {-1,-1,-1}, .aabb_max = {1,1,1}},
    };

    uint32_t hit_id;
    ASSERT_TRUE(!pick_nearest_entity(&ray, candidates, 1, &hit_id));

    return 0;
}

static int test_pick_nearest_empty(void) {
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 10.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };

    uint32_t hit_id;
    ASSERT_TRUE(!pick_nearest_entity(&ray, NULL, 0, &hit_id));

    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"ray_aabb_hit_center",        test_ray_aabb_hit_center},
    {"ray_aabb_miss",              test_ray_aabb_miss},
    {"ray_aabb_miss_parallel",     test_ray_aabb_miss_parallel},
    {"ray_aabb_origin_inside",     test_ray_aabb_origin_inside},
    {"ray_aabb_grazing",           test_ray_aabb_grazing},
    {"ray_sphere_hit",             test_ray_sphere_hit},
    {"ray_sphere_miss",            test_ray_sphere_miss},
    {"ray_sphere_origin_inside",   test_ray_sphere_origin_inside},
    {"ray_sphere_tangent",         test_ray_sphere_tangent},
    {"frustum_aabb_inside",        test_frustum_aabb_inside},
    {"frustum_aabb_outside_behind", test_frustum_aabb_outside_behind},
    {"frustum_aabb_outside_side",  test_frustum_aabb_outside_side},
    {"frustum_aabb_partial_overlap", test_frustum_aabb_partial_overlap},
    {"pick_nearest_basic",         test_pick_nearest_basic},
    {"pick_nearest_miss_all",      test_pick_nearest_miss_all},
    {"pick_nearest_empty",         test_pick_nearest_empty},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
