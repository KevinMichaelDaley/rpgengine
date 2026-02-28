/**
 * @file mesh_commit_tests.c
 * @brief Tests for mesh_commit — bake mesh slot to entity + FVMA asset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ferrum/editor/mesh/mesh_commit.h"
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

/* ------------------------------------------------------------------ */
/* Test: basic commit creates entity                                   */
/* ------------------------------------------------------------------ */

static void test_basic_commit(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    mesh_commit_args_t args;
    memset(&args, 0, sizeof(args));
    strncpy(args.entity_name, "my_box", sizeof(args.entity_name) - 1);
    args.clear_slot = true;

    mesh_commit_result_t result;
    bool ok = mesh_commit(&slot, &store, &args, &result);
    ASSERT(ok, "commit succeeded");
    ASSERT(result.entity_id != EDIT_ENTITY_INVALID_ID, "valid entity id");
    ASSERT(result.fvma_size > 0, "fvma data produced");

    /* Entity should exist and be active */
    const edit_entity_t *ent = edit_entity_store_get(&store, result.entity_id);
    ASSERT(ent != NULL, "entity found");
    ASSERT(ent->active, "entity active");
    ASSERT(ent->type == EDIT_ENTITY_TYPE_MESH, "entity is mesh type");
    ASSERT(strcmp(ent->name, "my_box") == 0, "entity name set");

    /* Slot should be cleared */
    ASSERT(slot.vertex_count == 0, "slot cleared");

    free(result.fvma_data);
    edit_entity_store_destroy(&store);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: commit with material override                                 */
/* ------------------------------------------------------------------ */

static void test_material_override(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    mesh_commit_args_t args;
    memset(&args, 0, sizeof(args));
    strncpy(args.entity_name, "mat_box", sizeof(args.entity_name) - 1);
    strncpy(args.material_override, "textures/brick.png",
            sizeof(args.material_override) - 1);
    args.clear_slot = true;

    mesh_commit_result_t result;
    bool ok = mesh_commit(&slot, &store, &args, &result);
    ASSERT(ok, "commit with material succeeded");

    const edit_entity_t *ent = edit_entity_store_get(&store, result.entity_id);
    ASSERT(ent != NULL, "entity found");
    ASSERT(strcmp(ent->materials[0], "textures/brick.png") == 0, "material set");

    free(result.fvma_data);
    edit_entity_store_destroy(&store);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: entity position at bounding box center                        */
/* ------------------------------------------------------------------ */

static void test_entity_position(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    /* Box centered at (5, 10, 15) */
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){5,10,15});

    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    mesh_commit_args_t args;
    memset(&args, 0, sizeof(args));
    strncpy(args.entity_name, "offset_box", sizeof(args.entity_name) - 1);
    args.clear_slot = true;

    mesh_commit_result_t result;
    bool ok = mesh_commit(&slot, &store, &args, &result);
    ASSERT(ok, "commit succeeded");

    const edit_entity_t *ent = edit_entity_store_get(&store, result.entity_id);
    ASSERT(ent != NULL, "entity found");
    /* Position should be at bounding box center */
    ASSERT(ent->pos[0] > 4.5f && ent->pos[0] < 5.5f, "X near 5");
    ASSERT(ent->pos[1] > 9.5f && ent->pos[1] < 10.5f, "Y near 10");
    ASSERT(ent->pos[2] > 14.5f && ent->pos[2] < 15.5f, "Z near 15");

    free(result.fvma_data);
    edit_entity_store_destroy(&store);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: keep slot after commit                                        */
/* ------------------------------------------------------------------ */

static void test_keep_slot(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});
    uint32_t orig_vc = slot.vertex_count;

    edit_entity_store_t store;
    edit_entity_store_init(&store, 64);

    mesh_commit_args_t args;
    memset(&args, 0, sizeof(args));
    strncpy(args.entity_name, "kept_box", sizeof(args.entity_name) - 1);
    args.clear_slot = false; /* Keep mesh data */

    mesh_commit_result_t result;
    bool ok = mesh_commit(&slot, &store, &args, &result);
    ASSERT(ok, "commit succeeded");
    ASSERT(slot.vertex_count == orig_vc, "slot NOT cleared");

    free(result.fvma_data);
    edit_entity_store_destroy(&store);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    mesh_commit_result_t result;
    ASSERT(!mesh_commit(NULL, NULL, NULL, &result), "null commit");
    g_pass++;
}

int main(void) {
    printf("mesh_commit_tests:\n");
    test_basic_commit();
    test_material_override();
    test_entity_position();
    test_keep_slot();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
