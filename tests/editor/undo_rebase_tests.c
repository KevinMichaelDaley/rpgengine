/**
 * @file undo_rebase_tests.c
 * @brief Unit tests for branching undo with rebase logic.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/undo_rebase.h"
#include "ferrum/editor/undo_conflict.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_cmd_ctx.h"

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

#define ASSERT_NOT_NULL(p)                                                   \
    do {                                                                     \
        if ((p) == NULL) {                                                   \
            fprintf(stderr, "  ASSERT_NOT_NULL failed (%s:%d)\n",            \
                    __FILE__, __LINE__);                                      \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Test: init and destroy                                                    */
/* ----------------------------------------------------------------------- */

static int test_branches_init_destroy(void) {
    undo_branches_t branches;
    ASSERT_TRUE(undo_branches_init(&branches, 8, 32));
    ASSERT_UINT_EQ(undo_branches_count(&branches), 0);
    undo_branches_destroy(&branches);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: rebase with no redo entries (normal record)                         */
/* ----------------------------------------------------------------------- */

static int test_rebase_no_redo(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    undo_branches_t branches;
    ASSERT_TRUE(undo_branches_init(&branches, 8, 32));

    /* Record A normally. */
    edit_undo_entry_t a = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 1,
    };
    ASSERT_TRUE(edit_undo_record_rebase(&stack, &a, NULL, 0, &branches));
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);
    ASSERT_UINT_EQ(undo_branches_count(&branches), 0);

    undo_branches_destroy(&branches);
    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: non-conflicting displaced entries survive rebase                    */
/* ----------------------------------------------------------------------- */

static int test_rebase_non_conflicting_survives(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    undo_branches_t branches;
    ASSERT_TRUE(undo_branches_init(&branches, 8, 32));

    /* Record A (entity 1), B (entity 2), C (entity 3). */
    for (uint32_t i = 1; i <= 3; i++) {
        edit_undo_entry_t e = {
            .forward_type = EDIT_CMD_TYPE_MOVE,
            .inverse_type = EDIT_CMD_TYPE_MOVE,
            .entity_id    = i,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &e, NULL, 0));
    }
    ASSERT_UINT_EQ(edit_undo_count(&stack), 3);

    /* Undo B and C (back to A). */
    edit_undo_step(&stack);  /* Undo C */
    edit_undo_step(&stack);  /* Undo B */
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);
    ASSERT_UINT_EQ(edit_undo_redo_count(&stack), 2);

    /* New edit D on entity 4 (no conflict with B=entity2, C=entity3). */
    edit_undo_entry_t d = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 4,
    };
    ASSERT_TRUE(edit_undo_record_rebase(&stack, &d, NULL, 0, &branches));

    /* Expected: A, D, B', C' (rebased). No orphan branches. */
    ASSERT_UINT_EQ(edit_undo_count(&stack), 4);
    ASSERT_UINT_EQ(undo_branches_count(&branches), 0);

    undo_branches_destroy(&branches);
    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: conflicting displaced entries moved to orphan                      */
/* ----------------------------------------------------------------------- */

static int test_rebase_conflicting_orphaned(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    undo_branches_t branches;
    ASSERT_TRUE(undo_branches_init(&branches, 8, 32));

    /* Record A (entity 1), B (entity 1). */
    edit_undo_entry_t a = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 1,
    };
    edit_undo_entry_t b = {
        .forward_type = EDIT_CMD_TYPE_SCALE,
        .inverse_type = EDIT_CMD_TYPE_SCALE,
        .entity_id    = 1,
    };
    ASSERT_TRUE(edit_undo_record(&stack, &a, NULL, 0));
    ASSERT_TRUE(edit_undo_record(&stack, &b, NULL, 0));

    /* Undo B. */
    edit_undo_step(&stack);
    ASSERT_UINT_EQ(edit_undo_count(&stack), 1);

    /* New edit C on entity 1 (conflicts with B). */
    edit_undo_entry_t c = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 1,
    };
    ASSERT_TRUE(edit_undo_record_rebase(&stack, &c, NULL, 0, &branches));

    /* Expected: A, C. B orphaned. */
    ASSERT_UINT_EQ(edit_undo_count(&stack), 2);
    ASSERT_UINT_EQ(undo_branches_count(&branches), 1);

    /* Verify orphan branch has 1 entry with entity_id=1, scale type. */
    const undo_branch_t *branch = undo_branches_get(&branches, 0);
    ASSERT_NOT_NULL(branch);
    ASSERT_UINT_EQ(branch->count, 1);
    ASSERT_UINT_EQ(branch->entries[0].entity_id, 1);
    ASSERT_UINT_EQ(branch->entries[0].forward_type, EDIT_CMD_TYPE_SCALE);

    undo_branches_destroy(&branches);
    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: mixed — some conflict, some don't                                  */
/* ----------------------------------------------------------------------- */

static int test_rebase_partial_conflict(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));

    undo_branches_t branches;
    ASSERT_TRUE(undo_branches_init(&branches, 8, 32));

    /* A(ent1), B(ent2), C(ent1). */
    edit_undo_entry_t entries[3] = {
        {.forward_type = EDIT_CMD_TYPE_MOVE, .inverse_type = EDIT_CMD_TYPE_MOVE, .entity_id = 1},
        {.forward_type = EDIT_CMD_TYPE_MOVE, .inverse_type = EDIT_CMD_TYPE_MOVE, .entity_id = 2},
        {.forward_type = EDIT_CMD_TYPE_MOVE, .inverse_type = EDIT_CMD_TYPE_MOVE, .entity_id = 1},
    };
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(edit_undo_record(&stack, &entries[i], NULL, 0));
    }

    /* Undo C and B. */
    edit_undo_step(&stack);
    edit_undo_step(&stack);

    /* New edit D on entity 1 → conflicts with B? No, B is entity 2.
     * Conflicts with C? Yes, C is entity 1.
     * So B survives (rebased), C is orphaned. */
    edit_undo_entry_t d = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .inverse_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 1,
    };
    ASSERT_TRUE(edit_undo_record_rebase(&stack, &d, NULL, 0, &branches));

    /* Expected: A, D, B'. C orphaned. */
    ASSERT_UINT_EQ(edit_undo_count(&stack), 3);
    ASSERT_UINT_EQ(undo_branches_count(&branches), 1);

    const undo_branch_t *branch = undo_branches_get(&branches, 0);
    ASSERT_UINT_EQ(branch->count, 1);
    ASSERT_UINT_EQ(branch->entries[0].entity_id, 1);

    undo_branches_destroy(&branches);
    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: orphan branch overflow discards oldest                              */
/* ----------------------------------------------------------------------- */

static int test_orphan_overflow_discards_oldest(void) {
    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 256, 4096));

    undo_branches_t branches;
    /* Only 2 branches max. */
    ASSERT_TRUE(undo_branches_init(&branches, 2, 32));

    /* Create 3 conflict cycles to overflow. */
    for (uint32_t cycle = 0; cycle < 3; cycle++) {
        /* Record entry on entity 1. */
        edit_undo_entry_t e = {
            .forward_type = EDIT_CMD_TYPE_MOVE,
            .inverse_type = EDIT_CMD_TYPE_MOVE,
            .entity_id    = 1,
        };
        ASSERT_TRUE(edit_undo_record(&stack, &e, NULL, 0));

        /* Undo it. */
        edit_undo_step(&stack);

        /* New conflicting edit on entity 1. */
        edit_undo_entry_t f = {
            .forward_type = EDIT_CMD_TYPE_SCALE,
            .inverse_type = EDIT_CMD_TYPE_SCALE,
            .entity_id    = 1,
        };
        ASSERT_TRUE(edit_undo_record_rebase(&stack, &f, NULL, 0, &branches));
    }

    /* Should have at most 2 branches (oldest discarded). */
    ASSERT_TRUE(undo_branches_count(&branches) <= 2);

    undo_branches_destroy(&branches);
    edit_undo_destroy(&stack);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: null params                                                         */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    ASSERT_FALSE(undo_branches_init(NULL, 8, 32));
    ASSERT_UINT_EQ(undo_branches_count(NULL), 0);
    ASSERT_TRUE(undo_branches_get(NULL, 0) == NULL);

    edit_undo_stack_t stack;
    ASSERT_TRUE(edit_undo_init(&stack, 64, 4096));
    edit_undo_entry_t e = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 1,
    };
    ASSERT_FALSE(edit_undo_record_rebase(NULL, &e, NULL, 0, NULL));
    ASSERT_FALSE(edit_undo_record_rebase(&stack, NULL, NULL, 0, NULL));
    edit_undo_destroy(&stack);
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
    {"branches_init_destroy",            test_branches_init_destroy},
    {"rebase_no_redo",                   test_rebase_no_redo},
    {"rebase_non_conflicting_survives",  test_rebase_non_conflicting_survives},
    {"rebase_conflicting_orphaned",      test_rebase_conflicting_orphaned},
    {"rebase_partial_conflict",          test_rebase_partial_conflict},
    {"orphan_overflow_discards_oldest",  test_orphan_overflow_discards_oldest},
    {"null_params",                      test_null_params},
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
