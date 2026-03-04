/**
 * @file p004_renderer_draw_list_tests.c
 * @brief Tests for draw_list_t: init, push, clear, sort, and key packing.
 *
 * Pure CPU tests — no GL context required.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/draw/draw_sort.h"
#include "ferrum/renderer/draw/draw_list.h"

/* ── Test macros ──────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, (int)(exp), (int)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_UINT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", \
                __FILE__, __LINE__, (unsigned)(exp), (unsigned)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_U64_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_U64_EQ failed: %s:%d: expected %llu got %llu\n",\
                __FILE__, __LINE__, \
                (unsigned long long)(exp), (unsigned long long)(act)); \
        return 1; \
    } \
} while (0)

/* ═══════════════════════════════════════════════════════════════════
 *  SORT KEY TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: key packing preserves all fields ─────────────────────── */
static int test_key_pack_roundtrip(void) {
    draw_sort_key_t k = draw_sort_key_build(7, 42, 100, 0xABCD);
    ASSERT_UINT_EQ(7,      draw_sort_key_shader(k));
    ASSERT_UINT_EQ(42,     draw_sort_key_material(k));
    ASSERT_UINT_EQ(100,    draw_sort_key_mesh(k));
    ASSERT_UINT_EQ(0xABCD, draw_sort_key_depth(k));
    return 0;
}

/* ── Test: shader has highest sort priority ─────────────────────── */
static int test_key_shader_priority(void) {
    draw_sort_key_t a = draw_sort_key_build(1, 0, 0, 0);
    draw_sort_key_t b = draw_sort_key_build(2, 0, 0, 0);
    ASSERT_TRUE(a.key < b.key);
    return 0;
}

/* ── Test: material sorts within same shader ────────────────────── */
static int test_key_material_priority(void) {
    draw_sort_key_t a = draw_sort_key_build(5, 10, 0, 0);
    draw_sort_key_t b = draw_sort_key_build(5, 20, 0, 0);
    ASSERT_TRUE(a.key < b.key);
    return 0;
}

/* ── Test: mesh sorts within same shader+material ───────────────── */
static int test_key_mesh_priority(void) {
    draw_sort_key_t a = draw_sort_key_build(5, 10, 1, 0);
    draw_sort_key_t b = draw_sort_key_build(5, 10, 2, 0);
    ASSERT_TRUE(a.key < b.key);
    return 0;
}

/* ── Test: depth is lowest priority ─────────────────────────────── */
static int test_key_depth_priority(void) {
    draw_sort_key_t a = draw_sort_key_build(5, 10, 1, 100);
    draw_sort_key_t b = draw_sort_key_build(5, 10, 1, 200);
    ASSERT_TRUE(a.key < b.key);
    return 0;
}

/* ── Test: max values fit without overflow ───────────────────────── */
static int test_key_max_values(void) {
    draw_sort_key_t k = draw_sort_key_build(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);
    ASSERT_U64_EQ(UINT64_MAX, k.key);
    ASSERT_UINT_EQ(0xFFFF, draw_sort_key_shader(k));
    ASSERT_UINT_EQ(0xFFFF, draw_sort_key_material(k));
    ASSERT_UINT_EQ(0xFFFF, draw_sort_key_mesh(k));
    ASSERT_UINT_EQ(0xFFFF, draw_sort_key_depth(k));
    return 0;
}

/* ── Test: zero key ─────────────────────────────────────────────── */
static int test_key_zero(void) {
    draw_sort_key_t k = draw_sort_key_build(0, 0, 0, 0);
    ASSERT_U64_EQ(0, k.key);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  DRAW LIST LIFECYCLE TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init creates empty list with requested capacity ──────── */
static int test_list_init(void) {
    draw_list_t list;
    int rc = draw_list_init(&list, 256);
    ASSERT_INT_EQ(DRAW_LIST_OK, rc);
    ASSERT_UINT_EQ(0,   list.count);
    ASSERT_UINT_EQ(256, list.capacity);
    draw_list_destroy(&list);
    return 0;
}

/* ── Test: push adds command and increments count ───────────────── */
static int test_list_push(void) {
    draw_list_t list;
    draw_list_init(&list, 16);

    draw_command_t cmd = {0};
    cmd.sort_key = draw_sort_key_build(1, 2, 3, 4);
    cmd.submesh_index = 7;
    cmd.instance_count = 1;

    int rc = draw_list_push(&list, &cmd);
    ASSERT_INT_EQ(DRAW_LIST_OK, rc);
    ASSERT_UINT_EQ(1, list.count);
    ASSERT_UINT_EQ(7, list.commands[0].submesh_index);

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: clear resets count but keeps capacity ────────────────── */
static int test_list_clear(void) {
    draw_list_t list;
    draw_list_init(&list, 16);

    draw_command_t cmd = {0};
    cmd.sort_key = draw_sort_key_build(0, 0, 0, 0);
    cmd.instance_count = 1;
    draw_list_push(&list, &cmd);
    draw_list_push(&list, &cmd);

    draw_list_clear(&list);
    ASSERT_UINT_EQ(0,  list.count);
    ASSERT_UINT_EQ(16, list.capacity);

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: push to full list returns error ───────────────────────── */
static int test_list_full(void) {
    draw_list_t list;
    draw_list_init(&list, 2);

    draw_command_t cmd = {0};
    cmd.sort_key = draw_sort_key_build(0, 0, 0, 0);
    cmd.instance_count = 1;
    draw_list_push(&list, &cmd);
    draw_list_push(&list, &cmd);

    int rc = draw_list_push(&list, &cmd);
    ASSERT_INT_EQ(DRAW_LIST_ERR_FULL, rc);
    ASSERT_UINT_EQ(2, list.count);

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: destroy NULL is safe ─────────────────────────────────── */
static int test_list_destroy_null(void) {
    draw_list_destroy(NULL);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  SORT TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: sort puts commands in ascending key order ────────────── */
static int test_sort_ascending(void) {
    draw_list_t list;
    draw_list_init(&list, 16);

    /* Insert in reverse order. */
    for (uint16_t i = 8; i > 0; --i) {
        draw_command_t cmd = {0};
        cmd.sort_key = draw_sort_key_build(i, 0, 0, 0);
        cmd.submesh_index = i;
        cmd.instance_count = 1;
        draw_list_push(&list, &cmd);
    }
    ASSERT_UINT_EQ(8, list.count);

    draw_list_sort(&list);

    /* Verify ascending order. */
    for (uint32_t i = 0; i < list.count; ++i) {
        ASSERT_UINT_EQ(i + 1, list.commands[i].submesh_index);
    }

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: sort is stable (equal keys preserve insertion order) ── */
static int test_sort_stable(void) {
    draw_list_t list;
    draw_list_init(&list, 16);

    /* 4 commands with same sort key but different submesh_index. */
    for (uint32_t i = 0; i < 4; ++i) {
        draw_command_t cmd = {0};
        cmd.sort_key = draw_sort_key_build(1, 1, 1, 1);
        cmd.submesh_index = i;
        cmd.instance_count = 1;
        draw_list_push(&list, &cmd);
    }

    draw_list_sort(&list);

    /* Radix sort is stable — insertion order preserved. */
    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_UINT_EQ(i, list.commands[i].submesh_index);
    }

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: sort by shader > material > mesh > depth ─────────────── */
static int test_sort_full_priority(void) {
    draw_list_t list;
    draw_list_init(&list, 16);

    /* cmd A: shader=2, mat=1, mesh=1, depth=1 */
    draw_command_t a = {0};
    a.sort_key = draw_sort_key_build(2, 1, 1, 1);
    a.submesh_index = 0;
    a.instance_count = 1;

    /* cmd B: shader=1, mat=99, mesh=99, depth=99 */
    draw_command_t b = {0};
    b.sort_key = draw_sort_key_build(1, 99, 99, 99);
    b.submesh_index = 1;
    b.instance_count = 1;

    /* cmd C: shader=1, mat=1, mesh=50, depth=0 */
    draw_command_t c = {0};
    c.sort_key = draw_sort_key_build(1, 1, 50, 0);
    c.submesh_index = 2;
    c.instance_count = 1;

    /* Push in wrong order: A, B, C. */
    draw_list_push(&list, &a);
    draw_list_push(&list, &b);
    draw_list_push(&list, &c);

    draw_list_sort(&list);

    /* Expected order: C (1,1,50,0), B (1,99,99,99), A (2,1,1,1). */
    ASSERT_UINT_EQ(2, list.commands[0].submesh_index); /* C */
    ASSERT_UINT_EQ(1, list.commands[1].submesh_index); /* B */
    ASSERT_UINT_EQ(0, list.commands[2].submesh_index); /* A */

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: sort empty list is no-op ─────────────────────────────── */
static int test_sort_empty(void) {
    draw_list_t list;
    draw_list_init(&list, 16);
    draw_list_sort(&list);
    ASSERT_UINT_EQ(0, list.count);
    draw_list_destroy(&list);
    return 0;
}

/* ── Test: sort single element ──────────────────────────────────── */
static int test_sort_single(void) {
    draw_list_t list;
    draw_list_init(&list, 16);

    draw_command_t cmd = {0};
    cmd.sort_key = draw_sort_key_build(5, 5, 5, 5);
    cmd.submesh_index = 42;
    cmd.instance_count = 1;
    draw_list_push(&list, &cmd);

    draw_list_sort(&list);
    ASSERT_UINT_EQ(42, list.commands[0].submesh_index);

    draw_list_destroy(&list);
    return 0;
}

/* ── Test: large sort (1024 commands) ───────────────────────────── */
static int test_sort_large(void) {
    draw_list_t list;
    draw_list_init(&list, 1024);

    /* Insert with descending keys. */
    for (uint32_t i = 0; i < 1024; ++i) {
        draw_command_t cmd = {0};
        uint16_t shader = (uint16_t)((1023 - i) >> 6);
        uint16_t mat    = (uint16_t)((1023 - i) & 0x3F);
        cmd.sort_key = draw_sort_key_build(shader, mat, 0, 0);
        cmd.submesh_index = i;
        cmd.instance_count = 1;
        draw_list_push(&list, &cmd);
    }

    draw_list_sort(&list);

    /* Verify sorted ascending. */
    for (uint32_t i = 1; i < list.count; ++i) {
        ASSERT_TRUE(list.commands[i - 1].sort_key.key <=
                    list.commands[i].sort_key.key);
    }

    draw_list_destroy(&list);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  FAILURE MODES
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init NULL ────────────────────────────────────────────── */
static int test_init_null(void) {
    int rc = draw_list_init(NULL, 64);
    ASSERT_INT_EQ(DRAW_LIST_ERR_INVALID, rc);
    return 0;
}

/* ── Test: init zero capacity ───────────────────────────────────── */
static int test_init_zero_capacity(void) {
    draw_list_t list;
    int rc = draw_list_init(&list, 0);
    ASSERT_INT_EQ(DRAW_LIST_ERR_INVALID, rc);
    return 0;
}

/* ── Test: push NULL list ───────────────────────────────────────── */
static int test_push_null_list(void) {
    draw_command_t cmd = {0};
    int rc = draw_list_push(NULL, &cmd);
    ASSERT_INT_EQ(DRAW_LIST_ERR_INVALID, rc);
    return 0;
}

/* ── Test: push NULL command ────────────────────────────────────── */
static int test_push_null_cmd(void) {
    draw_list_t list;
    draw_list_init(&list, 16);
    int rc = draw_list_push(&list, NULL);
    ASSERT_INT_EQ(DRAW_LIST_ERR_INVALID, rc);
    draw_list_destroy(&list);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Test runner
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct { const char *name; int (*fn)(void); } test_entry_t;

static const test_entry_t TESTS[] = {
    /* Sort key */
    {"key_pack_roundtrip",    test_key_pack_roundtrip},
    {"key_shader_priority",   test_key_shader_priority},
    {"key_material_priority", test_key_material_priority},
    {"key_mesh_priority",     test_key_mesh_priority},
    {"key_depth_priority",    test_key_depth_priority},
    {"key_max_values",        test_key_max_values},
    {"key_zero",              test_key_zero},
    /* Draw list lifecycle */
    {"list_init",             test_list_init},
    {"list_push",             test_list_push},
    {"list_clear",            test_list_clear},
    {"list_full",             test_list_full},
    {"list_destroy_null",     test_list_destroy_null},
    /* Sort */
    {"sort_ascending",        test_sort_ascending},
    {"sort_stable",           test_sort_stable},
    {"sort_full_priority",    test_sort_full_priority},
    {"sort_empty",            test_sort_empty},
    {"sort_single",           test_sort_single},
    {"sort_large",            test_sort_large},
    /* Failure modes */
    {"init_null",             test_init_null},
    {"init_zero_capacity",    test_init_zero_capacity},
    {"push_null_list",        test_push_null_list},
    {"push_null_cmd",         test_push_null_cmd},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("  OK %s\n", TESTS[i].name);
            ++passed;
        } else {
            printf("FAIL %s\n", TESTS[i].name);
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
