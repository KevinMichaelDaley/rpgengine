/**
 * @file p201_pivot_offset_tests.c
 * @brief Tests for pivot_offset entity field.
 *
 * Verifies that pivot_offset is initialized to zero, can be set,
 * serializes/deserializes correctly, and round-trips through JSON.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_serialize.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _d = (float)(exp) - (float)(act);                                \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tol)) {                                                     \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "              \
                    "expected %.6f got %.6f (tol %.6f)\n",                    \
                    __FILE__, __LINE__, (double)(exp), (double)(act),          \
                    (double)(tol));                                            \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

static int test_pivot_offset_default_zero(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    ASSERT_TRUE(id != EDIT_ENTITY_INVALID_ID);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[0], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[2], 0.0001f);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_offset_set_and_read(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    ASSERT_TRUE(e != NULL);

    e->pivot_offset[0] = 1.5f;
    e->pivot_offset[1] = -2.0f;
    e->pivot_offset[2] = 0.25f;

    const edit_entity_t *ro = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_NEAR(1.5f,  ro->pivot_offset[0], 0.0001f);
    ASSERT_FLOAT_NEAR(-2.0f, ro->pivot_offset[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.25f, ro->pivot_offset[2], 0.0001f);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_offset_serialize_zero(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    /* Serialize — pivot_offset is zero, should NOT appear in JSON */
    char buf[4096];
    size_t len = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "pivot_offset") == NULL);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_offset_serialize_nonzero(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pivot_offset[0] = 1.0f;
    e->pivot_offset[1] = 2.0f;
    e->pivot_offset[2] = 3.0f;

    /* Serialize — pivot_offset should appear */
    char buf[4096];
    size_t len = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "pivot_offset") != NULL);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_offset_roundtrip(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pivot_offset[0] = 0.5f;
    e->pivot_offset[1] = -1.0f;
    e->pivot_offset[2] = 3.14f;

    /* Serialize */
    char buf[4096];
    size_t len = edit_level_serialize(&store, buf, sizeof(buf));
    ASSERT_TRUE(len > 0);

    /* Deserialize into fresh store */
    edit_entity_store_t store2;
    ASSERT_TRUE(edit_entity_store_init(&store2, 16));
    ASSERT_TRUE(edit_level_deserialize(&store2, buf, len));

    const edit_entity_t *loaded = edit_entity_store_get(&store2, 0);
    ASSERT_TRUE(loaded != NULL);
    ASSERT_FLOAT_NEAR(0.5f,  loaded->pivot_offset[0], 0.001f);
    ASSERT_FLOAT_NEAR(-1.0f, loaded->pivot_offset[1], 0.001f);
    ASSERT_FLOAT_NEAR(3.14f, loaded->pivot_offset[2], 0.01f);

    edit_entity_store_destroy(&store2);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_offset_deserialize_missing(void) {
    /* JSON without pivot_offset should default to (0,0,0) */
    const char *json = "{\"version\":1,\"entities\":[{\"id\":0,\"type\":\"box\","
                       "\"pos\":[1,2,3],\"rot\":[0,0,0],\"scale\":[1,1,1]}]}";

    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    ASSERT_TRUE(edit_level_deserialize(&store, json, strlen(json)));

    const edit_entity_t *e = edit_entity_store_get(&store, 0);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[0], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[2], 0.0001f);

    edit_entity_store_destroy(&store);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"pivot_offset_default_zero",      test_pivot_offset_default_zero},
    {"pivot_offset_set_and_read",      test_pivot_offset_set_and_read},
    {"pivot_offset_serialize_zero",    test_pivot_offset_serialize_zero},
    {"pivot_offset_serialize_nonzero", test_pivot_offset_serialize_nonzero},
    {"pivot_offset_roundtrip",         test_pivot_offset_roundtrip},
    {"pivot_offset_deserialize_missing", test_pivot_offset_deserialize_missing},
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
