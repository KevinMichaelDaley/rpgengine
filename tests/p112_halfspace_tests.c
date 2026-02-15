/**
 * @file p112_halfspace_tests.c
 * @brief Unit tests for PHYS_SHAPE_HALFSPACE collision primitive.
 *
 * Tests sphere, capsule, and box vs halfspace narrowphase, AABB
 * generation, collider init, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                   \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: "                    \
                    "expected %d got %d\n",                                     \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, tol)                                        \
    do {                                                                        \
        phys_vec3_t _ev = (exp), _av = (act);                                  \
        float _t = (tol);                                                      \
        if (fabsf(_ev.x - _av.x) > _t || fabsf(_ev.y - _av.y) > _t           \
            || fabsf(_ev.z - _av.z) > _t) {                                    \
            fprintf(stderr, "ASSERT_VEC3_NEAR failed: %s:%d: "                \
                    "expected (%.4f,%.4f,%.4f) got (%.4f,%.4f,%.4f)\n",         \
                    __FILE__, __LINE__,                                         \
                    (double)_ev.x, (double)_ev.y, (double)_ev.z,               \
                    (double)_av.x, (double)_av.y, (double)_av.z);              \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static const phys_quat_t QUAT_ID = {0.0f, 0.0f, 0.0f, 1.0f};

static void make_body(phys_body_t *body, float px, float py, float pz)
{
    phys_body_init(body);
    phys_body_set_mass(body, 1.0f);
    body->position = (phys_vec3_t){px, py, pz};
    body->orientation = QUAT_ID;
}

static void make_static_body(phys_body_t *body, float px, float py, float pz)
{
    phys_body_init(body);
    body->position = (phys_vec3_t){px, py, pz};
    body->orientation = QUAT_ID;
}

/* ── Collider init tests ────────────────────────────────────────── */

/** 1. phys_collider_init_halfspace sets correct type and index. */
static int test_collider_init_halfspace(void)
{
    phys_collider_t c;
    memset(&c, 0xFF, sizeof(c));

    phys_collider_init_halfspace(&c, 0, (phys_vec3_t){0, 0, 0});

    ASSERT_INT_EQ(PHYS_SHAPE_HALFSPACE, (int)c.type);
    ASSERT_INT_EQ(0, (int)c.shape_index);
    ASSERT_FLOAT_NEAR(0.0f, c.local_offset.x, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, c.local_offset.y, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, c.local_offset.z, 0.001f);
    /* Rotation should be identity. */
    ASSERT_FLOAT_NEAR(1.0f, c.local_rotation.w, 0.001f);
    return 0;
}

/** 2. phys_collider_init_halfspace is NULL-safe. */
static int test_collider_init_halfspace_null(void)
{
    phys_collider_init_halfspace(NULL, 0, (phys_vec3_t){0, 0, 0});
    return 0;  /* Didn't crash. */
}

/* ── Sphere vs Halfspace tests ──────────────────────────────────── */

/** 3. Sphere above Y=0 halfspace, penetrating by 0.5. */
static int test_sphere_vs_halfspace_overlap(void)
{
    /* Halfspace at Y=0, normal=(0,1,0).
     * Sphere at Y=0.5, radius=1.0 → penetration = 0.5. */
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 0.5f, 0.0f);   /* sphere body */
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);  /* halfspace body */

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(1, (int)candidates[0].contact_count);
    /* Penetration = radius - distance_from_plane = 1.0 - 0.5 = 0.5 */
    ASSERT_FLOAT_NEAR(0.5f, candidates[0].contacts[0].penetration, 0.01f);
    /* Normal points from shape (body_a) toward halfspace (body_b),
     * i.e. into the solid half = (0,-1,0). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, -1.0f, 0.0f}),
                     candidates[0].contacts[0].normal, 0.01f);
    return 0;
}

/** 4. Sphere fully above halfspace (no contact). */
static int test_sphere_vs_halfspace_separated(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 5.0f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);
    ASSERT_INT_EQ(0, (int)count);
    return 0;
}

/** 5. Sphere exactly touching halfspace (penetration ~0). */
static int test_sphere_vs_halfspace_touching(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 1.0f, 0.0f);  /* sphere center at radius */
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);
    ASSERT_INT_EQ(1, (int)count);
    ASSERT_FLOAT_NEAR(0.0f, candidates[0].contacts[0].penetration, 0.01f);
    return 0;
}

/** 6. Sphere deeply below halfspace. */
static int test_sphere_vs_halfspace_deep(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, -3.0f, 0.0f);  /* deep below */
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);
    ASSERT_INT_EQ(1, (int)count);
    /* pen = radius + abs(distance_below) = 1.0 + 3.0 = 4.0 */
    ASSERT_FLOAT_NEAR(4.0f, candidates[0].contacts[0].penetration, 0.01f);
    return 0;
}

/* ── Capsule vs Halfspace tests ─────────────────────────────────── */

/** 7. Upright capsule partially below Y=0 halfspace. */
static int test_capsule_vs_halfspace_overlap(void)
{
    /* Capsule: center at Y=0.5, half_height=1.0, radius=0.3
     * Axis +Y → bottom endpoint at Y=-0.5, bottom sphere surface at Y=-0.8
     * Penetration = 0.0 - (-0.8) = 0.8 */
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 0.5f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_capsule_t capsules[1] = {{.radius = 0.3f, .half_height = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_capsule(&colliders[0], 0,
                                (phys_vec3_t){0, 0, 0}, QUAT_ID);
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .capsules = capsules,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(1, (int)candidates[0].contact_count);
    /* Deepest point is at bottom of capsule: Y = 0.5 - 1.0 - 0.3 = -0.8
     * Penetration = 0.8 */
    ASSERT_FLOAT_NEAR(0.8f, candidates[0].contacts[0].penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, -1.0f, 0.0f}),
                     candidates[0].contacts[0].normal, 0.01f);
    return 0;
}

/** 8. Capsule fully above halfspace (no contact). */
static int test_capsule_vs_halfspace_separated(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 5.0f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_capsule_t capsules[1] = {{.radius = 0.3f, .half_height = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_capsule(&colliders[0], 0,
                                (phys_vec3_t){0, 0, 0}, QUAT_ID);
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .capsules = capsules,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);
    ASSERT_INT_EQ(0, (int)count);
    return 0;
}

/* ── Box vs Halfspace tests ─────────────────────────────────────── */

/** 9. Axis-aligned box partially below Y=0 halfspace. */
static int test_box_vs_halfspace_overlap(void)
{
    /* Box center at Y=0.3, half_extents=(1,0.5,1) → bottom at Y=-0.2
     * Penetration = 0.2 */
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 0.3f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_box_t boxes[1] = {{.half_extents = {1.0f, 0.5f, 1.0f}}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_box(&colliders[0], 0,
                            (phys_vec3_t){0, 0, 0}, QUAT_ID);
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .boxes = boxes,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    /* Box vs halfspace can produce up to 4 contacts (vertices below plane).
     * Bottom face has 4 vertices all at Y = 0.3 - 0.5 = -0.2
     * All 4 penetrate by 0.2. */
    ASSERT_TRUE(candidates[0].contact_count >= 1);
    ASSERT_TRUE(candidates[0].contact_count <= 4);
    /* All contacts should have penetration ~0.2 and normal (0,-1,0). */
    for (int j = 0; j < candidates[0].contact_count; j++) {
        ASSERT_FLOAT_NEAR(0.2f, candidates[0].contacts[j].penetration, 0.01f);
        ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, -1.0f, 0.0f}),
                         candidates[0].contacts[j].normal, 0.01f);
    }
    return 0;
}

/** 10. Box fully above halfspace (no contact). */
static int test_box_vs_halfspace_separated(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 5.0f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_box_t boxes[1] = {{.half_extents = {1.0f, 0.5f, 1.0f}}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_box(&colliders[0], 0,
                            (phys_vec3_t){0, 0, 0}, QUAT_ID);
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .boxes = boxes,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);
    ASSERT_INT_EQ(0, (int)count);
    return 0;
}

/* ── Tilted halfspace tests ─────────────────────────────────────── */

/** 11. Sphere vs tilted halfspace (normal not axis-aligned). */
static int test_sphere_vs_tilted_halfspace(void)
{
    /* Halfspace: normal=(0,0.707,0.707), distance=0 (plane through origin at 45°).
     * Sphere at (0,0,0), radius=1.0.
     * Signed distance = dot(center, normal) - distance = 0.
     * Penetration = radius - signed_dist = 1.0. */
    float inv_sqrt2 = 0.70710678f;

    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, inv_sqrt2, inv_sqrt2},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_FLOAT_NEAR(1.0f, candidates[0].contacts[0].penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, -inv_sqrt2, -inv_sqrt2}),
                     candidates[0].contacts[0].normal, 0.01f);
    return 0;
}

/** 12. Halfspace with nonzero distance offset. */
static int test_sphere_vs_halfspace_offset(void)
{
    /* Halfspace: normal=(0,1,0), distance=2.0 → plane at Y=2.
     * Sphere at Y=2.3, radius=0.5 → signed_dist = 2.3 - 2.0 = 0.3
     * Penetration = 0.5 - 0.3 = 0.2 */
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 2.3f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 0.5f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 2.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_FLOAT_NEAR(0.2f, candidates[0].contacts[0].penetration, 0.01f);
    return 0;
}

/* ── Speculative margin test ────────────────────────────────────── */

/** 13. Sphere just above halfspace, within speculative margin. */
static int test_sphere_vs_halfspace_speculative(void)
{
    /* Sphere at Y=1.1, radius=1.0 → signed_dist = 1.1
     * Gap = 1.1 - 1.0 = 0.1.  Speculative margin = 0.2 → should generate contact.
     * Penetration = -(gap) = -0.1 (negative = speculative). */
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 1.1f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};
    phys_halfspace_t halfspaces[1] = {{
        .normal = {0.0f, 1.0f, 0.0f},
        .distance = 0.0f
    }};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
        .speculative_margin = 0.2f,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    /* Penetration should be negative (speculative). */
    ASSERT_FLOAT_NEAR(-0.1f, candidates[0].contacts[0].penetration, 0.01f);
    return 0;
}

/* ── Halfspace-Halfspace should be ignored ──────────────────────── */

/** 14. Two halfspaces paired should produce no contacts. */
static int test_halfspace_vs_halfspace_no_contact(void)
{
    phys_body_t bodies[2];
    make_static_body(&bodies[0], 0.0f, 0.0f, 0.0f);
    make_static_body(&bodies[1], 0.0f, 0.0f, 0.0f);

    phys_halfspace_t halfspaces[2] = {
        {.normal = {0.0f, 1.0f, 0.0f}, .distance = 0.0f},
        {.normal = {0.0f, -1.0f, 0.0f}, .distance = -1.0f},
    };

    phys_collider_t colliders[2];
    phys_collider_init_halfspace(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_halfspace(&colliders[1], 1, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};
    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .halfspaces = halfspaces,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);
    ASSERT_INT_EQ(0, (int)count);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p112_halfspace_tests\n");

    RUN_TEST(test_collider_init_halfspace);
    RUN_TEST(test_collider_init_halfspace_null);
    RUN_TEST(test_sphere_vs_halfspace_overlap);
    RUN_TEST(test_sphere_vs_halfspace_separated);
    RUN_TEST(test_sphere_vs_halfspace_touching);
    RUN_TEST(test_sphere_vs_halfspace_deep);
    RUN_TEST(test_capsule_vs_halfspace_overlap);
    RUN_TEST(test_capsule_vs_halfspace_separated);
    RUN_TEST(test_box_vs_halfspace_overlap);
    RUN_TEST(test_box_vs_halfspace_separated);
    RUN_TEST(test_sphere_vs_tilted_halfspace);
    RUN_TEST(test_sphere_vs_halfspace_offset);
    RUN_TEST(test_sphere_vs_halfspace_speculative);
    RUN_TEST(test_halfspace_vs_halfspace_no_contact);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
