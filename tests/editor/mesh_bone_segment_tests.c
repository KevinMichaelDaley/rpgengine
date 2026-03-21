/**
 * @file mesh_bone_segment_tests.c
 * @brief Tests for per-bone mesh triangle segmentation (Phase D).
 *
 * Validates that mesh triangles are correctly assigned to bones
 * based on dominant vertex weights.
 */

#include "ferrum/editor/mesh/mesh_bone_segment.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/physics/mesh_collider.h"  /* phys_triangle_t */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Helpers ---- */

/**
 * @brief Build a simple mesh with 4 triangles (2 quads) assigned to 2 bones.
 *
 * Vertices 0-3 belong to bone 0 (weight 1.0).
 * Vertices 4-7 belong to bone 1 (weight 1.0).
 * Triangles 0,1 use verts 0-3 (bone 0).
 * Triangles 2,3 use verts 4-7 (bone 1).
 */
static void build_two_bone_mesh(mesh_slot_t *slot,
                                 float **out_weights,
                                 uint32_t **out_indices) {
    memset(slot, 0, sizeof(*slot));
    mesh_slot_init(slot, 8, 12);

    /* 8 vertices forming two quads. */
    float positions[][3] = {
        /* Bone 0 quad */
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
        /* Bone 1 quad */
        {2, 0, 0}, {3, 0, 0}, {3, 1, 0}, {2, 1, 0},
    };
    float normal[3] = {0, 0, 1};
    for (int i = 0; i < 8; i++) {
        mesh_slot_add_vertex(slot, positions[i], normal);
    }

    /* 4 triangles. */
    mesh_slot_add_triangle(slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(slot, 0, 2, 3, 0);
    mesh_slot_add_triangle(slot, 4, 5, 6, 0);
    mesh_slot_add_triangle(slot, 4, 6, 7, 0);

    /* Bone weights: 4 floats per vertex (vec4). */
    *out_weights = (float *)calloc(8 * 4, sizeof(float));
    for (int i = 0; i < 4; i++) {
        (*out_weights)[i * 4 + 0] = 1.0f; /* bone 0, weight 1.0 */
    }
    for (int i = 4; i < 8; i++) {
        (*out_weights)[i * 4 + 0] = 0.0f;
        (*out_weights)[i * 4 + 1] = 1.0f; /* bone 1, weight 1.0 */
    }

    /* Bone indices: 4 uint32 per vertex (uvec4). */
    *out_indices = (uint32_t *)calloc(8 * 4, sizeof(uint32_t));
    for (int i = 0; i < 4; i++) {
        (*out_indices)[i * 4 + 0] = 0; /* bone 0 */
    }
    for (int i = 4; i < 8; i++) {
        (*out_indices)[i * 4 + 0] = 0;
        (*out_indices)[i * 4 + 1] = 1; /* bone 1 */
    }
}

/* ---- Tests ---- */

/** Init and destroy an empty segment set. */
static void test_init_destroy(void) {
    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);
    ASSERT(segs.capacity == 4);
    ASSERT(segs.count == 0);
    mesh_bone_segments_destroy(&segs);
}

/** from_slot with 2-bone mesh produces 2 segments. */
static void test_two_bone_segmentation(void) {
    mesh_slot_t slot;
    float *weights;
    uint32_t *indices;
    build_two_bone_mesh(&slot, &weights, &indices);

    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);

    bool ok = mesh_bone_segments_from_slot(&segs, &slot,
                                            indices, weights,
                                            4, 2);
    ASSERT(ok);
    ASSERT(segs.count == 2);

    /* Each segment should have 2 triangles. */
    uint32_t bone0_tris = 0, bone1_tris = 0;
    for (uint32_t i = 0; i < segs.count; i++) {
        if (segs.segments[i].bone_index == 0) {
            bone0_tris = segs.segments[i].tri_count;
        } else if (segs.segments[i].bone_index == 1) {
            bone1_tris = segs.segments[i].tri_count;
        }
    }
    ASSERT(bone0_tris == 2);
    ASSERT(bone1_tris == 2);

    mesh_bone_segments_destroy(&segs);
    mesh_slot_destroy(&slot);
    free(weights);
    free(indices);
}

/** Triangles with mixed bone weights use the dominant bone. */
static void test_dominant_bone_assignment(void) {
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    mesh_slot_init(&slot, 3, 3);

    float pos[][3] = {{0,0,0}, {1,0,0}, {0,1,0}};
    float nrm[3] = {0,0,1};
    for (int i = 0; i < 3; i++) {
        mesh_slot_add_vertex(&slot, pos[i], nrm);
    }
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    /* Vertex 0: bone 0 (0.6) + bone 1 (0.4)
     * Vertex 1: bone 0 (0.3) + bone 1 (0.7)
     * Vertex 2: bone 0 (0.8) + bone 1 (0.2)
     * Sum of weights: bone 0 = 0.6+0.3+0.8 = 1.7, bone 1 = 0.4+0.7+0.2 = 1.3
     * Dominant bone = 0 */
    float weights[3 * 4];
    memset(weights, 0, sizeof(weights));
    weights[0*4+0] = 0.6f; weights[0*4+1] = 0.4f;
    weights[1*4+0] = 0.3f; weights[1*4+1] = 0.7f;
    weights[2*4+0] = 0.8f; weights[2*4+1] = 0.2f;

    uint32_t indices[3 * 4];
    memset(indices, 0, sizeof(indices));
    indices[0*4+0] = 0; indices[0*4+1] = 1;
    indices[1*4+0] = 0; indices[1*4+1] = 1;
    indices[2*4+0] = 0; indices[2*4+1] = 1;

    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);
    bool ok = mesh_bone_segments_from_slot(&segs, &slot,
                                            indices, weights,
                                            4, 2);
    ASSERT(ok);
    ASSERT(segs.count >= 1);

    /* The triangle should be assigned to bone 0 (dominant). */
    bool found_bone0 = false;
    for (uint32_t i = 0; i < segs.count; i++) {
        if (segs.segments[i].bone_index == 0 &&
            segs.segments[i].tri_count == 1) {
            found_bone0 = true;
        }
    }
    ASSERT(found_bone0);

    mesh_bone_segments_destroy(&segs);
    mesh_slot_destroy(&slot);
}

/** NULL inputs return false gracefully. */
static void test_null_inputs(void) {
    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);

    ASSERT(!mesh_bone_segments_from_slot(NULL, NULL, NULL, NULL, 0, 0));
    ASSERT(!mesh_bone_segments_from_slot(&segs, NULL, NULL, NULL, 0, 0));

    mesh_bone_segments_destroy(&segs);
}

/** Empty mesh produces 0 segments. */
static void test_empty_mesh(void) {
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));

    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);

    bool ok = mesh_bone_segments_from_slot(&segs, &slot,
                                            NULL, NULL, 4, 0);
    ASSERT(!ok);
    ASSERT(segs.count == 0);

    mesh_bone_segments_destroy(&segs);
}

/** Single bone mesh puts all triangles in one segment. */
static void test_single_bone(void) {
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    mesh_slot_init(&slot, 4, 6);

    float pos[][3] = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}};
    float nrm[3] = {0,0,1};
    for (int i = 0; i < 4; i++) {
        mesh_slot_add_vertex(&slot, pos[i], nrm);
    }
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 0, 2, 3, 0);

    float weights[4 * 4];
    memset(weights, 0, sizeof(weights));
    for (int i = 0; i < 4; i++) weights[i * 4] = 1.0f;

    uint32_t indices[4 * 4];
    memset(indices, 0, sizeof(indices));

    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);
    bool ok = mesh_bone_segments_from_slot(&segs, &slot,
                                            indices, weights, 4, 1);
    ASSERT(ok);
    ASSERT(segs.count == 1);
    if (segs.count >= 1) {
        ASSERT(segs.segments[0].bone_index == 0);
        ASSERT(segs.segments[0].tri_count == 2);
    }

    mesh_bone_segments_destroy(&segs);
    mesh_slot_destroy(&slot);
}

/** Segment triangles have valid vertex positions. */
static void test_triangle_positions(void) {
    mesh_slot_t slot;
    float *weights;
    uint32_t *indices;
    build_two_bone_mesh(&slot, &weights, &indices);

    mesh_bone_segments_t segs;
    mesh_bone_segments_init(&segs, 4);
    mesh_bone_segments_from_slot(&segs, &slot, indices, weights, 4, 2);

    /* Check that all segment triangles have reasonable positions. */
    for (uint32_t s = 0; s < segs.count; s++) {
        const mesh_bone_segment_t *seg = &segs.segments[s];
        for (uint32_t t = 0; t < seg->tri_count; t++) {
            for (int v = 0; v < 3; v++) {
                float x = seg->triangles[t].v[v].x;
                float y = seg->triangles[t].v[v].y;
                float z = seg->triangles[t].v[v].z;
                /* All positions should be within [0,3] range. */
                ASSERT(x >= -0.01f && x <= 3.01f);
                ASSERT(y >= -0.01f && y <= 1.01f);
                ASSERT(z >= -0.01f && z <= 0.01f);
            }
        }
    }

    mesh_bone_segments_destroy(&segs);
    mesh_slot_destroy(&slot);
    free(weights);
    free(indices);
}

/* ---- Main ---- */

int main(void) {
    printf("mesh_bone_segment_tests:\n");

    test_init_destroy();
    test_two_bone_segmentation();
    test_dominant_bone_assignment();
    test_null_inputs();
    test_empty_mesh();
    test_single_bone();
    test_triangle_positions();

    printf("mesh_bone_segment_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
