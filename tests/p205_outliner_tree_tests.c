/**
 * @file p205_outliner_tree_tests.c
 * @brief Tests for the outliner tree model — flat list of entities
 *        with expand/collapse, filtering, and selection sync.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/panels/outliner_tree.h"
#include "ferrum/editor/edit_entity.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

static int test_outliner_init_empty(void) {
    outliner_tree_t tree;
    outliner_tree_init(&tree);

    ASSERT_TRUE(outliner_tree_count(&tree) == 0);
    ASSERT_TRUE(tree.filter[0] == '\0');

    outliner_tree_destroy(&tree);
    return 0;
}

static int test_outliner_rebuild_from_store(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_CAPSULE);

    outliner_tree_t tree;
    outliner_tree_init(&tree);
    outliner_tree_rebuild(&tree, &store);

    ASSERT_TRUE(outliner_tree_count(&tree) == 3);

    outliner_tree_destroy(&tree);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_outliner_get_entry(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id0);
    snprintf(e->name, sizeof(e->name), "TestBox");

    outliner_tree_t tree;
    outliner_tree_init(&tree);
    outliner_tree_rebuild(&tree, &store);

    const outliner_entry_t *entry = outliner_tree_get(&tree, 0);
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->entity_id == id0);
    ASSERT_TRUE(strcmp(entry->display_name, "TestBox") == 0);

    outliner_tree_destroy(&tree);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_outliner_unnamed_entity(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);

    outliner_tree_t tree;
    outliner_tree_init(&tree);
    outliner_tree_rebuild(&tree, &store);

    const outliner_entry_t *entry = outliner_tree_get(&tree, 0);
    ASSERT_TRUE(entry != NULL);
    /* Unnamed entity should get a generated name like "sphere_0". */
    ASSERT_TRUE(entry->display_name[0] != '\0');

    outliner_tree_destroy(&tree);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_outliner_filter(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e0 = edit_entity_store_get_mut(&store, id0);
    snprintf(e0->name, sizeof(e0->name), "WallLeft");

    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e1 = edit_entity_store_get_mut(&store, id1);
    snprintf(e1->name, sizeof(e1->name), "Floor");

    uint32_t id2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e2 = edit_entity_store_get_mut(&store, id2);
    snprintf(e2->name, sizeof(e2->name), "WallRight");

    outliner_tree_t tree;
    outliner_tree_init(&tree);
    outliner_tree_rebuild(&tree, &store);

    ASSERT_TRUE(outliner_tree_count(&tree) == 3);

    /* Filter for "Wall". */
    outliner_tree_set_filter(&tree, "Wall");
    ASSERT_TRUE(outliner_tree_count(&tree) == 2);

    /* Clear filter. */
    outliner_tree_set_filter(&tree, "");
    ASSERT_TRUE(outliner_tree_count(&tree) == 3);

    outliner_tree_destroy(&tree);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_outliner_filter_case_insensitive(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e0 = edit_entity_store_get_mut(&store, id0);
    snprintf(e0->name, sizeof(e0->name), "MyBox");

    outliner_tree_t tree;
    outliner_tree_init(&tree);
    outliner_tree_rebuild(&tree, &store);

    outliner_tree_set_filter(&tree, "mybox");
    ASSERT_TRUE(outliner_tree_count(&tree) == 1);

    outliner_tree_destroy(&tree);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_outliner_scroll(void) {
    outliner_tree_t tree;
    outliner_tree_init(&tree);

    /* Scroll offset starts at 0. */
    ASSERT_TRUE(tree.scroll_offset == 0);

    outliner_tree_scroll(&tree, 5);
    ASSERT_TRUE(tree.scroll_offset == 5);

    /* Scroll cannot go negative. */
    outliner_tree_scroll(&tree, -100);
    ASSERT_TRUE(tree.scroll_offset == 0);

    outliner_tree_destroy(&tree);
    return 0;
}

static int test_outliner_empty_store(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    outliner_tree_t tree;
    outliner_tree_init(&tree);
    outliner_tree_rebuild(&tree, &store);

    ASSERT_TRUE(outliner_tree_count(&tree) == 0);
    ASSERT_TRUE(outliner_tree_get(&tree, 0) == NULL);

    outliner_tree_destroy(&tree);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_outliner_get_out_of_bounds(void) {
    outliner_tree_t tree;
    outliner_tree_init(&tree);

    ASSERT_TRUE(outliner_tree_get(&tree, 0) == NULL);
    ASSERT_TRUE(outliner_tree_get(&tree, 999) == NULL);

    outliner_tree_destroy(&tree);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"outliner_init_empty",        test_outliner_init_empty},
    {"outliner_rebuild_from_store", test_outliner_rebuild_from_store},
    {"outliner_get_entry",         test_outliner_get_entry},
    {"outliner_unnamed_entity",    test_outliner_unnamed_entity},
    {"outliner_filter",            test_outliner_filter},
    {"outliner_filter_case_insensitive", test_outliner_filter_case_insensitive},
    {"outliner_scroll",            test_outliner_scroll},
    {"outliner_empty_store",       test_outliner_empty_store},
    {"outliner_get_out_of_bounds", test_outliner_get_out_of_bounds},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
