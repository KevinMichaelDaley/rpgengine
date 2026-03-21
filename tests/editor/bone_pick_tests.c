/**
 * @file bone_pick_tests.c
 * @brief Tests for ray-capsule intersection and bone picking.
 *
 * Validates ray_intersect_capsule() which tests a ray against a capsule
 * defined by two endpoints and a radius, and pick_nearest_bone() which
 * finds the closest bone hit by a ray in a skeleton.
 */

#include "ferrum/editor/viewport/bone_pick.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <stdio.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

/* ---- ray_intersect_capsule tests ---- */

/** Ray directly at a vertical capsule center. */
static void test_ray_hits_capsule_center(void) {
    editor_ray_t ray = {
        .origin = {5.0f, 0.5f, 0.0f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };
    /* Capsule from (0,0,0) to (0,1,0) with radius 0.1. */
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 1.0f, 0.0f};
    float radius = 0.1f;
    float t_hit = 0.0f;

    ASSERT(ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
    /* Hit should be around t=4.9 (5.0 - 0.1 radius). */
    ASSERT(t_hit > 4.0f);
    ASSERT(t_hit < 5.0f);
}

/** Ray that misses a capsule entirely. */
static void test_ray_misses_capsule(void) {
    editor_ray_t ray = {
        .origin = {5.0f, 5.0f, 0.0f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 1.0f, 0.0f};
    float radius = 0.1f;
    float t_hit = 0.0f;

    ASSERT(!ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
}

/** Ray hits the spherical cap at end A. */
static void test_ray_hits_cap_a(void) {
    editor_ray_t ray = {
        .origin = {0.0f, -2.0f, 0.0f},
        .direction = {0.0f, 1.0f, 0.0f}
    };
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 1.0f, 0.0f};
    float radius = 0.1f;
    float t_hit = 0.0f;

    ASSERT(ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
    /* Should hit bottom sphere cap at ~1.9. */
    ASSERT(t_hit > 1.5f);
    ASSERT(t_hit < 2.0f);
}

/** Ray hits the spherical cap at end B. */
static void test_ray_hits_cap_b(void) {
    editor_ray_t ray = {
        .origin = {0.0f, 5.0f, 0.0f},
        .direction = {0.0f, -1.0f, 0.0f}
    };
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 1.0f, 0.0f};
    float radius = 0.1f;
    float t_hit = 0.0f;

    ASSERT(ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
    /* Should hit top sphere cap at ~3.9. */
    ASSERT(t_hit > 3.5f);
    ASSERT(t_hit < 4.1f);
}

/** Ray behind the capsule (pointing away) should miss. */
static void test_ray_behind_capsule(void) {
    editor_ray_t ray = {
        .origin = {5.0f, 0.5f, 0.0f},
        .direction = {1.0f, 0.0f, 0.0f} /* Pointing away. */
    };
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 1.0f, 0.0f};
    float radius = 0.1f;
    float t_hit = 0.0f;

    ASSERT(!ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
}

/** Diagonal capsule. */
static void test_diagonal_capsule(void) {
    editor_ray_t ray = {
        .origin = {5.0f, 0.5f, 0.5f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 1.0f, 1.0f};
    float radius = 0.2f;
    float t_hit = 0.0f;

    ASSERT(ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
    ASSERT(t_hit > 0.0f);
}

/** Zero-length capsule degenerates to sphere. */
static void test_zero_length_capsule(void) {
    editor_ray_t ray = {
        .origin = {3.0f, 0.0f, 0.0f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };
    vec3_t cap_a = {0.0f, 0.0f, 0.0f};
    vec3_t cap_b = {0.0f, 0.0f, 0.0f};
    float radius = 0.5f;
    float t_hit = 0.0f;

    ASSERT(ray_intersect_capsule(&ray, cap_a, cap_b, radius, &t_hit));
    /* Should behave like sphere intersection at origin with r=0.5. */
    ASSERT_NEAR(t_hit, 2.5f, 0.1f);
}

/** NULL inputs handled gracefully. */
static void test_null_inputs(void) {
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 0.0f},
        .direction = {1.0f, 0.0f, 0.0f}
    };
    vec3_t a = {0.0f, 0.0f, 0.0f};
    vec3_t b = {1.0f, 0.0f, 0.0f};
    float t;

    ASSERT(!ray_intersect_capsule(NULL, a, b, 0.1f, &t));
    ASSERT(!ray_intersect_capsule(&ray, a, b, 0.1f, NULL));
}

/* ---- pick_nearest_bone tests ---- */

/** Pick from a simple 2-bone skeleton. */
static void test_pick_nearest_bone(void) {
    /* Two bones: one at Y=0..1, one at Y=1..2. */
    bone_pick_candidate_t candidates[2];
    candidates[0].bone_index = 0;
    candidates[0].cap_a = (vec3_t){0.0f, 0.0f, 0.0f};
    candidates[0].cap_b = (vec3_t){0.0f, 1.0f, 0.0f};
    candidates[0].radius = 0.1f;

    candidates[1].bone_index = 1;
    candidates[1].cap_a = (vec3_t){0.0f, 1.0f, 0.0f};
    candidates[1].cap_b = (vec3_t){0.0f, 2.0f, 0.0f};
    candidates[1].radius = 0.1f;

    /* Ray aimed at bone 0's center. */
    editor_ray_t ray = {
        .origin = {5.0f, 0.5f, 0.0f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };
    uint32_t picked = UINT32_MAX;
    ASSERT(pick_nearest_bone(&ray, candidates, 2, &picked));
    ASSERT(picked == 0);

    /* Ray aimed at bone 1's center. */
    ray.origin.y = 1.5f;
    ASSERT(pick_nearest_bone(&ray, candidates, 2, &picked));
    ASSERT(picked == 1);
}

/** No bones hit returns false. */
static void test_pick_no_hit(void) {
    bone_pick_candidate_t candidates[1];
    candidates[0].bone_index = 0;
    candidates[0].cap_a = (vec3_t){0.0f, 0.0f, 0.0f};
    candidates[0].cap_b = (vec3_t){0.0f, 1.0f, 0.0f};
    candidates[0].radius = 0.1f;

    editor_ray_t ray = {
        .origin = {5.0f, 10.0f, 0.0f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };
    uint32_t picked = UINT32_MAX;
    ASSERT(!pick_nearest_bone(&ray, candidates, 1, &picked));
}

/** Empty candidate list returns false. */
static void test_pick_empty(void) {
    editor_ray_t ray = {
        .origin = {0.0f, 0.0f, 0.0f},
        .direction = {1.0f, 0.0f, 0.0f}
    };
    uint32_t picked = UINT32_MAX;
    ASSERT(!pick_nearest_bone(&ray, NULL, 0, &picked));
}

/* ---- Main ---- */

int main(void) {
    printf("bone_pick_tests:\n");

    test_ray_hits_capsule_center();
    test_ray_misses_capsule();
    test_ray_hits_cap_a();
    test_ray_hits_cap_b();
    test_ray_behind_capsule();
    test_diagonal_capsule();
    test_zero_length_capsule();
    test_null_inputs();

    test_pick_nearest_bone();
    test_pick_no_hit();
    test_pick_empty();

    printf("bone_pick_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
