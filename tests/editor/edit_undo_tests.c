/**
 * @file edit_undo_tests.c
 * @brief Unit tests for the undo/redo stack.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/edit_undo.h"

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

#define ASSERT_NULL(p)                                                       \
    do {                                                                     \
        if ((p) != NULL) {                                                   \
            fprintf(stderr, "  ASSERT_NULL failed (%s:%d)\n",                \
                    __FILE__, __LINE__);                                      \
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

#define ASSERT_FLOAT_EQ(a, b)                                                \
    do {                                                                     \
        float _a = (float)(a), _b = (float)(b);                             \
        float _diff = (_a > _b) ? (_a - _b) : (_b - _a);                   \
        if (_diff > 0.0001f) {                                               \
            fprintf(stderr, "  ASSERT_FLOAT_EQ failed: %f != %f (%s:%d)\n",  \
                    (double)_a, (double)_b, __FILE__, __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Command type tags for testing.                                           */
/* ----------------------------------------------------------------------- */

enum {
    CMD_SPAWN  = 1,
    CMD_DELETE  = 2,
    CMD_MOVE    = 3,
    CMD_ROTATE  = 4,
};

/* ----------------------------------------------------------------------- */
/* Test: init and destroy                                                    */
/* ----------------------------------------------------------------------- */

static int test_init_destroy(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));
    ASSERT_UINT_EQ(edit_undo_count(&stack), 0);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 0);
    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: record and peek undo                                                */
/* ----------------------------------------------------------------------- */

static int test_record_and_peek(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    edit_undo_entry_t entry = {
        .forward_type = CMD_MOVE,
        .inverse_type = CMD_MOVE,
        .group_id     = EDIT_UNDO_NO_GROUP,
        .entity_id    = 42,
        .delta        = {1.0f, 2.0f, 3.0f, 0.0f},
    };
    ASSERT_TRUE(edit_undo_record(&stack, &entry, NULL, 0));
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);

    const edit_undo_entry_t *peek = edit_undo_peek_undo(&stack);
    ASSERT_NOT_NULL(peek);
    ASSERT_UINT_EQ(peek->forward_type, CMD_MOVE);
    ASSERT_UINT_EQ(peek->entity_id, 42);
    ASSERT_FLOAT_EQ(peek->delta[0], 1.0f);
    ASSERT_FLOAT_EQ(peek->delta[1], 2.0f);
    ASSERT_FLOAT_EQ(peek->delta[2], 3.0f);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo step                                                           */
/* ----------------------------------------------------------------------- */

static int test_undo_step(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    /* Record three commands. */
    for (uint32_t i = 0; i < 3; ++i) {
        edit_undo_entry_t entry = {
            .forward_type = CMD_MOVE,
            .inverse_type = CMD_MOVE,
            .entity_id    = i + 1,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &entry, NULL, 0));
    }
    ASSERT_UINT_EQ(edit_undo_count(&stack), 3);

    /* Undo one step. */
    uint32_t undone = edit_undo_step(&stack);
    ASSERT_UINT_EQ(undone, 1);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 2);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 1);

    /* Peek at what was just undone (should be the last entry). */
    const edit_undo_entry_t *redo_peek = edit_undo_peek_redo(&stack);
    ASSERT_NOT_NULL(redo_peek);
    ASSERT_UINT_EQ(redo_peek->entity_id, 3);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: redo step                                                           */
/* ----------------------------------------------------------------------- */

static int test_redo_step(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    edit_undo_entry_t entry = {
        .forward_type = CMD_SPAWN,
        .inverse_type = CMD_DELETE,
        .entity_id    = 10,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &entry, NULL, 0));

    /* Undo it. */
    ASSERT_UINT_EQ(edit_undo_step(&stack), 1);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 0);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 1);

    /* Redo it. */
    uint32_t redone = edit_undo_redo(&stack);
    ASSERT_UINT_EQ(redone, 1);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 0);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: new action after undo truncates redo history                         */
/* ----------------------------------------------------------------------- */

static int test_record_truncates_redo(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    /* Record A, B, C. */
    for (uint32_t i = 0; i < 3; ++i) {
        edit_undo_entry_t entry = {
            .forward_type = CMD_MOVE,
            .inverse_type = CMD_MOVE,
            .entity_id    = i,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &entry, NULL, 0));
    }

    /* Undo twice (cursor now at A). */
    edit_undo_step(&stack);
    edit_undo_step(&stack);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 2);

    /* Record D — should discard B and C from redo. */
    edit_undo_entry_t d = {
        .forward_type = CMD_SPAWN,
        .inverse_type = CMD_DELETE,
        .entity_id    = 99,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &d, NULL, 0));
    ASSERT_UINT_EQ(edit_undo_count(&stack), 2); /* A, D */
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 0);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: undo/redo on empty stack                                            */
/* ----------------------------------------------------------------------- */

static int test_undo_empty(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    ASSERT_UINT_EQ(edit_undo_step(&stack), 0);
    ASSERT_NULL(edit_undo_peek_undo(&stack));
    ASSERT_UINT_EQ(edit_undo_redo(&stack), 0);
    ASSERT_NULL(edit_undo_peek_redo(&stack));

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: snapshot data in arena                                              */
/* ----------------------------------------------------------------------- */

static int test_snapshot_data(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    /* Simulate a delete command with entity snapshot. */
    uint8_t snapshot[64];
    memset(snapshot, 0xAB, sizeof(snapshot));

    edit_undo_entry_t entry = {
        .forward_type = CMD_DELETE,
        .inverse_type = CMD_SPAWN,
        .entity_id    = 5,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &entry, snapshot, sizeof(snapshot)));

    const edit_undo_entry_t *peek = edit_undo_peek_undo(&stack);
    ASSERT_NOT_NULL(peek);
    ASSERT_NOT_NULL(peek->snapshot_data);
    ASSERT_UINT_EQ(peek->snapshot_size, 64);

    /* Verify snapshot data was copied correctly. */
    uint8_t *data = (uint8_t *)peek->snapshot_data;
    for (int i = 0; i < 64; ++i) {
        ASSERT_UINT_EQ(data[i], 0xAB);
    }

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: group undo                                                          */
/* ----------------------------------------------------------------------- */

static int test_group_undo(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    /* Record a group of 5 spawns. */
    uint32_t gid = edit_undo_begin_group(&stack);
    ASSERT_TRUE(gid != EDIT_UNDO_NO_GROUP);

    for (uint32_t i = 0; i < 5; ++i) {
        edit_undo_entry_t entry = {
            .forward_type = CMD_SPAWN,
            .inverse_type = CMD_DELETE,
            .entity_id    = 100 + i,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &entry, NULL, 0));
    }
    edit_undo_end_group(&stack);

    ASSERT_UINT_EQ(edit_undo_count(&stack), 5);

    /* Single undo should reverse all 5. */
    uint32_t undone = edit_undo_step(&stack);
    ASSERT_UINT_EQ(undone, 5);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 0);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 5);

    /* Single redo should restore all 5. */
    uint32_t redone = edit_undo_redo(&stack);
    ASSERT_UINT_EQ(redone, 5);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 5);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 0);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: mixed groups and singles                                            */
/* ----------------------------------------------------------------------- */

static int test_mixed_groups(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    /* Single A. */
    edit_undo_entry_t a = {
        .forward_type = CMD_MOVE, .inverse_type = CMD_MOVE, .entity_id = 1,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &a, NULL, 0));

    /* Group [B, C]. */
    edit_undo_begin_group(&stack);
    edit_undo_entry_t b = {
        .forward_type = CMD_SPAWN, .inverse_type = CMD_DELETE, .entity_id = 2,
    };
    edit_undo_entry_t c = {
        .forward_type = CMD_SPAWN, .inverse_type = CMD_DELETE, .entity_id = 3,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &b, NULL, 0));
    ASSERT_TRUE(edit_undo_record(&stack, &c, NULL, 0));
    edit_undo_end_group(&stack);

    /* Single D. */
    edit_undo_entry_t d = {
        .forward_type = CMD_MOVE, .inverse_type = CMD_MOVE, .entity_id = 4,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &d, NULL, 0));

    ASSERT_UINT_EQ(edit_undo_count(&stack), 4);

    /* Undo D (single). */
    ASSERT_UINT_EQ(edit_undo_step(&stack), 1);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 3);

    /* Undo [B, C] (group). */
    ASSERT_UINT_EQ(edit_undo_step(&stack), 2);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);

    /* Undo A (single). */
    ASSERT_UINT_EQ(edit_undo_step(&stack), 1);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 0);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: ring wraps (oldest entries evicted)                                  */
/* ----------------------------------------------------------------------- */

static int test_ring_wrap(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 4, 4096));

    /* Fill 6 entries into a ring of 4 — first 2 should be evicted. */
    for (uint32_t i = 0; i < 6; ++i) {
        edit_undo_entry_t entry = {
            .forward_type = CMD_MOVE,
            .inverse_type = CMD_MOVE,
            .entity_id    = i,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &entry, NULL, 0));
    }

    /* Should have 4 entries (the most recent). */
    ASSERT_UINT_EQ(edit_undo_count(&stack), 4);

    /* Undo all 4 — entity_ids should be 5, 4, 3, 2. */
    for (uint32_t expected = 5; expected >= 2; --expected) {
        const edit_undo_entry_t *peek = edit_undo_peek_undo(&stack);
        ASSERT_NOT_NULL(peek);
        ASSERT_UINT_EQ(peek->entity_id, expected);
        ASSERT_UINT_EQ(edit_undo_step(&stack), 1);
    }

    /* Nothing left to undo. */
    ASSERT_UINT_EQ(edit_undo_step(&stack), 0);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: clear                                                               */
/* ----------------------------------------------------------------------- */

static int test_clear(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    for (uint32_t i = 0; i < 5; ++i) {
        edit_undo_entry_t entry = {
            .forward_type = CMD_MOVE, .inverse_type = CMD_MOVE, .entity_id = i,
        };
        edit_undo_record(&stack, &entry, NULL, 0);
    }

    edit_undo_clear(&stack);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 0);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 0);
    ASSERT_NULL(edit_undo_peek_undo(&stack));

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: arena overflow forces eviction                                      */
/* ----------------------------------------------------------------------- */

static int test_arena_overflow_eviction(void) {
    edit_undo_stack_t stack;
    /* Tiny arena: 128 bytes. Each snapshot = 64 bytes. Max ~2 snapshots. */
    ASSERT_TRUE(edit_undo_init(&stack, 16, 128));

    uint8_t snap[64];
    memset(snap, 0xCC, sizeof(snap));

    /* Record 4 entries with snapshots. Arena can hold ~2 at a time. */
    for (uint32_t i = 0; i < 4; ++i) {
        edit_undo_entry_t entry = {
            .forward_type = CMD_DELETE,
            .inverse_type = CMD_SPAWN,
            .entity_id    = i,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &entry, snap, 64));
    }

    /* Stack should still have entries, oldest may have been evicted. */
    ASSERT_TRUE(edit_undo_count(&stack) > 0);

    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: NULL params                                                         */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    ASSERT_FALSE(edit_undo_init(NULL, 64, 4096));

    edit_undo_stack_t stack;
    ASSERT_FALSE(edit_undo_init(&stack, 0, 4096));
    ASSERT_FALSE(edit_undo_init(&stack, 64, 0));

    /* Operations on NULL should not crash. */
    ASSERT_UINT_EQ(edit_undo_count(NULL), 0);
    ASSERT_UINT_EQ(edit_undo_redo_count(NULL), 0);

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
    {"init_destroy",              test_init_destroy},
    {"record_and_peek",           test_record_and_peek},
    {"undo_step",                 test_undo_step},
    {"redo_step",                 test_redo_step},
    {"record_truncates_redo",     test_record_truncates_redo},
    {"undo_empty",                test_undo_empty},
    {"snapshot_data",             test_snapshot_data},
    {"group_undo",                test_group_undo},
    {"mixed_groups",              test_mixed_groups},
    {"ring_wrap",                 test_ring_wrap},
    {"clear",                     test_clear},
    {"arena_overflow_eviction",   test_arena_overflow_eviction},
    {"null_params",               test_null_params},
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
