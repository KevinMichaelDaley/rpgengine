/**
 * @file aegis_yield_tests.c
 * @brief Tests for yield/resume/exit and fuel metering.
 *
 * Per ref/aegis_bytecode_spec.md §6.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/aegis/aegis_vm.h"

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
/* Test helper: make a VM with defaults                                      */
/* ----------------------------------------------------------------------- */

#define TEST_ARENA_SIZE 4096

static bool make_test_vm(aegis_vm_t *vm, aegis_bytecode_t *bc,
                         uint8_t *arena) {
    memset(arena, 0, TEST_ARENA_SIZE);
    aegis_bytecode_init(bc);
    bc->static_size = 64;

    aegis_config_t cfg = aegis_config_default();
    cfg.fuel_budget = 100;
    cfg.stack_max   = 256;

    return aegis_vm_init(vm, bc, &cfg, arena, TEST_ARENA_SIZE);
}

/* ======================================================================= */
/* Init tests                                                                */
/* ======================================================================= */

static bool test_init(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    ASSERT(make_test_vm(&vm, &bc, arena));
    ASSERT_INT_EQ(0, (int)vm.pc);
    ASSERT_INT_EQ(100, (int)vm.fuel);
    ASSERT(vm.alive);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)vm.status);
    return true;
}

static bool test_init_registers_zero(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    ASSERT(make_test_vm(&vm, &bc, arena));
    for (int i = 0; i < AEGIS_REGISTER_COUNT; i++) {
        ASSERT(vm.regs[i].u64 == 0);
    }
    return true;
}

/* ======================================================================= */
/* Fuel tests                                                                */
/* ======================================================================= */

static bool test_fuel_consume(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    ASSERT(aegis_vm_consume_fuel(&vm));
    ASSERT_INT_EQ(99, (int)vm.fuel);
    return true;
}

static bool test_fuel_exhaustion(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Consume all fuel. */
    for (int i = 0; i < 100; i++) {
        ASSERT(aegis_vm_consume_fuel(&vm));
    }
    ASSERT_INT_EQ(0, (int)vm.fuel);
    /* Next consume fails. */
    ASSERT(!aegis_vm_consume_fuel(&vm));
    return true;
}

static bool test_fuel_reset(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Consume some fuel. */
    for (int i = 0; i < 50; i++) {
        aegis_vm_consume_fuel(&vm);
    }
    ASSERT_INT_EQ(50, (int)vm.fuel);

    aegis_vm_reset_fuel(&vm);
    ASSERT_INT_EQ(100, (int)vm.fuel);
    return true;
}

/* ======================================================================= */
/* Yield tests                                                               */
/* ======================================================================= */

static bool test_explicit_yield(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Consume some fuel, then yield. */
    for (int i = 0; i < 30; i++) {
        aegis_vm_consume_fuel(&vm);
    }

    aegis_vm_status_t s = aegis_vm_yield(&vm);
    ASSERT_INT_EQ(AEGIS_VM_YIELDED, (int)s);
    /* Fuel should be reset. */
    ASSERT_INT_EQ(100, (int)vm.fuel);
    ASSERT(vm.alive);
    return true;
}

static bool test_yield_resets_heap(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Allocate some heap. */
    int32_t off = aegis_memory_alloc(&vm.memory, 128);
    ASSERT(off >= 0);

    aegis_vm_yield(&vm);

    /* After yield, heap should be reset (can allocate full heap again). */
    int32_t heap_size = (int32_t)TEST_ARENA_SIZE
                      - (int32_t)bc.static_size
                      - (int32_t)vm.config.stack_max;
    int32_t off2 = aegis_memory_alloc(&vm.memory, (uint32_t)heap_size);
    ASSERT(off2 >= 0);
    return true;
}

static bool test_yield_at_depth_error(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Push a call frame (simulating being inside a function). */
    ASSERT(aegis_memory_push_frame(&vm.memory, 42));

    /* Yield should fail because call depth > 0. */
    aegis_vm_status_t s = aegis_vm_yield(&vm);
    ASSERT_INT_EQ(AEGIS_VM_ERROR, (int)s);
    return true;
}

static bool test_yield_preserves_static(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Write to static array. */
    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.i32 = 999;
    ASSERT(aegis_memory_static_store(&vm.memory, 0, &val));

    aegis_vm_yield(&vm);

    /* Static should survive yield. */
    aegis_register_t out;
    ASSERT(aegis_memory_static_load(&vm.memory, 0, &out));
    ASSERT_INT_EQ(999, out.i32);
    return true;
}

/* ======================================================================= */
/* Force-yield tests                                                         */
/* ======================================================================= */

static bool test_force_yield(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Set some state. */
    vm.pc = 42;
    vm.regs[0].i32 = 777;

    aegis_vm_force_yield(&vm);

    ASSERT_INT_EQ(AEGIS_VM_FORCE_YIELDED, (int)vm.status);
    /* PC preserved. */
    ASSERT_INT_EQ(42, (int)vm.pc);
    /* Registers preserved. */
    ASSERT_INT_EQ(777, vm.regs[0].i32);
    /* Fuel reset. */
    ASSERT_INT_EQ(100, (int)vm.fuel);
    ASSERT(vm.alive);
    return true;
}

static bool test_force_yield_preserves_heap(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Allocate and write to heap. */
    int32_t off = aegis_memory_alloc(&vm.memory, 16);
    ASSERT(off >= 0);
    aegis_register_t val;
    memset(&val, 0, sizeof(val));
    val.i32 = 888;
    ASSERT(aegis_memory_heap_store(&vm.memory, (uint32_t)off, 0, &val));

    aegis_vm_force_yield(&vm);

    /* Heap data should still be there. */
    aegis_register_t out;
    ASSERT(aegis_memory_heap_load(&vm.memory, (uint32_t)off, 0, &out));
    ASSERT_INT_EQ(888, out.i32);
    return true;
}

static bool test_force_yield_preserves_stack(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Push a call frame. */
    ASSERT(aegis_memory_push_frame(&vm.memory, 10));
    ASSERT_INT_EQ(1, (int)aegis_memory_call_depth(&vm.memory));

    aegis_vm_force_yield(&vm);

    /* Stack preserved. */
    ASSERT_INT_EQ(1, (int)aegis_memory_call_depth(&vm.memory));
    uint32_t ret_pc;
    ASSERT(aegis_memory_pop_frame(&vm.memory, &ret_pc));
    ASSERT_INT_EQ(10, (int)ret_pc);
    return true;
}

/* ======================================================================= */
/* Wait-yield tests                                                          */
/* ======================================================================= */

static bool test_wait_yield(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    /* Consume some fuel. */
    for (int i = 0; i < 30; i++) {
        aegis_vm_consume_fuel(&vm);
    }
    vm.pc = 15;

    aegis_vm_wait_yield(&vm);

    ASSERT_INT_EQ(AEGIS_VM_WAIT_YIELDED, (int)vm.status);
    /* PC preserved (wait re-executes same instruction). */
    ASSERT_INT_EQ(15, (int)vm.pc);
    /* Fuel NOT reset on wait-yield. */
    ASSERT_INT_EQ(70, (int)vm.fuel);
    ASSERT(vm.alive);
    return true;
}

/* ======================================================================= */
/* Exit tests                                                                */
/* ======================================================================= */

static bool test_exit(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    aegis_vm_exit(&vm, 42);

    ASSERT_INT_EQ(AEGIS_VM_EXITED, (int)vm.status);
    ASSERT_INT_EQ(42, (int)vm.exit_code);
    ASSERT(!vm.alive);
    return true;
}

static bool test_exit_zero(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc;
    uint8_t arena[TEST_ARENA_SIZE];
    make_test_vm(&vm, &bc, arena);

    aegis_vm_exit(&vm, 0);
    ASSERT_INT_EQ(AEGIS_VM_EXITED, (int)vm.status);
    ASSERT_INT_EQ(0, (int)vm.exit_code);
    ASSERT(!vm.alive);
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    printf("=== Aegis Yield/Fuel Tests ===\n\n");

    RUN(test_init);
    RUN(test_init_registers_zero);

    RUN(test_fuel_consume);
    RUN(test_fuel_exhaustion);
    RUN(test_fuel_reset);

    RUN(test_explicit_yield);
    RUN(test_yield_resets_heap);
    RUN(test_yield_at_depth_error);
    RUN(test_yield_preserves_static);

    RUN(test_force_yield);
    RUN(test_force_yield_preserves_heap);
    RUN(test_force_yield_preserves_stack);

    RUN(test_wait_yield);

    RUN(test_exit);
    RUN(test_exit_zero);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
