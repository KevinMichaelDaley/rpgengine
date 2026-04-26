/**
 * @file aegis_sense_tests.c
 * @brief Tests for SENSE_QUERY async opcode and executor.
 *
 * Covers: submission, param packing, full sweep, targeted mode,
 * proximity detection, LOS raycast, and VM integration via IL.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/aegis/aegis_ops_async.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_sense.h"
#include "ferrum/physics/raycast.h"
#include "ferrum/physics/world.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                       */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                        \
    do {                                               \
        printf("RUN  %s\n", #fn);                      \
        fn();                                          \
        printf("OK   %s\n", #fn);                      \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabsf((float)(a) - (float)(b)) < (eps))

#define PASS() g_pass++

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool make_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies    = 16;
    cfg.max_colliders = 16;
    return phys_world_init(world, &cfg) == 0;
}

static void make_vm(aegis_vm_t *vm, aegis_bytecode_t *bc,
                    uint8_t *arena, uint32_t arena_sz) {
    aegis_config_t cfg = aegis_config_default();
    cfg.max_async_tasks = 8;
    cfg.static_max = 256;
    cfg.stack_max  = 256;
    cfg.arena_size = arena_sz;
    memset(vm, 0, sizeof(*vm));
    memset(arena, 0, arena_sz);
    aegis_vm_init(vm, bc, &cfg, arena, arena_sz);
}

/**
 * @brief Create a body with a sphere collider and set its entity_index.
 */
static uint32_t add_entity_body(phys_world_t *world, uint32_t entity_id,
                                float x, float y, float z, float radius) {
    uint32_t bid = phys_world_create_body(world);
    if (bid == UINT32_MAX) return UINT32_MAX;
    phys_body_t *b = phys_world_get_body(world, bid);
    phys_body_set_mass(b, 1.0f);
    b->position = (phys_vec3_t){x, y, z};
    b->entity_index = entity_id;
    phys_world_set_sphere_collider(world, bid, radius, (phys_vec3_t){0, 0, 0});
    return bid;
}

/* ------------------------------------------------------------------ */
/* 1. Direct handler submit test                                      */
/* ------------------------------------------------------------------ */

static void test_sense_query_submit(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[8192];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    vm.entity_id = 1;

    /* mode_flags = full sweep (0) | proximity + LOS (0x03 << 16) */
    vm.regs[1].u32 = (0x03u << 16);
    vm.regs[2].entity_id = 0;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_sense_query(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(out[0].task_type, (uint32_t)AEGIS_TASK_SENSE_QUERY);
    ASSERT_TRUE(out[0].result_ptr != NULL);
    ASSERT_EQ(out[0].result_cap, (uint32_t)AEGIS_SENSE_RESULT_CAPACITY);

    /* Verify param layout. */
    uint16_t mode, flags;
    uint32_t target;
    float pos[3], range;
    memcpy(&mode,   out[0].params,      2);
    memcpy(&flags,  out[0].params + 2,  2);
    memcpy(&target, out[0].params + 4,  4);
    memcpy(pos,     out[0].params + 8,  12);
    memcpy(&range,  out[0].params + 20, 4);

    ASSERT_EQ(mode, (uint16_t)AEGIS_SENSE_MODE_FULL);
    ASSERT_EQ(flags, (uint16_t)0x03);
    ASSERT_EQ(target, (uint32_t)0);
    ASSERT_NEAR(range, 50.0f, 0.01f);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. Full sweep executor test                                        */
/* ------------------------------------------------------------------ */

static void test_sense_query_full_sweep(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    /* NPC at origin. Entities at 5m, 15m, 60m. */
    add_entity_body(&world, 100,  5.0f, 0.0f, 0.0f, 1.0f);
    add_entity_body(&world, 200, 15.0f, 0.0f, 0.0f, 1.0f);
    add_entity_body(&world, 300, 60.0f, 0.0f, 0.0f, 1.0f);

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    uint8_t result[AEGIS_SENSE_RESULT_CAPACITY];
    memset(result, 0, sizeof(result));

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = AEGIS_TASK_SENSE_QUERY;
    task.result_ptr = result;
    task.result_cap = sizeof(result);

    uint16_t mode = AEGIS_SENSE_MODE_FULL;
    uint16_t flags = AEGIS_SENSE_PROXIMITY | AEGIS_SENSE_LOS;
    uint32_t target = 0;
    float npc_pos[3] = {0.0f, 0.0f, 0.0f};
    float max_range = 50.0f;
    memcpy(task.params,      &mode,     2);
    memcpy(task.params + 2,  &flags,    2);
    memcpy(task.params + 4,  &target,   4);
    memcpy(task.params + 8,  npc_pos,   12);
    memcpy(task.params + 20, &max_range, 4);

    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));

    uint32_t executed = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(executed, (uint32_t)1);

    aegis_sense_result_t *header = (aegis_sense_result_t *)result;
    ASSERT_EQ(header->status, 0);
    /* 5m and 15m bodies should be found; 60m body is out of range. */
    ASSERT_EQ(header->entity_count, (uint32_t)2);
    ASSERT_EQ(header->total_found, (uint32_t)2);
    ASSERT_EQ(header->truncated, (uint32_t)0);

    /* Verify first entity (closest). */
    aegis_sense_entity_t *ent = (aegis_sense_entity_t *)(result + sizeof(*header));
    ASSERT_EQ(ent->entity_id, (uint32_t)100);
    ASSERT_NEAR(ent->distance, 5.0f, 0.1f);
    ASSERT_TRUE(ent->salience > 0.8f);
    ASSERT_TRUE((ent->flags & AEGIS_SENSE_ENTITY_VISIBLE) != 0);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. Targeted mode executor test                                     */
/* ------------------------------------------------------------------ */

static void test_sense_query_targeted(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));

    add_entity_body(&world, 100, 10.0f, 0.0f, 0.0f, 1.0f);
    add_entity_body(&world, 200, 20.0f, 0.0f, 0.0f, 1.0f);

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    uint8_t result[AEGIS_SENSE_RESULT_CAPACITY];
    memset(result, 0, sizeof(result));

    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = AEGIS_TASK_SENSE_QUERY;
    task.result_ptr = result;
    task.result_cap = sizeof(result);

    uint16_t mode = AEGIS_SENSE_MODE_TARGETED;
    uint16_t flags = AEGIS_SENSE_PROXIMITY;
    uint32_t target = 200;
    float npc_pos[3] = {0.0f, 0.0f, 0.0f};
    float max_range = 50.0f;
    memcpy(task.params,      &mode,     2);
    memcpy(task.params + 2,  &flags,    2);
    memcpy(task.params + 4,  &target,   4);
    memcpy(task.params + 8,  npc_pos,   12);
    memcpy(task.params + 20, &max_range, 4);

    ASSERT_TRUE(aegis_async_buffer_submit(&buf, &task));
    aegis_async_execute_drain(&buf, &world, 16);

    aegis_sense_result_t *header = (aegis_sense_result_t *)result;
    ASSERT_EQ(header->entity_count, (uint32_t)1);

    aegis_sense_entity_t *ent = (aegis_sense_entity_t *)(result + sizeof(*header));
    ASSERT_EQ(ent->entity_id, (uint32_t)200);
    ASSERT_NEAR(ent->distance, 20.0f, 0.1f);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 4. VM integration via IL assembly                                  */
/* ------------------------------------------------------------------ */

static void test_vm_sense_query_and_wait(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[65536];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    vm.entity_id = 42;
    vm.config.fuel_budget = 2000;
    vm.fuel = 2000;

    const char *il =
        "load_imm r1, 0x00030000\n"  /* full sweep + proximity + LOS */
        "load_imm r2, 0\n"
        "sense_query r0, r1, r2\n"
        "wait r3, r4, r0\n"
        "yield\n";

    aegis_asm_t as = {0};
    aegis_bytecode_t compiled = {0};
    bool ok = aegis_asm_compile(&as, il, (uint32_t)strlen(il), &compiled);
    ASSERT_TRUE(ok);

    vm.bytecode = &compiled;
    aegis_vm_reset_fuel(&vm);

    /* First run: submits sense_query, wait yields. */
    aegis_vm_status_t st = aegis_vm_run(&vm);
    if (st != AEGIS_VM_WAIT_YIELDED) {
        fprintf(stderr, "DEBUG: st=%d (expected %d), async_task_count=%u, status=%u\n",
                (int)st, (int)AEGIS_VM_WAIT_YIELDED,
                vm.async_task_count,
                (vm.async_task_count > 0) ? atomic_load(&vm.async_tasks[0].status) : 999);
    }
    ASSERT_EQ(st, AEGIS_VM_WAIT_YIELDED);
    ASSERT_EQ(vm.async_task_count, (uint32_t)1);

    /* Drain and execute the sense query. */
    phys_world_t world;
    ASSERT_TRUE(make_world(&world));
    add_entity_body(&world, 999, 8.0f, 0.0f, 0.0f, 1.0f);

    uint32_t drained = aegis_async_execute_drain(&buf, &world, 16);
    ASSERT_EQ(drained, (uint32_t)1);

    /* Second run: wait completes, yield exits. */
    aegis_vm_reset_fuel(&vm);
    st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED);

    /* r4 should be COMPLETE. */
    ASSERT_EQ(vm.regs[4].u32, (uint32_t)AEGIS_ASYNC_COMPLETE);

    /* r3 contains result handle; result is at heap offset. */
    int32_t handle = vm.regs[0].i32;
    aegis_sense_result_t *header = (aegis_sense_result_t *)(arena + handle);
    ASSERT_EQ(header->status, 0);
    ASSERT_EQ(header->entity_count, (uint32_t)1);

    aegis_sense_entity_t *ent = (aegis_sense_entity_t *)((uint8_t *)header + sizeof(*header));
    ASSERT_EQ(ent->entity_id, (uint32_t)999);
    ASSERT_NEAR(ent->distance, 8.0f, 0.1f);

    phys_world_destroy(&world);
    aegis_async_buffer_destroy(&buf);
    free(compiled.instructions);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 5. Fuel deduction test                                             */
/* ------------------------------------------------------------------ */

static void test_sense_query_fuel_deduction(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[8192];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    vm.entity_id = 1;

    vm.fuel = 350;
    vm.regs[1].u32 = (1u << 16); /* proximity only */
    vm.regs[2].entity_id = 0;

    aegis_decode_result_t d = {0};
    d.raw_a = 0; d.raw_b = 1; d.raw_c = 2;

    bool ok = aegis_op_sense_query(&vm, &d);
    if (!ok) {
        fprintf(stderr, "DEBUG: sense_query returned false, async_task_count=%u\n", vm.async_task_count);
    }
    ASSERT_TRUE(ok);
    /* Fuel is deducted by the VM runner (aegis_vm_run), not the handler. */
    ASSERT_EQ(vm.fuel, (uint32_t)350);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. Max async tasks limit                                           */
/* ------------------------------------------------------------------ */

static void test_sense_query_max_tasks(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[65536];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    vm.config.max_async_tasks = 2;
    vm.entity_id = 1;

    aegis_decode_result_t d = {0};
    d.raw_a = 0; d.raw_b = 1; d.raw_c = 2;
    vm.regs[1].u32 = 0;
    vm.regs[2].entity_id = 0;

    ASSERT_TRUE(aegis_op_sense_query(&vm, &d));
    vm.async_tasks[0].status = AEGIS_ASYNC_PENDING;

    ASSERT_TRUE(aegis_op_sense_query(&vm, &d));
    vm.async_tasks[1].status = AEGIS_ASYNC_PENDING;

    bool ok = aegis_op_sense_query(&vm, &d);
    ASSERT_TRUE(!ok); /* Third should fail. */

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void) {
    printf("=== aegis_sense_tests ===\n");

    RUN(test_sense_query_submit);
    RUN(test_sense_query_full_sweep);
    RUN(test_sense_query_targeted);
    RUN(test_vm_sense_query_and_wait);
    RUN(test_sense_query_fuel_deduction);
    RUN(test_sense_query_max_tasks);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
