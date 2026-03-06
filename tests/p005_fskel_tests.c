/**
 * @file p005_fskel_tests.c
 * @brief Tests for .fskel binary format (write + load round-trip).
 *
 * Validates format header, chunk integrity, round-trip fidelity,
 * corruption detection, and edge cases (empty, large skeletons).
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "ferrum/animation/fskel_format.h"
#include "ferrum/animation/fskel_loader.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/* ── Test harness ────────────────────────────────────────────────── */

static int g_pass, g_fail;

#define RUN(fn) do {                                    \
    printf("RUN  " #fn "\n");                           \
    int rc = fn();                                      \
    if (rc == 0) { printf("  OK " #fn "\n"); g_pass++; } \
    else { printf("FAIL " #fn "\n"); g_fail++; }        \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_FLOAT_EQ failed: %s:%d: %f != %f\n", \
               __FILE__, __LINE__, (double)(a), (double)(b)); \
        return 1; \
    } \
} while (0)

/* ── Helpers ─────────────────────────────────────────────────────── */

static const char *tmp_path = "/tmp/test_skeleton.fskel";

/** Build a simple 3-joint skeleton for testing. */
static void make_test_skeleton(skeleton_def_t *skel) {
    skeleton_def_init(skel, 3, 4);

    snprintf(skel->joint_names[0], SKELETON_JOINT_NAME_MAX, "root");
    snprintf(skel->joint_names[1], SKELETON_JOINT_NAME_MAX, "spine");
    snprintf(skel->joint_names[2], SKELETON_JOINT_NAME_MAX, "head");

    skel->parent_indices[0] = UINT32_MAX;
    skel->parent_indices[1] = 0;
    skel->parent_indices[2] = 1;

    skel->rest_local[0] = mat4_identity();
    skel->rest_local[1] = mat4_translation(0.f, 1.f, 0.f);
    skel->rest_local[2] = mat4_translation(0.f, 0.5f, 0.f);

    skel->rest_world[0] = mat4_identity();
    skel->rest_world[1] = mat4_translation(0.f, 1.f, 0.f);
    skel->rest_world[2] = mat4_translation(0.f, 1.5f, 0.f);

    /* Add a limit rotation constraint to joint 1. */
    skel->constraint_counts[1] = 1;
    constraint_def_t *c = &skel->constraints[1 * skel->max_constraints_per_joint];
    memset(c, 0, sizeof(*c));
    c->type = CONSTRAINT_LIMIT_ROTATION;
    c->influence = 0.8f;
    c->target_bone_idx = UINT32_MAX;
    c->params.limit_rotation.use_limit_x = true;
    c->params.limit_rotation.min_x = -1.0f;
    c->params.limit_rotation.max_x = 1.0f;
}

/** Build IBMs (3 identity matrices). */
static void make_test_ibms(mat4_t *ibms) {
    ibms[0] = mat4_identity();
    ibms[1] = mat4_translation(0.f, -1.f, 0.f);
    ibms[2] = mat4_translation(0.f, -1.5f, 0.f);
}

/* ═══════════════════════════════════════════════════════════════════
 *  TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/** Write and load produces identical skeleton. */
static int test_round_trip(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    mat4_t ibms[3];
    make_test_ibms(ibms);

    bool ok = fskel_write(tmp_path, &skel, ibms, 3);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);

    /* Compare skeleton. */
    ASSERT_TRUE(loaded.joint_count == skel.joint_count);
    ASSERT_TRUE(loaded.max_constraints_per_joint == skel.max_constraints_per_joint);

    for (uint32_t i = 0; i < skel.joint_count; i++) {
        ASSERT_TRUE(strcmp(loaded.joint_names[i], skel.joint_names[i]) == 0);
        ASSERT_TRUE(loaded.parent_indices[i] == skel.parent_indices[i]);

        for (int j = 0; j < 16; j++) {
            ASSERT_FLOAT_EQ(loaded.rest_local[i].m[j], skel.rest_local[i].m[j], 1e-6f);
            ASSERT_FLOAT_EQ(loaded.rest_world[i].m[j], skel.rest_world[i].m[j], 1e-6f);
        }
    }

    /* Compare constraints. */
    ASSERT_TRUE(loaded.constraint_counts[0] == 0);
    ASSERT_TRUE(loaded.constraint_counts[1] == 1);
    ASSERT_TRUE(loaded.constraint_counts[2] == 0);

    constraint_def_t *lc = &loaded.constraints[1 * loaded.max_constraints_per_joint];
    ASSERT_TRUE(lc->type == CONSTRAINT_LIMIT_ROTATION);
    ASSERT_FLOAT_EQ(lc->influence, 0.8f, 1e-6f);
    ASSERT_TRUE(lc->params.limit_rotation.use_limit_x == true);
    ASSERT_FLOAT_EQ(lc->params.limit_rotation.min_x, -1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(lc->params.limit_rotation.max_x, 1.0f, 1e-6f);

    /* Compare IBMs. */
    ASSERT_TRUE(loaded_ibm_count == 3);
    for (uint32_t i = 0; i < 3; i++) {
        for (int j = 0; j < 16; j++) {
            ASSERT_FLOAT_EQ(loaded_ibms[i].m[j], ibms[i].m[j], 1e-6f);
        }
    }

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** Format validates magic number. */
static int test_bad_magic(void) {
    /* Write a valid file, then corrupt the magic. */
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    mat4_t ibms[3];
    make_test_ibms(ibms);

    fskel_write(tmp_path, &skel, ibms, 3);

    /* Corrupt magic bytes. */
    FILE *f = fopen(tmp_path, "r+b");
    ASSERT_TRUE(f != NULL);
    uint32_t bad_magic = 0xDEADBEEF;
    fwrite(&bad_magic, 4, 1, f);
    fclose(f);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);

    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** Truncated file is rejected. */
static int test_truncated_file(void) {
    /* Write minimal valid data, then truncate. */
    FILE *f = fopen(tmp_path, "wb");
    ASSERT_TRUE(f != NULL);
    uint32_t magic = FSKEL_MAGIC;
    fwrite(&magic, 4, 1, f);
    /* Only 4 bytes — no version, no data. */
    fclose(f);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);

    unlink(tmp_path);
    return 0;
}

/** Skeleton with 0 constraints round-trips. */
static int test_no_constraints(void) {
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 0);

    snprintf(skel.joint_names[0], SKELETON_JOINT_NAME_MAX, "a");
    snprintf(skel.joint_names[1], SKELETON_JOINT_NAME_MAX, "b");
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_local[0] = mat4_identity();
    skel.rest_local[1] = mat4_translation(1.f, 0.f, 0.f);
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_translation(1.f, 0.f, 0.f);

    mat4_t ibms[2] = { mat4_identity(), mat4_identity() };

    bool ok = fskel_write(tmp_path, &skel, ibms, 2);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(loaded.joint_count == 2);
    ASSERT_TRUE(loaded.max_constraints_per_joint == 0);
    ASSERT_TRUE(strcmp(loaded.joint_names[0], "a") == 0);
    ASSERT_TRUE(strcmp(loaded.joint_names[1], "b") == 0);

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** Multiple constraints on one joint. */
static int test_multi_constraints(void) {
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);

    snprintf(skel.joint_names[0], SKELETON_JOINT_NAME_MAX, "root");
    snprintf(skel.joint_names[1], SKELETON_JOINT_NAME_MAX, "child");
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_local[0] = mat4_identity();
    skel.rest_local[1] = mat4_translation(0.f, 1.f, 0.f);
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_translation(0.f, 1.f, 0.f);

    /* 3 constraints on joint 1. */
    skel.constraint_counts[1] = 3;
    constraint_def_t *base = &skel.constraints[1 * skel.max_constraints_per_joint];

    /* Constraint 0: limit rotation. */
    memset(&base[0], 0, sizeof(base[0]));
    base[0].type = CONSTRAINT_LIMIT_ROTATION;
    base[0].influence = 1.0f;
    base[0].target_bone_idx = UINT32_MAX;

    /* Constraint 1: copy location. */
    memset(&base[1], 0, sizeof(base[1]));
    base[1].type = CONSTRAINT_COPY_LOCATION;
    base[1].influence = 0.5f;
    base[1].target_bone_idx = 0;

    /* Constraint 2: IK. */
    memset(&base[2], 0, sizeof(base[2]));
    base[2].type = CONSTRAINT_IK;
    base[2].influence = 1.0f;
    base[2].target_bone_idx = 0;
    base[2].params.ik.chain_length = 3;
    base[2].params.ik.iterations = 10;

    mat4_t ibms[2] = { mat4_identity(), mat4_identity() };

    bool ok = fskel_write(tmp_path, &skel, ibms, 2);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(loaded.constraint_counts[1] == 3);

    constraint_def_t *lc = &loaded.constraints[1 * loaded.max_constraints_per_joint];
    ASSERT_TRUE(lc[0].type == CONSTRAINT_LIMIT_ROTATION);
    ASSERT_TRUE(lc[1].type == CONSTRAINT_COPY_LOCATION);
    ASSERT_FLOAT_EQ(lc[1].influence, 0.5f, 1e-6f);
    ASSERT_TRUE(lc[2].type == CONSTRAINT_IK);
    ASSERT_TRUE(lc[2].params.ik.chain_length == 3);

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** NULL arguments are handled gracefully. */
static int test_null_safety(void) {
    bool ok = fskel_write(NULL, NULL, NULL, 0);
    ASSERT_TRUE(!ok);

    ok = fskel_write(tmp_path, NULL, NULL, 0);
    ASSERT_TRUE(!ok);

    skeleton_def_t loaded;
    ok = fskel_load(NULL, &loaded, NULL, NULL);
    ASSERT_TRUE(!ok);

    ok = fskel_load(tmp_path, NULL, NULL, NULL);
    ASSERT_TRUE(!ok);

    return 0;
}

/** Nonexistent file fails gracefully. */
static int test_missing_file(void) {
    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load("/tmp/nonexistent_skeleton.fskel",
                          &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);
    return 0;
}

/** Validate format version check. */
static int test_bad_version(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    mat4_t ibms[3];
    make_test_ibms(ibms);

    fskel_write(tmp_path, &skel, ibms, 3);

    /* Corrupt version (offset 4). */
    FILE *f = fopen(tmp_path, "r+b");
    ASSERT_TRUE(f != NULL);
    fseek(f, 4, SEEK_SET);
    uint32_t bad_version = 9999;
    fwrite(&bad_version, 4, 1, f);
    fclose(f);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);

    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_round_trip);
    RUN(test_bad_magic);
    RUN(test_truncated_file);
    RUN(test_no_constraints);
    RUN(test_multi_constraints);
    RUN(test_null_safety);
    RUN(test_missing_file);
    RUN(test_bad_version);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
