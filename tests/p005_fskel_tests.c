/**
 * @file p005_fskel_tests.c
 * @brief Tests for .fskel JSON format (write + load round-trip).
 *
 * Validates JSON round-trip fidelity, corruption detection,
 * and edge cases (empty, large skeletons).
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
#include "ferrum/animation/bone_joint_desc.h"
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
    /* max_constraints_per_joint is derived from actual data in JSON,
     * so it may differ from the original allocation size. */
    ASSERT_TRUE(loaded.max_constraints_per_joint >= 1);

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

/** Non-JSON content is rejected. */
static int test_invalid_json(void) {
    FILE *f = fopen(tmp_path, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "this is not JSON at all!!!");
    fclose(f);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);

    unlink(tmp_path);
    return 0;
}

/** Truncated JSON file is rejected. */
static int test_truncated_file(void) {
    FILE *f = fopen(tmp_path, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "{\"version\": 5, \"joints\": [");
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

/** Old version (< 5) is rejected. */
static int test_bad_version(void) {
    FILE *f = fopen(tmp_path, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "{\"version\": 3, \"joints\": [{\"name\":\"root\",\"parent\":-1,"
               "\"rest_local\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"
               "\"rest_world\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"
               "\"constraints\":[],\"collider\":{\"shape\":\"none\","
               "\"params\":[0,0,0,0,0,0],\"ccd\":false,\"kinematic\":false,"
               "\"mass\":0,\"hull_offset\":0,\"hull_count\":0,\"collision_group\":0},"
               "\"joint_desc\":{\"type\":0,\"axis\":[0,1,0],\"rest_length\":0,"
               "\"limit_min\":[0,0,0],\"limit_max\":[0,0,0],\"limit_axes\":0}}],"
               "\"ibms\":[],\"hull_vertices\":[]}");
    fclose(f);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);

    unlink(tmp_path);
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

/** Empty joints array is rejected. */
static int test_empty_joints(void) {
    FILE *f = fopen(tmp_path, "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "{\"version\": 5, \"joints\": [], \"ibms\": [], "
               "\"hull_vertices\": []}");
    fclose(f);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    bool ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(!ok);

    unlink(tmp_path);
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

/** Joint descriptors round-trip through write/load. */
static int test_joint_desc_round_trip(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel);
    mat4_t ibms[3];
    make_test_ibms(ibms);

    /* Allocate joint descriptors. */
    skel.joints = (bone_joint_desc_t *)calloc(3, sizeof(bone_joint_desc_t));
    ASSERT_TRUE(skel.joints != NULL);

    /* Root: no joint (root bone). */
    skel.joints[0].joint_type = 0;

    /* Spine: ball joint to root. */
    skel.joints[1].joint_type = 1;

    /* Head: hinge joint along X axis. */
    skel.joints[2].joint_type = 2;
    skel.joints[2].axis[0] = 1.0f;
    skel.joints[2].axis[1] = 0.0f;
    skel.joints[2].axis[2] = 0.0f;
    skel.joints[2].limit_min[0] = -0.5f;
    skel.joints[2].limit_max[0] = 0.8f;

    bool ok = fskel_write(tmp_path, &skel, ibms, 3);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);

    /* Verify joints loaded. */
    ASSERT_TRUE(loaded.joints != NULL);
    ASSERT_TRUE(loaded.joints[0].joint_type == 0);
    ASSERT_TRUE(loaded.joints[1].joint_type == 1);
    ASSERT_TRUE(loaded.joints[2].joint_type == 2);
    ASSERT_FLOAT_EQ(loaded.joints[2].axis[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[2].limit_min[0], -0.5f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[2].limit_max[0], 0.8f, 1e-6f);

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/** Expanded joint types (lock, copy_rot, limit_rot, limit_pos, aim) round-trip. */
static int test_expanded_joint_types_round_trip(void) {
    skeleton_def_t skel;
    skeleton_def_init(&skel, 6, 0);

    snprintf(skel.joint_names[0], SKELETON_JOINT_NAME_MAX, "root");
    snprintf(skel.joint_names[1], SKELETON_JOINT_NAME_MAX, "lock_bone");
    snprintf(skel.joint_names[2], SKELETON_JOINT_NAME_MAX, "copy_rot_bone");
    snprintf(skel.joint_names[3], SKELETON_JOINT_NAME_MAX, "limit_rot_bone");
    snprintf(skel.joint_names[4], SKELETON_JOINT_NAME_MAX, "limit_pos_bone");
    snprintf(skel.joint_names[5], SKELETON_JOINT_NAME_MAX, "aim_bone");

    skel.parent_indices[0] = UINT32_MAX;
    for (uint32_t i = 1; i < 6; i++) skel.parent_indices[i] = 0;
    for (uint32_t i = 0; i < 6; i++) {
        skel.rest_local[i] = mat4_identity();
        skel.rest_world[i] = mat4_identity();
    }

    skel.joints = (bone_joint_desc_t *)calloc(6, sizeof(bone_joint_desc_t));
    ASSERT_TRUE(skel.joints != NULL);

    /* Root: no joint. */
    skel.joints[0].joint_type = 0;

    /* Lock joint (type 4). */
    skel.joints[1].joint_type = 4;

    /* Copy rotation (type 5). */
    skel.joints[2].joint_type = 5;

    /* Limit rotation (type 6): X and Z active. */
    skel.joints[3].joint_type = 6;
    skel.joints[3].limit_min[0] = -1.5f;
    skel.joints[3].limit_max[0] = 1.5f;
    skel.joints[3].limit_min[2] = -0.3f;
    skel.joints[3].limit_max[2] = 0.7f;
    skel.joints[3].limit_axes = 5;  /* 0b101 = X + Z */

    /* Limit position (type 7): all 3 axes. */
    skel.joints[4].joint_type = 7;
    skel.joints[4].limit_min[0] = -2.0f;
    skel.joints[4].limit_max[0] = 2.0f;
    skel.joints[4].limit_min[1] = -1.0f;
    skel.joints[4].limit_max[1] = 3.0f;
    skel.joints[4].limit_min[2] = 0.0f;
    skel.joints[4].limit_max[2] = 5.0f;
    skel.joints[4].limit_axes = 7;  /* 0b111 = X + Y + Z */

    /* Aim joint (type 8): track Y axis. */
    skel.joints[5].joint_type = 8;
    skel.joints[5].axis[0] = 0.0f;
    skel.joints[5].axis[1] = 1.0f;
    skel.joints[5].axis[2] = 0.0f;

    mat4_t ibms[6];
    for (int i = 0; i < 6; i++) ibms[i] = mat4_identity();

    bool ok = fskel_write(tmp_path, &skel, ibms, 6);
    ASSERT_TRUE(ok);

    skeleton_def_t loaded;
    mat4_t *loaded_ibms = NULL;
    uint32_t loaded_ibm_count = 0;
    ok = fskel_load(tmp_path, &loaded, &loaded_ibms, &loaded_ibm_count);
    ASSERT_TRUE(ok);

    ASSERT_TRUE(loaded.joints != NULL);
    ASSERT_TRUE(loaded.joints[0].joint_type == 0);
    ASSERT_TRUE(loaded.joints[1].joint_type == 4);
    ASSERT_TRUE(loaded.joints[2].joint_type == 5);
    ASSERT_TRUE(loaded.joints[3].joint_type == 6);
    ASSERT_TRUE(loaded.joints[4].joint_type == 7);
    ASSERT_TRUE(loaded.joints[5].joint_type == 8);

    /* Limit rotation per-axis. */
    ASSERT_FLOAT_EQ(loaded.joints[3].limit_min[0], -1.5f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[3].limit_max[0], 1.5f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[3].limit_min[2], -0.3f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[3].limit_max[2], 0.7f, 1e-6f);
    ASSERT_TRUE(loaded.joints[3].limit_axes == 5);

    /* Limit position per-axis. */
    ASSERT_FLOAT_EQ(loaded.joints[4].limit_min[0], -2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[4].limit_max[0], 2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[4].limit_min[1], -1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[4].limit_max[1], 3.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[4].limit_min[2], 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[4].limit_max[2], 5.0f, 1e-6f);
    ASSERT_TRUE(loaded.joints[4].limit_axes == 7);

    /* Aim track axis. */
    ASSERT_FLOAT_EQ(loaded.joints[5].axis[0], 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[5].axis[1], 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(loaded.joints[5].axis[2], 0.0f, 1e-6f);

    skeleton_def_destroy(&loaded);
    free(loaded_ibms);
    unlink(tmp_path);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_round_trip);
    RUN(test_invalid_json);
    RUN(test_truncated_file);
    RUN(test_no_constraints);
    RUN(test_multi_constraints);
    RUN(test_null_safety);
    RUN(test_missing_file);
    RUN(test_bad_version);
    RUN(test_collider_round_trip);
    RUN(test_hull_vertex_round_trip);
    RUN(test_empty_joints);
    RUN(test_no_colliders_round_trip);
    RUN(test_joint_desc_round_trip);
    RUN(test_expanded_joint_types_round_trip);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
