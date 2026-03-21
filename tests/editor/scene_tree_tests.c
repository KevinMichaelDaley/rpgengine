/**
 * @file scene_tree_tests.c
 * @brief Unit tests for the LCRS scene tree.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/edit_scene_tree.h"

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

#define NONE EDIT_SCENE_TREE_NONE

/* ----------------------------------------------------------------------- */
/* Test: init and destroy                                                    */
/* ----------------------------------------------------------------------- */

static int test_init_destroy(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));
    ASSERT_UINT_EQ(tree.capacity, 64);

    /* All nodes should be roots (parent = NONE). */
    for (uint32_t i = 0; i < 64; i++) {
        ASSERT_TRUE(edit_scene_tree_is_root(&tree, i));
        ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, i), NONE);
        ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, i), NONE);
        ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, i), NONE);
    }

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: attach child to parent                                              */
/* ----------------------------------------------------------------------- */

static int test_attach_single_child(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    /* Attach entity 5 as child of entity 2. */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 5, 2));

    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 5), 2);
    ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, 2), 5);
    ASSERT_FALSE(edit_scene_tree_is_root(&tree, 5));
    ASSERT_TRUE(edit_scene_tree_is_root(&tree, 2));

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: attach multiple children (prepend order)                            */
/* ----------------------------------------------------------------------- */

static int test_attach_multiple_children(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    /* Attach 10, 11, 12 to parent 1 (prepend → first_child = 12). */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 10, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 11, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 12, 1));

    ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, 1), 12);
    ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, 12), 11);
    ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, 11), 10);
    ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, 10), NONE);

    /* All three have parent 1. */
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 10), 1);
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 11), 1);
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 12), 1);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: detach from parent                                                  */
/* ----------------------------------------------------------------------- */

static int test_detach(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    ASSERT_TRUE(edit_scene_tree_attach(&tree, 10, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 11, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 12, 1));

    /* Detach middle child (11). */
    edit_scene_tree_detach(&tree, 11);
    ASSERT_TRUE(edit_scene_tree_is_root(&tree, 11));
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 11), NONE);

    /* Remaining: 12 → 10. */
    ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, 1), 12);
    ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, 12), 10);
    ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, 10), NONE);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: detach first child                                                  */
/* ----------------------------------------------------------------------- */

static int test_detach_first_child(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    ASSERT_TRUE(edit_scene_tree_attach(&tree, 10, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 11, 1));

    /* Detach first child (11, since prepend). */
    edit_scene_tree_detach(&tree, 11);
    ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, 1), 10);
    ASSERT_UINT_EQ(edit_scene_tree_get_next_sibling(&tree, 10), NONE);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: reparent (attach already-attached child to new parent)              */
/* ----------------------------------------------------------------------- */

static int test_reparent(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    ASSERT_TRUE(edit_scene_tree_attach(&tree, 5, 1));
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 5), 1);

    /* Reparent 5 from 1 to 2. */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 5, 2));
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 5), 2);
    ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, 2), 5);

    /* Old parent 1 should have no children. */
    ASSERT_UINT_EQ(edit_scene_tree_get_first_child(&tree, 1), NONE);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: is_ancestor                                                         */
/* ----------------------------------------------------------------------- */

static int test_is_ancestor(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    /* Build: 0 → 1 → 2 → 3 */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 1, 0));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 2, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 3, 2));

    ASSERT_TRUE(edit_scene_tree_is_ancestor(&tree, 0, 3));
    ASSERT_TRUE(edit_scene_tree_is_ancestor(&tree, 1, 3));
    ASSERT_TRUE(edit_scene_tree_is_ancestor(&tree, 2, 3));
    ASSERT_FALSE(edit_scene_tree_is_ancestor(&tree, 3, 0));
    ASSERT_FALSE(edit_scene_tree_is_ancestor(&tree, 3, 3)); /* Not own ancestor. */

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: count descendants                                                   */
/* ----------------------------------------------------------------------- */

static int test_count_descendants(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    /* Build: 0 → {1, 2}, 1 → {3, 4}, 2 → {5} */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 1, 0));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 2, 0));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 3, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 4, 1));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 5, 2));

    ASSERT_UINT_EQ(edit_scene_tree_count_descendants(&tree, 0), 5);
    ASSERT_UINT_EQ(edit_scene_tree_count_descendants(&tree, 1), 2);
    ASSERT_UINT_EQ(edit_scene_tree_count_descendants(&tree, 2), 1);
    ASSERT_UINT_EQ(edit_scene_tree_count_descendants(&tree, 3), 0);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: attach to self rejected                                             */
/* ----------------------------------------------------------------------- */

static int test_attach_self_rejected(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    ASSERT_FALSE(edit_scene_tree_attach(&tree, 5, 5));

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: circular attach rejected                                            */
/* ----------------------------------------------------------------------- */

static int test_circular_rejected(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    /* Build: 0 → 1 → 2. Try to parent 0 under 2 (circular). */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 1, 0));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 2, 1));
    ASSERT_FALSE(edit_scene_tree_attach(&tree, 0, 2));

    /* Tree should be unchanged. */
    ASSERT_TRUE(edit_scene_tree_is_root(&tree, 0));
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 1), 0);
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(&tree, 2), 1);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: DFS iteration                                                       */
/* ----------------------------------------------------------------------- */

static int test_dfs_iteration(void) {
    edit_scene_tree_t tree;
    ASSERT_TRUE(edit_scene_tree_init(&tree, 64));

    /* Build: 0 → {1, 2}, 1 → {3} */
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 1, 0));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 2, 0));
    ASSERT_TRUE(edit_scene_tree_attach(&tree, 3, 1));

    /* DFS from 0 should visit: 0, 2, 1, 3 (prepend order: 2 before 1). */
    edit_scene_tree_iter_t it;
    edit_scene_tree_iter_init(&it, &tree, 0);

    uint32_t visited[10];
    uint32_t depths[10];
    uint32_t count = 0;
    uint32_t node;
    uint32_t depth;
    while (edit_scene_tree_iter_next(&it, &node, &depth)) {
        if (count < 10) {
            visited[count] = node;
            depths[count] = depth;
            count++;
        }
    }

    ASSERT_UINT_EQ(count, 4);
    ASSERT_UINT_EQ(visited[0], 0);
    ASSERT_UINT_EQ(depths[0], 0);
    ASSERT_UINT_EQ(visited[1], 2);
    ASSERT_UINT_EQ(depths[1], 1);
    ASSERT_UINT_EQ(visited[2], 1);
    ASSERT_UINT_EQ(depths[2], 1);
    ASSERT_UINT_EQ(visited[3], 3);
    ASSERT_UINT_EQ(depths[3], 2);

    edit_scene_tree_destroy(&tree);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: null params                                                         */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    ASSERT_FALSE(edit_scene_tree_init(NULL, 64));
    ASSERT_UINT_EQ(edit_scene_tree_get_parent(NULL, 0), NONE);
    ASSERT_TRUE(edit_scene_tree_is_root(NULL, 0));
    ASSERT_FALSE(edit_scene_tree_is_ancestor(NULL, 0, 1));
    ASSERT_UINT_EQ(edit_scene_tree_count_descendants(NULL, 0), 0);
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
    {"attach_single_child",       test_attach_single_child},
    {"attach_multiple_children",  test_attach_multiple_children},
    {"detach",                    test_detach},
    {"detach_first_child",        test_detach_first_child},
    {"reparent",                  test_reparent},
    {"is_ancestor",               test_is_ancestor},
    {"count_descendants",         test_count_descendants},
    {"attach_self_rejected",      test_attach_self_rejected},
    {"circular_rejected",         test_circular_rejected},
    {"dfs_iteration",             test_dfs_iteration},
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
