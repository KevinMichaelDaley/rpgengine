/**
 * @file skeleton_builder_tests.c
 * @brief Unit tests for the skeleton builder (dynamic bone add/remove).
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/anim/skeleton_builder.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

/* ----------------------------------------------------------------------- */
/* Test harness                                                             */
/* ----------------------------------------------------------------------- */

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n",            \
                    #expr, __FILE__, __LINE__);                               \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_UINT_EQ(a, b)                                                 \
    do {                                                                     \
        unsigned _a = (unsigned)(a), _b = (unsigned)(b);                     \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_UINT_EQ failed: %u != %u (%s:%d)\n",   \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(a, b, eps)                                         \
    do {                                                                     \
        float _a = (float)(a), _b = (float)(b);                             \
        float _d = (_a > _b) ? (_a - _b) : (_b - _a);                      \
        if (_d > (eps)) {                                                    \
            fprintf(stderr, "  ASSERT_FLOAT_NEAR failed: %f != %f (eps=%f) (%s:%d)\n", \
                    (double)_a, (double)_b, (double)(eps), __FILE__, __LINE__); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** @brief Create an empty skeleton in the registry for testing. */
static bool setup_empty_skel_(edit_skeleton_registry_t *reg,
                                const char *path) {
    skeleton_def_t skel;
    if (!skeleton_def_init(&skel, 0, 0)) {
        /* joint_count=0 may not be supported; try 1 and we'll test from there. */
        return false;
    }
    edit_skeleton_registry_add(reg, path, &skel, NULL, 0);
    return true;
}

/** @brief Create a skeleton with N bones as a chain (0→1→2→...). */
static bool setup_chain_skel_(edit_skeleton_registry_t *reg,
                                const char *path, uint32_t count) {
    skeleton_def_t skel;
    if (!skeleton_def_init(&skel, count, 0)) return false;
    for (uint32_t i = 0; i < count; i++) {
        skel.parent_indices[i] = (i == 0) ? UINT32_MAX : i - 1;
        snprintf(skel.joint_names[i], SKELETON_JOINT_NAME_MAX, "bone_%u", i);
        /* Place each bone 1 unit up from parent. */
        skel.rest_local[i] = mat4_identity();
        skel.rest_local[i].m[13] = 1.0f; /* Y translation. */
    }
    /* Root has no parent offset. */
    skel.rest_local[0].m[13] = 0.0f;
    skeleton_builder_recompute_world(&skel);
    edit_skeleton_registry_add(reg, path, &skel, NULL, 0);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test: add bone to chain                                                   */
/* ----------------------------------------------------------------------- */

static int test_add_bone(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 2));

    /* Add a child of bone 1. */
    vec3_t head = {0, 2, 0};
    vec3_t tail = {0, 3, 0};
    uint32_t new_idx = skeleton_builder_add_bone(
        &reg, "test.fskel", "new_bone", 1, head, tail);
    ASSERT_TRUE(new_idx != UINT32_MAX);
    ASSERT_UINT_EQ(new_idx, 2);

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_TRUE(se != NULL);
    ASSERT_UINT_EQ(se->skel.joint_count, 3);
    ASSERT_UINT_EQ(se->skel.parent_indices[2], 1);
    ASSERT_TRUE(strcmp(se->skel.joint_names[2], "new_bone") == 0);

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: add root bone (no parent)                                           */
/* ----------------------------------------------------------------------- */

static int test_add_root_bone(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 1));

    vec3_t head = {5, 0, 0};
    vec3_t tail = {5, 1, 0};
    uint32_t new_idx = skeleton_builder_add_bone(
        &reg, "test.fskel", "root2", UINT32_MAX, head, tail);
    ASSERT_TRUE(new_idx != UINT32_MAX);

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_UINT_EQ(se->skel.joint_count, 2);
    ASSERT_UINT_EQ(se->skel.parent_indices[new_idx], UINT32_MAX);

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: remove bone                                                         */
/* ----------------------------------------------------------------------- */

static int test_remove_bone(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 3));

    /* Remove middle bone (1). Children (bone 2) should reparent to 0. */
    ASSERT_TRUE(skeleton_builder_remove_bone(&reg, "test.fskel", 1));

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_UINT_EQ(se->skel.joint_count, 2);

    /* Original bone 2 is now bone 1 (indices shifted). Its parent should be 0. */
    ASSERT_UINT_EQ(se->skel.parent_indices[1], 0);

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: remove root bone                                                    */
/* ----------------------------------------------------------------------- */

static int test_remove_root_bone(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 3));

    /* Remove root (0). Child (1) should become root. */
    ASSERT_TRUE(skeleton_builder_remove_bone(&reg, "test.fskel", 0));

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_UINT_EQ(se->skel.joint_count, 2);
    ASSERT_UINT_EQ(se->skel.parent_indices[0], UINT32_MAX); /* Was bone 1, now root. */

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: set parent                                                          */
/* ----------------------------------------------------------------------- */

static int test_set_parent(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 3));

    /* Reparent bone 2 from bone 1 to bone 0. */
    ASSERT_TRUE(skeleton_builder_set_parent(&reg, "test.fskel", 2, 0));

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_UINT_EQ(se->skel.parent_indices[2], 0);

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: recompute world                                                     */
/* ----------------------------------------------------------------------- */

static int test_recompute_world(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 3));

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");

    /* Bone 0: root at origin. Bone 1: Y=1. Bone 2: Y=2. */
    ASSERT_FLOAT_NEAR(se->skel.rest_world[0].m[13], 0.0f, 0.01f);
    ASSERT_FLOAT_NEAR(se->skel.rest_world[1].m[13], 1.0f, 0.01f);
    ASSERT_FLOAT_NEAR(se->skel.rest_world[2].m[13], 2.0f, 0.01f);

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: add then remove returns to original count                           */
/* ----------------------------------------------------------------------- */

static int test_add_remove_roundtrip(void) {
    edit_skeleton_registry_t reg;
    ASSERT_TRUE(edit_skeleton_registry_init(&reg, 16));
    ASSERT_TRUE(setup_chain_skel_(&reg, "test.fskel", 2));

    vec3_t h = {0, 0, 0}, t = {0, 1, 0};
    uint32_t idx = skeleton_builder_add_bone(
        &reg, "test.fskel", "tmp", 0, h, t);
    ASSERT_TRUE(idx != UINT32_MAX);

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_UINT_EQ(se->skel.joint_count, 3);

    ASSERT_TRUE(skeleton_builder_remove_bone(&reg, "test.fskel", idx));

    se = edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT_UINT_EQ(se->skel.joint_count, 2);

    edit_skeleton_registry_destroy(&reg);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: null params                                                         */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    vec3_t h = {0,0,0}, t = {0,1,0};
    ASSERT_UINT_EQ(skeleton_builder_add_bone(NULL, "x", "b", 0, h, t), UINT32_MAX);
    ASSERT_FALSE(skeleton_builder_remove_bone(NULL, "x", 0));
    ASSERT_FALSE(skeleton_builder_set_parent(NULL, "x", 0, 1));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"add_bone",              test_add_bone},
    {"add_root_bone",         test_add_root_bone},
    {"remove_bone",           test_remove_bone},
    {"remove_root_bone",      test_remove_root_bone},
    {"set_parent",            test_set_parent},
    {"recompute_world",       test_recompute_world},
    {"add_remove_roundtrip",  test_add_remove_roundtrip},
    {"null_params",           test_null_params},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
