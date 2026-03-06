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
#include "ferrum/animation/bone_collider.h"
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

/** Collider descriptors round-trip through write/load. */
static int test_collider_round_trip(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    mat4_t ibms[3];
    make_test_ibms(ibms);

    /* Allocate colliders. */
    skel.colliders = (bone_collider_desc_t *)calloc(3, sizeof(bone_collider_desc_t));
    ASSERT_TRUE(skel.colliders != NULL);

    /* Root: capsule */
    skel.colliders[0].shape_type = BONE_COLLIDER_CAPSULE;
    skel.colliders[0].params[0] = 0.15f; /* radius */
    skel.colliders[0].params[1] = 0.6f;  /* height */
    skel.colliders[0].params[2] = 1.0f;  /* axis Y */
    skel.colliders[0].ccd_enabled = 0;
    skel.colliders[0].is_kinematic = 1;
    skel.colliders[0].mass = 0.0f;

    /* Spine: box */
    skel.colliders[1].shape_type = BONE_COLLIDER_BOX;
    skel.colliders[1].params[0] = 0.2f;
    skel.colliders[1].params[1] = 0.3f;
    skel.colliders[1].params[2] = 0.15f;
    skel.colliders[1].ccd_enabled = 1;
    skel.colliders[1].is_kinematic = 0;
    skel.colliders[1].mass = 5.0f;

    /* Head: sphere */
    skel.colliders[2].shape_type = BONE_COLLIDER_SPHERE;
    skel.colliders[2].params[0] = 0.12f;
    skel.colliders[2].is_kinematic = 0;
    skel.colliders[2].mass = 2.5f;

    bool ok = fskel_write(tmp_path, &skel, ibms, 3);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);

    /* Verify colliders loaded. */
    ASSERT_TRUE(loaded.colliders != NULL);
    ASSERT_TRUE(loaded.colliders[0].shape_type == BONE_COLLIDER_CAPSULE);
    ASSERT_FLOAT_EQ(loaded.colliders[0].params[0], 0.15f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.colliders[0].params[1], 0.6f, 1e-6f);
    ASSERT_TRUE(loaded.colliders[0].is_kinematic == 1);

    ASSERT_TRUE(loaded.colliders[1].shape_type == BONE_COLLIDER_BOX);
    ASSERT_TRUE(loaded.colliders[1].ccd_enabled == 1);
    ASSERT_TRUE(loaded.colliders[1].is_kinematic == 0);
    ASSERT_FLOAT_EQ(loaded.colliders[1].mass, 5.0f, 1e-6f);

    ASSERT_TRUE(loaded.colliders[2].shape_type == BONE_COLLIDER_SPHERE);
    ASSERT_FLOAT_EQ(loaded.colliders[2].params[0], 0.12f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.colliders[2].mass, 2.5f, 1e-6f);

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** Convex hull vertex data round-trips through write/load. */
static int test_hull_vertex_round_trip(void) {
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 0);
    snprintf(skel.joint_names[0], SKELETON_JOINT_NAME_MAX, "root");
    snprintf(skel.joint_names[1], SKELETON_JOINT_NAME_MAX, "child");
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;

    /* Set up colliders with convex hull on joint 1. */
    skel.colliders = (bone_collider_desc_t *)calloc(2, sizeof(bone_collider_desc_t));
    ASSERT_TRUE(skel.colliders != NULL);
    skel.colliders[0].shape_type = BONE_COLLIDER_NONE;
    skel.colliders[1].shape_type = BONE_COLLIDER_CONVEX_HULL;
    skel.colliders[1].hull_offset = 0;
    skel.colliders[1].hull_count = 4;

    /* 4 vertices = tetrahedron. */
    skel.hull_vertex_count = 4;
    skel.hull_vertices = (float *)calloc(4 * 3, sizeof(float));
    ASSERT_TRUE(skel.hull_vertices != NULL);
    float verts[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.5f, 1.0f, 0.0f,
        0.5f, 0.5f, 1.0f,
    };
    memcpy(skel.hull_vertices, verts, sizeof(verts));

    mat4_t ibms[2] = { mat4_identity(), mat4_identity() };
    bool ok = fskel_write(tmp_path, &skel, ibms, 2);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);

    ASSERT_TRUE(loaded.hull_vertex_count == 4);
    ASSERT_TRUE(loaded.hull_vertices != NULL);
    ASSERT_TRUE(loaded.colliders[1].shape_type == BONE_COLLIDER_CONVEX_HULL);
    ASSERT_TRUE(loaded.colliders[1].hull_count == 4);

    for (int i = 0; i < 12; i++) {
        ASSERT_FLOAT_EQ(loaded.hull_vertices[i], verts[i], 1e-6f);
    }

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** v1 files load with NULL colliders (backward compat). */
static int test_v1_backward_compat(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    mat4_t ibms[3];
    make_test_ibms(ibms);

    /* Write a v2 file, then patch version to 1 and truncate COLL data. */
    bool ok = fskel_write(tmp_path, &skel, ibms, 3);
    ASSERT_TRUE(ok);

    /* Get file size of v1 portion (before COLL chunk).
     * Header: 20 bytes
     * Names: 3 × 64 = 192
     * Parents: 3 × 4 = 12
     * rest_local: 3 × 64 = 192
     * rest_world: 3 × 64 = 192
     * constraint_counts: 3 × 4 = 12
     * constraints: 3 × 4 × 224 = 2688
     * IBMs: 3 × 64 = 192
     * Total v1: 20 + 192 + 12 + 192 + 192 + 12 + 2688 + 192 = 3500
     */
    size_t v1_size = 20 + (3 * 64) + (3 * 4) + (3 * 64) + (3 * 64)
                   + (3 * 4) + (3 * 4 * sizeof(constraint_def_t))
                   + (3 * 64);

    /* Read the file, truncate to v1 size, patch version. */
    FILE *f = fopen(tmp_path, "rb");
    ASSERT_TRUE(f != NULL);
    fseek(f, 0, SEEK_END);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(v1_size);
    ASSERT_TRUE(buf != NULL);
    ASSERT_TRUE(fread(buf, 1, v1_size, f) == v1_size);
    fclose(f);

    /* Patch version to 1. */
    uint32_t v1 = 1;
    memcpy(buf + 4, &v1, 4);

    /* Write truncated v1 file. */
    f = fopen(tmp_path, "wb");
    ASSERT_TRUE(f != NULL);
    fwrite(buf, 1, v1_size, f);
    fclose(f);
    free(buf);

    /* Load v1 file — should succeed with NULL colliders. */
    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(loaded.joint_count == 3);
    ASSERT_TRUE(loaded.colliders == NULL);
    ASSERT_TRUE(loaded.hull_vertices == NULL);
    ASSERT_TRUE(loaded.hull_vertex_count == 0);

    /* Skeleton data still valid. */
    ASSERT_TRUE(strcmp(loaded.joint_names[0], "root") == 0);
    ASSERT_TRUE(loaded.constraint_counts[1] == 1);

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** No-collider skeleton writes empty COLL chunk. */
static int test_no_colliders_round_trip(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    /* skel.colliders is NULL — no collision data. */
    mat4_t ibms[3];
    make_test_ibms(ibms);

    bool ok = fskel_write(tmp_path, &skel, ibms, 3);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);

    /* All colliders should be NONE. */
    ASSERT_TRUE(loaded.colliders != NULL);
    for (uint32_t i = 0; i < loaded.joint_count; i++) {
        ASSERT_TRUE(loaded.colliders[i].shape_type == BONE_COLLIDER_NONE);
    }

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
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
    RUN(test_collider_round_trip);
    RUN(test_hull_vertex_round_trip);
    RUN(test_v1_backward_compat);
    RUN(test_no_colliders_round_trip);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
