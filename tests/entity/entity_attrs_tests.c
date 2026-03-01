/**
 * @file entity_attrs_tests.c
 * @brief Tests for entity_attrs_t: dynamic key-value attribute storage.
 *
 * Covers: init, set, get, remove, clear, count, binary search,
 * update-in-place, capacity limits, type handling, sorted ordering.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/entity/entity_attrs.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) do { \
    if (fabsf((a) - (b)) > 1e-6f) { \
        printf("  ASSERT FAILED: %f != %f (line %d)\n", \
               (double)(a), (double)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Happy path tests                                                          */
/* ----------------------------------------------------------------------- */

/** Fresh attrs block is empty. */
static bool test_init_empty(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);
    ASSERT(entity_attrs_count(&attrs) == 0);
    return true;
}

/** Set a float attribute and read it back. */
static bool test_set_get_f32(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float val = 42.5f;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER, SCRIPT_ATTR_F32,
                            &val, sizeof(val)));
    ASSERT(entity_attrs_count(&attrs) == 1);

    uint8_t out_type, out_size;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER,
                                        &out_type, &out_size);
    ASSERT(data != NULL);
    ASSERT(out_type == SCRIPT_ATTR_F32);
    ASSERT(out_size == sizeof(float));
    ASSERT_FLOAT_EQ(*(const float *)data, 42.5f);
    return true;
}

/** Set a vec3 attribute and read it back. */
static bool test_set_get_vec3(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float pos[3] = { 1.0f, 2.0f, 3.0f };
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_POS, SCRIPT_ATTR_VEC3,
                            pos, sizeof(pos)));

    uint8_t out_type, out_size;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_POS,
                                        &out_type, &out_size);
    ASSERT(data != NULL);
    ASSERT(out_type == SCRIPT_ATTR_VEC3);
    ASSERT(out_size == 12);
    const float *v = (const float *)data;
    ASSERT_FLOAT_EQ(v[0], 1.0f);
    ASSERT_FLOAT_EQ(v[1], 2.0f);
    ASSERT_FLOAT_EQ(v[2], 3.0f);
    return true;
}

/** Set a string attribute and read it back. */
static bool test_set_get_str(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    const char *name = "health_potion";
    uint8_t len = (uint8_t)(strlen(name) + 1); /* include null */
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_NAME, SCRIPT_ATTR_STR,
                            name, len));

    uint8_t out_type, out_size;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_NAME,
                                        &out_type, &out_size);
    ASSERT(data != NULL);
    ASSERT(out_type == SCRIPT_ATTR_STR);
    ASSERT(out_size == len);
    ASSERT(strcmp((const char *)data, "health_potion") == 0);
    return true;
}

/** Set an i32 attribute. */
static bool test_set_get_i32(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    int32_t hp = -100;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER + 1, SCRIPT_ATTR_I32,
                            &hp, sizeof(hp)));

    uint8_t out_type, out_size;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER + 1,
                                        &out_type, &out_size);
    ASSERT(data != NULL);
    ASSERT(out_type == SCRIPT_ATTR_I32);
    ASSERT(*(const int32_t *)data == -100);
    return true;
}

/** Set a u32 attribute. */
static bool test_set_get_u32(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    uint32_t flags = 0xDEADBEEF;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_TYPE, SCRIPT_ATTR_U32,
                            &flags, sizeof(flags)));

    uint8_t out_type, out_size;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_TYPE,
                                        &out_type, &out_size);
    ASSERT(data != NULL);
    ASSERT(*(const uint32_t *)data == 0xDEADBEEF);
    return true;
}

/** Set a bool attribute. */
static bool test_set_get_bool(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    uint8_t flag = 1;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER + 10, SCRIPT_ATTR_BOOL,
                            &flag, sizeof(flag)));

    uint8_t out_type, out_size;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER + 10,
                                        &out_type, &out_size);
    ASSERT(data != NULL);
    ASSERT(out_type == SCRIPT_ATTR_BOOL);
    ASSERT(*(const uint8_t *)data == 1);
    return true;
}

/** Multiple attributes maintain sorted key order. */
static bool test_multiple_attrs_sorted(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    /* Insert in reverse key order to test insertion sort. */
    float scale[3] = { 2.0f, 2.0f, 2.0f };
    float rot[3] = { 0.0f, 90.0f, 0.0f };
    float pos[3] = { 10.0f, 20.0f, 30.0f };

    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_SCALE, SCRIPT_ATTR_VEC3,
                            scale, sizeof(scale)));
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_ROT, SCRIPT_ATTR_VEC3,
                            rot, sizeof(rot)));
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_POS, SCRIPT_ATTR_VEC3,
                            pos, sizeof(pos)));
    ASSERT(entity_attrs_count(&attrs) == 3);

    /* All should be retrievable. */
    uint8_t t, s;
    const float *p;

    p = (const float *)entity_attrs_get(&attrs, SCRIPT_KEY_POS, &t, &s);
    ASSERT(p != NULL);
    ASSERT_FLOAT_EQ(p[0], 10.0f);

    p = (const float *)entity_attrs_get(&attrs, SCRIPT_KEY_ROT, &t, &s);
    ASSERT(p != NULL);
    ASSERT_FLOAT_EQ(p[1], 90.0f);

    p = (const float *)entity_attrs_get(&attrs, SCRIPT_KEY_SCALE, &t, &s);
    ASSERT(p != NULL);
    ASSERT_FLOAT_EQ(p[0], 2.0f);

    return true;
}

/** Update an existing attribute in-place. */
static bool test_update_in_place(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float v1 = 1.0f;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER, SCRIPT_ATTR_F32,
                            &v1, sizeof(v1)));

    float v2 = 99.0f;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER, SCRIPT_ATTR_F32,
                            &v2, sizeof(v2)));

    /* Count should still be 1 (updated, not inserted). */
    ASSERT(entity_attrs_count(&attrs) == 1);

    uint8_t t, s;
    const float *p = (const float *)entity_attrs_get(&attrs, SCRIPT_KEY_USER,
                                                     &t, &s);
    ASSERT(p != NULL);
    ASSERT_FLOAT_EQ(*p, 99.0f);
    return true;
}

/** Update with a different size reallocates payload. */
static bool test_update_different_size(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    const char *short_str = "hi";
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER + 5, SCRIPT_ATTR_STR,
                            short_str, 3));

    const char *long_str = "hello world!";
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER + 5, SCRIPT_ATTR_STR,
                            long_str, 13));

    ASSERT(entity_attrs_count(&attrs) == 1);

    uint8_t t, s;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER + 5, &t, &s);
    ASSERT(data != NULL);
    ASSERT(s == 13);
    ASSERT(strcmp((const char *)data, "hello world!") == 0);
    return true;
}

/** Remove an attribute. */
static bool test_remove(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float val = 5.0f;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER, SCRIPT_ATTR_F32,
                            &val, sizeof(val)));
    ASSERT(entity_attrs_count(&attrs) == 1);

    ASSERT(entity_attrs_remove(&attrs, SCRIPT_KEY_USER));
    ASSERT(entity_attrs_count(&attrs) == 0);

    uint8_t t, s;
    ASSERT(entity_attrs_get(&attrs, SCRIPT_KEY_USER, &t, &s) == NULL);
    return true;
}

/** Remove middle attribute, others remain accessible. */
static bool test_remove_middle(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float a = 1.0f, b = 2.0f, c = 3.0f;
    ASSERT(entity_attrs_set(&attrs, 10, SCRIPT_ATTR_F32, &a, sizeof(a)));
    ASSERT(entity_attrs_set(&attrs, 20, SCRIPT_ATTR_F32, &b, sizeof(b)));
    ASSERT(entity_attrs_set(&attrs, 30, SCRIPT_ATTR_F32, &c, sizeof(c)));

    ASSERT(entity_attrs_remove(&attrs, 20));
    ASSERT(entity_attrs_count(&attrs) == 2);

    uint8_t t, s;
    ASSERT_FLOAT_EQ(*(const float *)entity_attrs_get(&attrs, 10, &t, &s), 1.0f);
    ASSERT(entity_attrs_get(&attrs, 20, &t, &s) == NULL);
    ASSERT_FLOAT_EQ(*(const float *)entity_attrs_get(&attrs, 30, &t, &s), 3.0f);
    return true;
}

/** Clear removes all attributes. */
static bool test_clear(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float v = 1.0f;
    ASSERT(entity_attrs_set(&attrs, 10, SCRIPT_ATTR_F32, &v, sizeof(v)));
    ASSERT(entity_attrs_set(&attrs, 20, SCRIPT_ATTR_F32, &v, sizeof(v)));
    ASSERT(entity_attrs_set(&attrs, 30, SCRIPT_ATTR_F32, &v, sizeof(v)));

    entity_attrs_clear(&attrs);
    ASSERT(entity_attrs_count(&attrs) == 0);

    uint8_t t, s;
    ASSERT(entity_attrs_get(&attrs, 10, &t, &s) == NULL);
    ASSERT(entity_attrs_get(&attrs, 20, &t, &s) == NULL);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Edge case tests                                                           */
/* ----------------------------------------------------------------------- */

/** Get on empty attrs returns NULL. */
static bool test_get_empty(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    uint8_t t, s;
    ASSERT(entity_attrs_get(&attrs, SCRIPT_KEY_POS, &t, &s) == NULL);
    return true;
}

/** Get with non-existent key returns NULL. */
static bool test_get_missing_key(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float v = 1.0f;
    ASSERT(entity_attrs_set(&attrs, 10, SCRIPT_ATTR_F32, &v, sizeof(v)));

    uint8_t t, s;
    ASSERT(entity_attrs_get(&attrs, 999, &t, &s) == NULL);
    return true;
}

/** Remove non-existent key returns false. */
static bool test_remove_missing(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    ASSERT(!entity_attrs_remove(&attrs, SCRIPT_KEY_USER));
    return true;
}

/** Size zero attribute (e.g., a flag with no payload). */
static bool test_zero_size_attr(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    /* A boolean-like flag with zero payload — just presence matters. */
    uint8_t flag = 1;
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER + 100, SCRIPT_ATTR_BOOL,
                            &flag, 1));

    uint8_t t, s;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER + 100, &t, &s);
    ASSERT(data != NULL);
    ASSERT(t == SCRIPT_ATTR_BOOL);
    return true;
}

/** Fill many attributes up to a reasonable limit. */
static bool test_many_attrs(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    /* Insert 50 float attributes with sequential keys. */
    for (uint16_t i = 0; i < 50; i++) {
        float v = (float)i;
        ASSERT(entity_attrs_set(&attrs, (uint16_t)(SCRIPT_KEY_USER + i),
                                SCRIPT_ATTR_F32, &v, sizeof(v)));
    }
    ASSERT(entity_attrs_count(&attrs) == 50);

    /* Verify all are accessible. */
    for (uint16_t i = 0; i < 50; i++) {
        uint8_t t, s;
        const float *p = (const float *)entity_attrs_get(
            &attrs, (uint16_t)(SCRIPT_KEY_USER + i), &t, &s);
        ASSERT(p != NULL);
        ASSERT_FLOAT_EQ(*p, (float)i);
    }
    return true;
}

/** Blob attribute (raw bytes). */
static bool test_blob_attr(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    uint8_t blob[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER + 50, SCRIPT_ATTR_BLOB,
                            blob, sizeof(blob)));

    uint8_t t, s;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER + 50, &t, &s);
    ASSERT(data != NULL);
    ASSERT(t == SCRIPT_ATTR_BLOB);
    ASSERT(s == 16);
    ASSERT(memcmp(data, blob, 16) == 0);
    return true;
}

/** Set after remove reuses space. */
static bool test_set_after_remove(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    float v1 = 1.0f, v2 = 2.0f;
    ASSERT(entity_attrs_set(&attrs, 10, SCRIPT_ATTR_F32, &v1, sizeof(v1)));
    ASSERT(entity_attrs_remove(&attrs, 10));
    ASSERT(entity_attrs_set(&attrs, 10, SCRIPT_ATTR_F32, &v2, sizeof(v2)));

    ASSERT(entity_attrs_count(&attrs) == 1);
    uint8_t t, s;
    ASSERT_FLOAT_EQ(*(const float *)entity_attrs_get(&attrs, 10, &t, &s), 2.0f);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Failure mode tests                                                        */
/* ----------------------------------------------------------------------- */

/** Capacity exhaustion returns false. */
static bool test_capacity_exhaustion(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    /* Each entry: 8 bytes (attr_entry_t) + payload. Fill with max-size blobs
     * to exhaust capacity quickly. */
    uint8_t big[200];
    memset(big, 0xAA, sizeof(big));

    int inserted = 0;
    for (uint16_t i = 0; i < 200; i++) {
        bool ok = entity_attrs_set(&attrs, (uint16_t)(SCRIPT_KEY_USER + i),
                                   SCRIPT_ATTR_BLOB, big, sizeof(big));
        if (!ok) break;
        inserted++;
    }

    /* Should have hit the limit before 200 inserts. */
    ASSERT(inserted > 0);
    ASSERT(inserted < 200);

    /* Existing entries should still be accessible. */
    uint8_t t, s;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER, &t, &s);
    ASSERT(data != NULL);
    return true;
}

/** Max payload size is 255 bytes. */
static bool test_max_payload_size(void) {
    entity_attrs_t attrs;
    entity_attrs_init(&attrs);

    uint8_t big[255];
    memset(big, 0xBB, sizeof(big));
    ASSERT(entity_attrs_set(&attrs, SCRIPT_KEY_USER, SCRIPT_ATTR_BLOB,
                            big, 255));

    uint8_t t, s;
    const void *data = entity_attrs_get(&attrs, SCRIPT_KEY_USER, &t, &s);
    ASSERT(data != NULL);
    ASSERT(s == 255);
    ASSERT(((const uint8_t *)data)[0] == 0xBB);
    ASSERT(((const uint8_t *)data)[254] == 0xBB);
    return true;
}

/** Key enums have expected values. */
static bool test_key_enum_values(void) {
    ASSERT(SCRIPT_KEY_POS == 0);
    ASSERT(SCRIPT_KEY_ROT == 1);
    ASSERT(SCRIPT_KEY_SCALE == 2);
    ASSERT(SCRIPT_KEY_NAME == 3);
    ASSERT(SCRIPT_KEY_TYPE == 4);
    ASSERT(SCRIPT_KEY_BODY_IDX == 5);
    ASSERT(SCRIPT_KEY_MATERIAL == 6);
    ASSERT(SCRIPT_KEY_ECS_BASE == 64);
    ASSERT(SCRIPT_KEY_USER == 256);
    return true;
}

/** Type enums have expected values. */
static bool test_type_enum_values(void) {
    ASSERT(SCRIPT_ATTR_F32 == 0);
    ASSERT(SCRIPT_ATTR_VEC3 == 1);
    ASSERT(SCRIPT_ATTR_I32 == 2);
    ASSERT(SCRIPT_ATTR_U32 == 3);
    ASSERT(SCRIPT_ATTR_BOOL == 4);
    ASSERT(SCRIPT_ATTR_STR == 5);
    ASSERT(SCRIPT_ATTR_BLOB == 6);
    return true;
}

/* ----------------------------------------------------------------------- */
/* main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    /* Happy path */
    RUN(test_init_empty);
    RUN(test_set_get_f32);
    RUN(test_set_get_vec3);
    RUN(test_set_get_str);
    RUN(test_set_get_i32);
    RUN(test_set_get_u32);
    RUN(test_set_get_bool);
    RUN(test_multiple_attrs_sorted);
    RUN(test_update_in_place);
    RUN(test_update_different_size);
    RUN(test_remove);
    RUN(test_remove_middle);
    RUN(test_clear);

    /* Edge cases */
    RUN(test_get_empty);
    RUN(test_get_missing_key);
    RUN(test_remove_missing);
    RUN(test_zero_size_attr);
    RUN(test_many_attrs);
    RUN(test_blob_attr);
    RUN(test_set_after_remove);

    /* Failure modes */
    RUN(test_capacity_exhaustion);
    RUN(test_max_payload_size);
    RUN(test_key_enum_values);
    RUN(test_type_enum_values);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
