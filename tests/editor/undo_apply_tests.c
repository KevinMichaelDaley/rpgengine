/**
 * @file undo_apply_tests.c
 * @brief Unit tests for undo/redo application logic.
 *
 * Tests that edit_undo_apply_inverse() and edit_undo_apply_forward()
 * correctly reverse and re-apply entity mutations.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/undo_apply.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_entity_version.h"

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

#define ASSERT_FLOAT_EQ(a, b)                                                \
    do {                                                                     \
        float _a = (float)(a), _b = (float)(b);                             \
        float _diff = (_a > _b) ? (_a - _b) : (_b - _a);                   \
        if (_diff > 0.001f) {                                                \
            fprintf(stderr, "  ASSERT_FLOAT_EQ failed: %f != %f (%s:%d)\n",  \
                    (double)_a, (double)_b, __FILE__, __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_NOT_NULL(p)                                                   \
    do {                                                                     \
        if ((p) == NULL) {                                                   \
            fprintf(stderr, "  ASSERT_NOT_NULL failed (%s:%d)\n",            \
                    __FILE__, __LINE__);                                      \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_NULL(p)                                                       \
    do {                                                                     \
        if ((p) != NULL) {                                                   \
            fprintf(stderr, "  ASSERT_NULL failed (%s:%d)\n",                \
                    __FILE__, __LINE__);                                      \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Test helpers: set up a minimal cmd context                               */
/* ----------------------------------------------------------------------- */

/** @brief Bridge call counters for verification. */
typedef struct {
    uint32_t spawn_count;
    uint32_t delete_count;
    uint32_t move_count;
    uint32_t last_spawn_entity_id;
    uint32_t last_delete_entity_id;
    uint32_t last_move_entity_id;
} test_bridge_state_t;

static test_bridge_state_t g_bridge_state;

static uint32_t test_on_spawn(void *ud, uint32_t eid, const edit_entity_t *e) {
    (void)ud; (void)e;
    g_bridge_state.spawn_count++;
    g_bridge_state.last_spawn_entity_id = eid;
    return eid + 1000; /* Return a fake body index. */
}

static void test_on_delete(void *ud, uint32_t eid, uint32_t body) {
    (void)ud; (void)body;
    g_bridge_state.delete_count++;
    g_bridge_state.last_delete_entity_id = eid;
}

static void test_on_move(void *ud, uint32_t eid, uint32_t body,
                          const edit_entity_t *e) {
    (void)ud; (void)body; (void)e;
    g_bridge_state.move_count++;
    g_bridge_state.last_move_entity_id = eid;
}

static edit_physics_bridge_t g_test_bridge = {
    .on_spawn  = test_on_spawn,
    .on_delete = test_on_delete,
    .on_move   = test_on_move,
    .user_data = NULL,
};

typedef struct {
    edit_entity_store_t entities;
    edit_selection_t    selection;
    edit_undo_stack_t   undo;
    edit_cmd_ctx_t      ctx;
} test_env_t;

static bool test_env_init(test_env_t *env) {
    memset(env, 0, sizeof(*env));
    if (!edit_entity_store_init(&env->entities, 256)) return false;
    if (!edit_selection_init(&env->selection)) {
        edit_entity_store_destroy(&env->entities);
        return false;
    }
    if (!edit_undo_init(&env->undo, 128, 64 * 1024)) {
        edit_selection_destroy(&env->selection);
        edit_entity_store_destroy(&env->entities);
        return false;
    }
    env->ctx.entities  = &env->entities;
    env->ctx.selection = &env->selection;
    env->ctx.undo      = &env->undo;
    env->ctx.bridge    = &g_test_bridge;
    memset(&g_bridge_state, 0, sizeof(g_bridge_state));
    return true;
}

static void test_env_destroy(test_env_t *env) {
    edit_undo_destroy(&env->undo);
    edit_selection_destroy(&env->selection);
    edit_entity_store_destroy(&env->entities);
}

/* ----------------------------------------------------------------------- */
/* Test: undo move reverses delta                                           */
/* ----------------------------------------------------------------------- */

static int test_undo_move_reverses_delta(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    /* Create entity at origin. */
    uint32_t eid = edit_entity_store_create(&env.entities, EDIT_ENTITY_TYPE_BOX);
    ASSERT_TRUE(eid != EDIT_ENTITY_INVALID_ID);
    edit_entity_t *e = edit_entity_store_get_mut(&env.entities, eid);
    ASSERT_NOT_NULL(e);
    e->pos[0] = 0.0f; e->pos[1] = 0.0f; e->pos[2] = 0.0f;

    /* Record a move entry with inverse delta (-5, -3, -1). */
    edit_undo_entry_t entry = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = eid,
        .delta        = {-5.0f, -3.0f, -1.0f, 0.0f},
    };
    ASSERT_TRUE(edit_undo_record(&env.undo, &entry, NULL, 0));

    /* Simulate: entity was moved to (5, 3, 1). */
    e->pos[0] = 5.0f; e->pos[1] = 3.0f; e->pos[2] = 1.0f;

    /* Undo: apply inverse delta (-5, -3, -1) to restore (0, 0, 0). */
    const edit_undo_entry_t *peek = edit_undo_peek_undo(&env.undo);
    ASSERT_NOT_NULL(peek);
    ASSERT_TRUE(edit_undo_apply_inverse(&env.ctx, peek));

    e = edit_entity_store_get_mut(&env.entities, eid);
    ASSERT_NOT_NULL(e);
    ASSERT_FLOAT_EQ(e->pos[0], 0.0f);
    ASSERT_FLOAT_EQ(e->pos[1], 0.0f);
    ASSERT_FLOAT_EQ(e->pos[2], 0.0f);

    /* Bridge should have been notified. */
    ASSERT_UINT_EQ(g_bridge_state.move_count, 1);
    ASSERT_UINT_EQ(g_bridge_state.last_move_entity_id, eid);

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo scale reverses factor                                         */
/* ----------------------------------------------------------------------- */

static int test_undo_scale_reverses_factor(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    uint32_t eid = edit_entity_store_create(&env.entities, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&env.entities, eid);
    e->scale[0] = 2.0f; e->scale[1] = 2.0f; e->scale[2] = 2.0f;

    /* Inverse scale: multiply by (0.5, 0.5, 0.5) to restore (1, 1, 1). */
    edit_undo_entry_t entry = {
        .forward_type = EDIT_CMD_TYPE_SCALE,
        .inverse_type = EDIT_CMD_TYPE_SCALE,
        .entity_id    = eid,
        .delta        = {0.5f, 0.5f, 0.5f, 0.0f},
    };
    ASSERT_TRUE(edit_undo_record(&env.undo, &entry, NULL, 0));

    const edit_undo_entry_t *peek = edit_undo_peek_undo(&env.undo);
    ASSERT_TRUE(edit_undo_apply_inverse(&env.ctx, peek));

    e = edit_entity_store_get_mut(&env.entities, eid);
    ASSERT_FLOAT_EQ(e->scale[0], 1.0f);
    ASSERT_FLOAT_EQ(e->scale[1], 1.0f);
    ASSERT_FLOAT_EQ(e->scale[2], 1.0f);

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo delete restores entity from snapshot                          */
/* ----------------------------------------------------------------------- */

static int test_undo_delete_restores_entity(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    /* Create and configure an entity. */
    uint32_t eid = edit_entity_store_create(&env.entities, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *e = edit_entity_store_get_mut(&env.entities, eid);
    e->pos[0] = 10.0f; e->pos[1] = 20.0f; e->pos[2] = 30.0f;
    e->scale[0] = 2.0f; e->scale[1] = 2.0f; e->scale[2] = 2.0f;
    strncpy(e->name, "test_sphere", EDIT_ENTITY_NAME_MAX - 1);

    /* Take snapshot and record delete undo entry. */
    edit_entity_t snapshot = *e;
    edit_undo_entry_t entry = {
        .forward_type = EDIT_CMD_TYPE_DELETE,
        .inverse_type = EDIT_CMD_TYPE_SPAWN,
        .entity_id    = eid,
    };
    ASSERT_TRUE(edit_undo_record(&env.undo, &entry, &snapshot,
                                  sizeof(edit_entity_t)));

    /* Remove the entity (simulating cmd_delete). */
    edit_entity_store_remove(&env.entities, eid);

    /* Verify entity is gone. */
    ASSERT_NULL(edit_entity_store_get(&env.entities, eid));

    /* Undo: should restore the entity from snapshot. */
    const edit_undo_entry_t *peek = edit_undo_peek_undo(&env.undo);
    ASSERT_NOT_NULL(peek);
    ASSERT_TRUE(edit_undo_apply_inverse(&env.ctx, peek));

    /* Verify entity is back with correct data. */
    const edit_entity_t *restored = edit_entity_store_get(&env.entities, eid);
    ASSERT_NOT_NULL(restored);
    ASSERT_FLOAT_EQ(restored->pos[0], 10.0f);
    ASSERT_FLOAT_EQ(restored->pos[1], 20.0f);
    ASSERT_FLOAT_EQ(restored->pos[2], 30.0f);
    ASSERT_FLOAT_EQ(restored->scale[0], 2.0f);
    ASSERT_UINT_EQ(restored->type, EDIT_ENTITY_TYPE_SPHERE);

    /* Bridge should have been notified of spawn. */
    ASSERT_UINT_EQ(g_bridge_state.spawn_count, 1);

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo spawn removes entity                                          */
/* ----------------------------------------------------------------------- */

static int test_undo_spawn_removes_entity(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    /* Create entity (simulating cmd_spawn). */
    uint32_t eid = edit_entity_store_create(&env.entities, EDIT_ENTITY_TYPE_BOX);
    ASSERT_TRUE(eid != EDIT_ENTITY_INVALID_ID);

    /* Record spawn undo entry (inverse = delete). */
    edit_undo_entry_t entry = {
        .forward_type = EDIT_CMD_TYPE_SPAWN,
        .inverse_type = EDIT_CMD_TYPE_DELETE,
        .entity_id    = eid,
    };
    ASSERT_TRUE(edit_undo_record(&env.undo, &entry, NULL, 0));

    /* Undo: should remove the entity. */
    const edit_undo_entry_t *peek = edit_undo_peek_undo(&env.undo);
    ASSERT_TRUE(edit_undo_apply_inverse(&env.ctx, peek));

    /* Entity should be gone. */
    ASSERT_NULL(edit_entity_store_get(&env.entities, eid));

    /* Bridge should have been notified of delete. */
    ASSERT_UINT_EQ(g_bridge_state.delete_count, 1);

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: redo re-applies forward operation                                  */
/* ----------------------------------------------------------------------- */

static int test_redo_reapplies_forward(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    /* Create entity at (5, 5, 5). */
    uint32_t eid = edit_entity_store_create(&env.entities, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&env.entities, eid);
    e->pos[0] = 5.0f; e->pos[1] = 5.0f; e->pos[2] = 5.0f;

    /* Record move forward with delta (3, 0, 0), inverse (-3, 0, 0). */
    edit_undo_entry_t entry = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = eid,
        .delta        = {-3.0f, 0.0f, 0.0f, 0.0f},
    };
    ASSERT_TRUE(edit_undo_record(&env.undo, &entry, NULL, 0));

    /* Entity was moved to (8, 5, 5). Now undo it. */
    e->pos[0] = 8.0f;
    const edit_undo_entry_t *peek = edit_undo_peek_undo(&env.undo);
    ASSERT_TRUE(edit_undo_apply_inverse(&env.ctx, peek));
    edit_undo_step(&env.undo);

    /* Verify position restored to (5, 5, 5). */
    e = edit_entity_store_get_mut(&env.entities, eid);
    ASSERT_FLOAT_EQ(e->pos[0], 5.0f);

    /* Now redo: should re-apply forward (move by +3). */
    const edit_undo_entry_t *redo_peek = edit_undo_peek_redo(&env.undo);
    ASSERT_NOT_NULL(redo_peek);
    ASSERT_TRUE(edit_undo_apply_forward(&env.ctx, redo_peek));

    e = edit_entity_store_get_mut(&env.entities, eid);
    ASSERT_FLOAT_EQ(e->pos[0], 8.0f);

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo on empty returns false                                        */
/* ----------------------------------------------------------------------- */

static int test_undo_empty_returns_false(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    /* Nothing to undo. */
    ASSERT_NULL(edit_undo_peek_undo(&env.undo));

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo with missing entity is graceful                               */
/* ----------------------------------------------------------------------- */

static int test_undo_missing_entity_graceful(void) {
    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));

    /* Record a move for an entity that doesn't exist. */
    edit_undo_entry_t entry = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 9999,
        .delta        = {1.0f, 0.0f, 0.0f, 0.0f},
    };
    ASSERT_TRUE(edit_undo_record(&env.undo, &entry, NULL, 0));

    /* Undo should not crash, returns false (entity not found). */
    const edit_undo_entry_t *peek = edit_undo_peek_undo(&env.undo);
    ASSERT_FALSE(edit_undo_apply_inverse(&env.ctx, peek));

    test_env_destroy(&env);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: null params handled                                                */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    ASSERT_FALSE(edit_undo_apply_inverse(NULL, NULL));
    ASSERT_FALSE(edit_undo_apply_forward(NULL, NULL));

    test_env_t env;
    ASSERT_TRUE(test_env_init(&env));
    ASSERT_FALSE(edit_undo_apply_inverse(&env.ctx, NULL));
    ASSERT_FALSE(edit_undo_apply_forward(&env.ctx, NULL));
    test_env_destroy(&env);
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
    {"undo_move_reverses_delta",      test_undo_move_reverses_delta},
    {"undo_scale_reverses_factor",    test_undo_scale_reverses_factor},
    {"undo_delete_restores_entity",   test_undo_delete_restores_entity},
    {"undo_spawn_removes_entity",     test_undo_spawn_removes_entity},
    {"redo_reapplies_forward",        test_redo_reapplies_forward},
    {"undo_empty_returns_false",      test_undo_empty_returns_false},
    {"undo_missing_entity_graceful",  test_undo_missing_entity_graceful},
    {"null_params",                   test_null_params},
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
