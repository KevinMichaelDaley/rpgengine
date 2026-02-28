/**
 * @file mesh_clip_tests.c
 * @brief Tests for mesh clipping by plane.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_clip.h"
#include "ferrum/editor/mesh/mesh_primitives.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

static bool indices_valid_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->index_count; i++) {
        if (slot->indices[i] >= slot->vertex_count) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Clip front — keep upper half of box                                 */
/* ------------------------------------------------------------------ */

static void test_clip_front(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){2,2,2}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* Clip at Y=0 plane, keep front (Y > 0) */
    float plane_pt[3] = {0, 0, 0};
    float plane_nrm[3] = {0, 1, 0};

    bool ok = mesh_clip(&slot, plane_pt, plane_nrm, MESH_CLIP_FRONT);
    ASSERT(ok, "clip front succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    /* All remaining vertices should have Y >= -epsilon */
    /* All referenced vertices should have Y >= -epsilon */
    bool front_ok = true;
    for (uint32_t i = 0; i < slot.index_count; i++) {
        uint32_t vi = slot.indices[i];
        if (slot.positions[vi*3+1] < -0.01f) { front_ok = false; break; }
    }
    ASSERT(front_ok, "referenced vertices on front side");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Clip back — keep lower half                                         */
/* ------------------------------------------------------------------ */

static void test_clip_back(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){2,2,2}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    float plane_pt[3] = {0, 0, 0};
    float plane_nrm[3] = {0, 1, 0};

    bool ok = mesh_clip(&slot, plane_pt, plane_nrm, MESH_CLIP_BACK);
    ASSERT(ok, "clip back succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");

    bool back_ok = true;
    for (uint32_t i = 0; i < slot.index_count; i++) {
        uint32_t vi = slot.indices[i];
        if (slot.positions[vi*3+1] > 0.01f) { back_ok = false; break; }
    }
    ASSERT(back_ok, "referenced vertices on back side");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Clip single tri straddling plane                                    */
/* ------------------------------------------------------------------ */

static void test_clip_single_tri(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, -1, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2, -1, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 1, 0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    float plane_pt[3] = {0, 0, 0};
    float plane_nrm[3] = {0, 1, 0};

    bool ok = mesh_clip(&slot, plane_pt, plane_nrm, MESH_CLIP_FRONT);
    ASSERT(ok, "single tri clip succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(slot.index_count > 0, "some geometry remains");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Clip no intersection (all front)                                    */
/* ------------------------------------------------------------------ */

static void test_clip_all_front(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0, 1, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1, 1, 0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0, 2, 0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    float plane_pt[3] = {0, 0, 0};
    float plane_nrm[3] = {0, 1, 0};

    uint32_t orig_faces = slot.index_count / 3;
    bool ok = mesh_clip(&slot, plane_pt, plane_nrm, MESH_CLIP_FRONT);
    ASSERT(ok, "all-front clip succeeded");
    ASSERT(slot.index_count / 3 == orig_faces, "unchanged");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null safety                                                         */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_clip(NULL, NULL, NULL, MESH_CLIP_FRONT), "null clip");
    g_pass++;
}

int main(void) {
    printf("mesh_clip_tests:\n");
    test_clip_front();
    test_clip_back();
    test_clip_single_tri();
    test_clip_all_front();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
