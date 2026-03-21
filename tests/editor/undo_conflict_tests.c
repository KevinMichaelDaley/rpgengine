/**
 * @file undo_conflict_tests.c
 * @brief Unit tests for undo conflict detection.
 */

#include <stdio.h>
#include <string.h>
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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Test: same entity, same operation type → conflict                        */
/* ----------------------------------------------------------------------- */

static int test_same_entity_transform_conflicts(void) {
    edit_undo_entry_t a = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 42,
    };
    edit_undo_entry_t b = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 42,
    };
    undo_conflict_key_t ka = undo_conflict_key_extract(&a);
    undo_conflict_key_t kb = undo_conflict_key_extract(&b);
    ASSERT_TRUE(undo_conflict_check(&ka, &kb));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: different entities → no conflict                                   */
/* ----------------------------------------------------------------------- */

static int test_different_entities_no_conflict(void) {
    edit_undo_entry_t a = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 1,
    };
    edit_undo_entry_t b = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 2,
    };
    undo_conflict_key_t ka = undo_conflict_key_extract(&a);
    undo_conflict_key_t kb = undo_conflict_key_extract(&b);
    ASSERT_FALSE(undo_conflict_check(&ka, &kb));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: different ops, same entity → conflict                              */
/* ----------------------------------------------------------------------- */

static int test_different_ops_same_entity_conflict(void) {
    edit_undo_entry_t a = {
        .forward_type = EDIT_CMD_TYPE_MOVE,
        .entity_id    = 10,
    };
    edit_undo_entry_t b = {
        .forward_type = EDIT_CMD_TYPE_SCALE,
        .entity_id    = 10,
    };
    undo_conflict_key_t ka = undo_conflict_key_extract(&a);
    undo_conflict_key_t kb = undo_conflict_key_extract(&b);
    ASSERT_TRUE(undo_conflict_check(&ka, &kb));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: spawn vs delete on same entity → conflict                          */
/* ----------------------------------------------------------------------- */

static int test_spawn_delete_same_entity_conflict(void) {
    edit_undo_entry_t a = {
        .forward_type = EDIT_CMD_TYPE_SPAWN,
        .entity_id    = 7,
    };
    edit_undo_entry_t b = {
        .forward_type = EDIT_CMD_TYPE_DELETE,
        .entity_id    = 7,
    };
    undo_conflict_key_t ka = undo_conflict_key_extract(&a);
    undo_conflict_key_t kb = undo_conflict_key_extract(&b);
    ASSERT_TRUE(undo_conflict_check(&ka, &kb));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: null entry returns zero key                                        */
/* ----------------------------------------------------------------------- */

static int test_null_entry_zero_key(void) {
    undo_conflict_key_t k = undo_conflict_key_extract(NULL);
    ASSERT_UINT_EQ(k.entity_id, 0);
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
    {"same_entity_transform_conflicts",     test_same_entity_transform_conflicts},
    {"different_entities_no_conflict",       test_different_entities_no_conflict},
    {"different_ops_same_entity_conflict",   test_different_ops_same_entity_conflict},
    {"spawn_delete_same_entity_conflict",    test_spawn_delete_same_entity_conflict},
    {"null_entry_zero_key",                  test_null_entry_zero_key},
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
