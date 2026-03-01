/**
 * @file aegis_memory_tests.c
 * @brief Unit tests for Aegis three-zone memory layout.
 *
 * Covers: init/partitioning, static array, call stack, heap arena,
 * bounds checking, heap reset, zone boundary enforcement.
 *
 * Per ref/aegis_bytecode_spec.md §3.6.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_memory.h"

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

#define ASSERT_INT_EQ(a, b) do { \
    if ((int)(a) != (int)(b)) { \
        printf("  ASSERT FAILED: %d != %d (line %d)\n", \
               (int)(a), (int)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Helper: create a memory instance on the stack                             */
/* ----------------------------------------------------------------------- */

/* 1 KB arena: 256 static + 256 stack + 512 heap */
#define TEST_ARENA_SIZE   1024
#define TEST_STATIC_SIZE  256
#define TEST_STACK_SIZE   256

static bool make_test_mem(aegis_memory_t *mem, uint8_t *buf) {
    memset(buf, 0, TEST_ARENA_SIZE);
    return aegis_memory_init(mem, buf, TEST_ARENA_SIZE,
                             TEST_STATIC_SIZE, TEST_STACK_SIZE);
}

/* ======================================================================= */
/* Init / partition tests                                                    */
/* ======================================================================= */

/** Init succeeds with valid sizes. */
static bool test_init_valid(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    ASSERT(make_test_mem(&mem, buf));
    return true;
}

/** Init fails if arena_size < static_size + stack_size. */
static bool test_init_too_small(void) {
    uint8_t buf[128];
    aegis_memory_t mem;
    /* 128 < 256 + 256 = 512 */
    ASSERT(!aegis_memory_init(&mem, buf, 128, 256, 256));
    return true;
}

/** Init fails if arena_size is zero. */
static bool test_init_zero_arena(void) {
    uint8_t buf[1];
    aegis_memory_t mem;
    ASSERT(!aegis_memory_init(&mem, buf, 0, 0, 0));
    return true;
}

/** Init with zero static and zero stack is valid (heap-only). */
static bool test_init_heap_only(void) {
    uint8_t buf[512];
    aegis_memory_t mem;
    ASSERT(aegis_memory_init(&mem, buf, 512, 0, 0));
    return true;
}

/* ======================================================================= */
/* Static array tests                                                        */
/* ======================================================================= */

/** Write to static array and read back. */
static bool test_static_store_load(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.i32 = 42;

    ASSERT(aegis_memory_static_store(&mem, 0, &val));

    aegis_register_t out;
    ASSERT(aegis_memory_static_load(&mem, 0, &out));
    ASSERT_INT_EQ(42, out.i32);
    return true;
}

/** Static array at max valid offset (last aligned slot). */
static bool test_static_max_offset(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    /* Max valid offset: static_size - 16. */
    uint32_t max_off = TEST_STATIC_SIZE - 16;
    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.i32 = 99;

    ASSERT(aegis_memory_static_store(&mem, max_off, &val));
    aegis_register_t out;
    ASSERT(aegis_memory_static_load(&mem, max_off, &out));
    ASSERT_INT_EQ(99, out.i32);
    return true;
}

/** Static array out-of-bounds write fails. */
static bool test_static_oob_write(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    /* Offset would read past static region. */
    ASSERT(!aegis_memory_static_store(&mem, TEST_STATIC_SIZE, &val));
    return true;
}

/** Static array out-of-bounds read fails. */
static bool test_static_oob_read(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t out;
    ASSERT(!aegis_memory_static_load(&mem, TEST_STATIC_SIZE, &out));
    return true;
}

/** Static persists across heap reset. */
static bool test_static_survives_heap_reset(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.i32 = 777;
    ASSERT(aegis_memory_static_store(&mem, 0, &val));

    aegis_memory_heap_reset(&mem);

    aegis_register_t out;
    ASSERT(aegis_memory_static_load(&mem, 0, &out));
    ASSERT_INT_EQ(777, out.i32);
    return true;
}

/* ======================================================================= */
/* Call stack tests                                                          */
/* ======================================================================= */

/** Push and pop a register value. */
static bool test_stack_push_pop(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.f32 = 3.14f;

    ASSERT(aegis_memory_stack_push(&mem, &val));

    aegis_register_t out;
    ASSERT(aegis_memory_stack_pop(&mem, &out));
    ASSERT(fabsf(out.f32 - 3.14f) < 1e-6f);
    return true;
}

/** Push multiple, pop in LIFO order. */
static bool test_stack_lifo(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    for (int i = 0; i < 10; i++) {
        aegis_register_t val;
        memset(&val, 0, sizeof(val));
        val.i32 = i * 100;
        ASSERT(aegis_memory_stack_push(&mem, &val));
    }

    for (int i = 9; i >= 0; i--) {
        aegis_register_t out;
        ASSERT(aegis_memory_stack_pop(&mem, &out));
        ASSERT_INT_EQ(i * 100, out.i32);
    }
    return true;
}

/** Stack overflow returns false. */
static bool test_stack_overflow(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    /* Stack is 256 bytes, each push is 16 bytes → max 16 pushes. */
    int max_pushes = TEST_STACK_SIZE / 16;
    aegis_register_t val;
    memset(&val, 0, sizeof(val));

    for (int i = 0; i < max_pushes; i++) {
        ASSERT(aegis_memory_stack_push(&mem, &val));
    }
    /* Next push should fail. */
    ASSERT(!aegis_memory_stack_push(&mem, &val));
    return true;
}

/** Pop from empty stack returns false. */
static bool test_stack_underflow(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t out;
    ASSERT(!aegis_memory_stack_pop(&mem, &out));
    return true;
}

/** Push call frame and pop it. */
static bool test_frame_push_pop(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    ASSERT(aegis_memory_push_frame(&mem, 42));

    uint32_t return_pc;
    ASSERT(aegis_memory_pop_frame(&mem, &return_pc));
    ASSERT_INT_EQ(42, (int)return_pc);
    return true;
}

/** Nested call frames. */
static bool test_frame_nested(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    ASSERT(aegis_memory_push_frame(&mem, 10));
    ASSERT(aegis_memory_push_frame(&mem, 20));
    ASSERT(aegis_memory_push_frame(&mem, 30));

    uint32_t pc;
    ASSERT(aegis_memory_pop_frame(&mem, &pc));
    ASSERT_INT_EQ(30, (int)pc);
    ASSERT(aegis_memory_pop_frame(&mem, &pc));
    ASSERT_INT_EQ(20, (int)pc);
    ASSERT(aegis_memory_pop_frame(&mem, &pc));
    ASSERT_INT_EQ(10, (int)pc);
    return true;
}

/** Stack depth query. */
static bool test_stack_depth(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    ASSERT_INT_EQ(0, (int)aegis_memory_call_depth(&mem));
    ASSERT(aegis_memory_push_frame(&mem, 0));
    ASSERT_INT_EQ(1, (int)aegis_memory_call_depth(&mem));
    uint32_t pc;
    aegis_memory_pop_frame(&mem, &pc);
    ASSERT_INT_EQ(0, (int)aegis_memory_call_depth(&mem));
    return true;
}

/* ======================================================================= */
/* Heap arena tests                                                         */
/* ======================================================================= */

/** Allocate from heap returns valid offset. */
static bool test_heap_alloc(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    int32_t off = aegis_memory_alloc(&mem, 32);
    ASSERT(off >= 0);
    return true;
}

/** Multiple allocations return non-overlapping offsets. */
static bool test_heap_alloc_no_overlap(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    int32_t a = aegis_memory_alloc(&mem, 64);
    int32_t b = aegis_memory_alloc(&mem, 64);
    ASSERT(a >= 0);
    ASSERT(b >= 0);
    ASSERT(b >= a + 64);
    return true;
}

/** Heap overflow returns -1. */
static bool test_heap_overflow(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    /* Heap is arena - static - stack = 1024 - 256 - 256 = 512 bytes. */
    int32_t a = aegis_memory_alloc(&mem, 512);
    ASSERT(a >= 0);
    /* Next alloc should fail. */
    int32_t b = aegis_memory_alloc(&mem, 1);
    ASSERT_INT_EQ(-1, b);
    return true;
}

/** Zero-size alloc returns valid offset (noop bump). */
static bool test_heap_alloc_zero(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    int32_t off = aegis_memory_alloc(&mem, 0);
    ASSERT(off >= 0);
    return true;
}

/** Heap reset frees all heap allocations. */
static bool test_heap_reset(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_memory_alloc(&mem, 256);
    aegis_memory_alloc(&mem, 200);
    aegis_memory_heap_reset(&mem);

    /* After reset, full heap available again. */
    int32_t off = aegis_memory_alloc(&mem, 512);
    ASSERT(off >= 0);
    return true;
}

/** Heap load/store with bounds checking. */
static bool test_heap_load_store(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    int32_t off = aegis_memory_alloc(&mem, 16);
    ASSERT(off >= 0);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.i32 = 999;
    ASSERT(aegis_memory_heap_store(&mem, (uint32_t)off, 0, &val));

    aegis_register_t out;
    ASSERT(aegis_memory_heap_load(&mem, (uint32_t)off, 0, &out));
    ASSERT_INT_EQ(999, out.i32);
    return true;
}

/** Heap load out-of-bounds fails. */
static bool test_heap_oob_load(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t out;
    /* Accessing past arena end. */
    ASSERT(!aegis_memory_heap_load(&mem, TEST_ARENA_SIZE, 0, &out));
    return true;
}

/** Heap store out-of-bounds fails. */
static bool test_heap_oob_store(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    ASSERT(!aegis_memory_heap_store(&mem, TEST_ARENA_SIZE, 0, &val));
    return true;
}

/* ======================================================================= */
/* Zone boundary tests                                                      */
/* ======================================================================= */

/** Heap store cannot write into static zone. */
static bool test_heap_no_static_write(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    /* Offset 0 is in the static zone, not heap. */
    ASSERT(!aegis_memory_heap_store(&mem, 0, 0, &val));
    return true;
}

/** Static store partial overflow fails (offset + 16 > static_size). */
static bool test_static_partial_overflow(void) {
    uint8_t buf[TEST_ARENA_SIZE];
    aegis_memory_t mem;
    make_test_mem(&mem, buf);

    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    /* Offset 248: 248 + 16 = 264 > 256 static bytes. */
    ASSERT(!aegis_memory_static_store(&mem, 248, &val));
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Memory Tests ===\n\n");

    /* Init */
    RUN(test_init_valid);
    RUN(test_init_too_small);
    RUN(test_init_zero_arena);
    RUN(test_init_heap_only);

    /* Static array */
    RUN(test_static_store_load);
    RUN(test_static_max_offset);
    RUN(test_static_oob_write);
    RUN(test_static_oob_read);
    RUN(test_static_survives_heap_reset);

    /* Call stack */
    RUN(test_stack_push_pop);
    RUN(test_stack_lifo);
    RUN(test_stack_overflow);
    RUN(test_stack_underflow);
    RUN(test_frame_push_pop);
    RUN(test_frame_nested);
    RUN(test_stack_depth);

    /* Heap arena */
    RUN(test_heap_alloc);
    RUN(test_heap_alloc_no_overlap);
    RUN(test_heap_overflow);
    RUN(test_heap_alloc_zero);
    RUN(test_heap_reset);
    RUN(test_heap_load_store);
    RUN(test_heap_oob_load);
    RUN(test_heap_oob_store);

    /* Zone boundaries */
    RUN(test_heap_no_static_write);
    RUN(test_static_partial_overflow);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
