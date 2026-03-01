/**
 * @file aegis_ops_async_tests.c
 * @brief Tests for async bytecode instructions: vis_test, nav_query, poll, wait.
 *
 * Covers submission, poll (pending/complete/error), wait (yield + resume),
 * heap survival across wait-yield, max_async_tasks enforcement, and
 * VM integration via IL assembly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_ops_async.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_decode.h"

/* ------------------------------------------------------------------ */
/* Minimal test harness                                               */
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

#define PASS() g_pass++

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

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

/* ================================================================== */
/* Direct handler tests                                               */
/* ================================================================== */

/* --- vis_test ----------------------------------------------------- */

static void test_vis_test_submit(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Set origin in r1 and ray_vec in r2. */
    vm.regs[1].vec3[0] = 10.0f;
    vm.regs[1].vec3[1] = 20.0f;
    vm.regs[1].vec3[2] = 30.0f;
    vm.regs[2].vec3[0] = 0.0f;
    vm.regs[2].vec3[1] = -1.0f;
    vm.regs[2].vec3[2] = 0.0f;

    /* Instruction: vis_test r0, r1, r2. */
    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_vis_test(&vm, &d);
    ASSERT_TRUE(ok);

    /* r0 should contain a valid handle (>= 0). */
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    /* Drain and verify the submitted task. */
    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(out[0].task_type, (uint32_t)AEGIS_TASK_VIS_TEST);
    ASSERT_TRUE(out[0].result_ptr != NULL);

    /* Params should contain origin and ray_vec. */
    float origin[3], ray[3];
    memcpy(origin, out[0].params, sizeof(origin));
    memcpy(ray, out[0].params + sizeof(origin), sizeof(ray));
    ASSERT_TRUE(origin[0] == 10.0f && origin[1] == 20.0f && origin[2] == 30.0f);
    ASSERT_TRUE(ray[0] == 0.0f && ray[1] == -1.0f && ray[2] == 0.0f);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- nav_query ---------------------------------------------------- */

static void test_nav_query_submit(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* From in r1, to in r2. */
    vm.regs[1].vec3[0] = 1.0f;
    vm.regs[1].vec3[1] = 2.0f;
    vm.regs[1].vec3[2] = 3.0f;
    vm.regs[2].vec3[0] = 10.0f;
    vm.regs[2].vec3[1] = 20.0f;
    vm.regs[2].vec3[2] = 30.0f;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_nav_query(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(out[0].task_type, (uint32_t)AEGIS_TASK_NAV_QUERY);

    float from[3], to[3];
    memcpy(from, out[0].params, sizeof(from));
    memcpy(to, out[0].params + sizeof(from), sizeof(to));
    ASSERT_TRUE(from[0] == 1.0f && to[0] == 10.0f);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- poll (pending) ----------------------------------------------- */

static void test_poll_pending(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Submit a task first to get a handle. */
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    aegis_decode_result_t d_vis = {0};
    d_vis.raw_a = 3; d_vis.raw_b = 1; d_vis.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d_vis));
    int32_t handle = vm.regs[3].i32;

    /* Now poll: r0=result, r1=flag, r2=handle. */
    vm.regs[5].i32 = handle;
    aegis_decode_result_t d = {0};
    d.raw_a = 10; /* result */
    d.raw_b = 11; /* flag */
    d.raw_c = 5;  /* handle */

    bool ok = aegis_op_poll(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_PENDING);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- poll (complete) ---------------------------------------------- */

static void test_poll_complete(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Submit a task. */
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    aegis_decode_result_t d_vis = {0};
    d_vis.raw_a = 3; d_vis.raw_b = 1; d_vis.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d_vis));
    int32_t handle = vm.regs[3].i32;

    /* Simulate world completing the task: write result to result_ptr. */
    /* The handle is a heap offset; result_ptr = arena base + handle. */
    float hit_pos[3] = {5.0f, 10.0f, 15.0f};
    void *result_ptr = vm.memory.base + handle;
    memcpy(result_ptr, hit_pos, sizeof(hit_pos));

    /* Set status to COMPLETE. We need access to the task's status.
     * The task stores its status in the first 4 bytes of the result slot
     * metadata. Actually, the async_tasks array in VM tracks this. */
    vm.async_tasks[0].status = AEGIS_ASYNC_COMPLETE;

    /* Poll. */
    vm.regs[5].i32 = handle;
    aegis_decode_result_t d = {0};
    d.raw_a = 10; /* result */
    d.raw_b = 11; /* flag */
    d.raw_c = 5;  /* handle */

    bool ok = aegis_op_poll(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_COMPLETE);

    /* Result register should contain the hit position. */
    ASSERT_TRUE(vm.regs[10].vec3[0] == 5.0f);
    ASSERT_TRUE(vm.regs[10].vec3[1] == 10.0f);
    ASSERT_TRUE(vm.regs[10].vec3[2] == 15.0f);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- poll (error) ------------------------------------------------- */

static void test_poll_error(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Submit a task. */
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    aegis_decode_result_t d_vis = {0};
    d_vis.raw_a = 3; d_vis.raw_b = 1; d_vis.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d_vis));

    /* Simulate error. */
    vm.async_tasks[0].status = AEGIS_ASYNC_ERROR;

    vm.regs[5].i32 = vm.regs[3].i32;
    aegis_decode_result_t d = {0};
    d.raw_a = 10; d.raw_b = 11; d.raw_c = 5;

    bool ok = aegis_op_poll(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_ERROR);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- wait (pending → wait-yield) ---------------------------------- */

static void test_wait_pending_yields(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Submit a task. */
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    aegis_decode_result_t d_vis = {0};
    d_vis.raw_a = 3; d_vis.raw_b = 1; d_vis.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d_vis));

    /* Wait should return false (signals wait-yield to interpreter). */
    vm.regs[5].i32 = vm.regs[3].i32;
    aegis_decode_result_t d = {0};
    d.raw_a = 10; d.raw_b = 11; d.raw_c = 5;

    bool ok = aegis_op_wait(&vm, &d);
    ASSERT_TRUE(!ok); /* false = should wait-yield */
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_PENDING);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- wait (complete → advances) ----------------------------------- */

static void test_wait_complete_advances(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Submit a task. */
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    aegis_decode_result_t d_vis = {0};
    d_vis.raw_a = 3; d_vis.raw_b = 1; d_vis.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d_vis));

    /* Simulate completion. */
    float hit[3] = {1.0f, 2.0f, 3.0f};
    void *rp = vm.memory.base + vm.regs[3].i32;
    memcpy(rp, hit, sizeof(hit));
    vm.async_tasks[0].status = AEGIS_ASYNC_COMPLETE;

    vm.regs[5].i32 = vm.regs[3].i32;
    aegis_decode_result_t d = {0};
    d.raw_a = 10; d.raw_b = 11; d.raw_c = 5;

    bool ok = aegis_op_wait(&vm, &d);
    ASSERT_TRUE(ok); /* true = complete, advance PC normally */
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_COMPLETE);
    ASSERT_TRUE(vm.regs[10].vec3[0] == 1.0f);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- max_async_tasks limit ---------------------------------------- */

static void test_max_async_tasks_exceeded(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 32));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[8192];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    /* config.max_async_tasks = 8 */

    aegis_decode_result_t d = {0};
    d.raw_a = 0; d.raw_b = 1; d.raw_c = 2;
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;

    /* Submit max_async_tasks tasks. */
    for (uint32_t i = 0; i < 8; i++) {
        d.raw_a = (uint8_t)(i + 10);
        bool ok = aegis_op_vis_test(&vm, &d);
        ASSERT_TRUE(ok);
    }

    /* The 9th should fail. */
    d.raw_a = 20;
    bool ok = aegis_op_vis_test(&vm, &d);
    ASSERT_TRUE(!ok); /* limit exceeded → error */

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- heap survives wait-yield ------------------------------------- */

static void test_heap_survives_wait_yield(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Submit a task — this allocates a result slot in the heap. */
    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    aegis_decode_result_t d_vis = {0};
    d_vis.raw_a = 3; d_vis.raw_b = 1; d_vis.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d_vis));
    int32_t handle = vm.regs[3].i32;

    /* Record the heap bump before wait-yield. */
    uint32_t heap_before = vm.memory.heap_bump;

    /* Simulate a wait-yield: heap should NOT be reset. */
    aegis_vm_wait_yield(&vm);
    ASSERT_EQ(vm.status, AEGIS_VM_WAIT_YIELDED);

    /* Heap bump should be unchanged (heap not reset). */
    ASSERT_EQ(vm.memory.heap_bump, heap_before);

    /* The result slot should still be accessible. */
    void *slot = vm.memory.base + handle;
    ASSERT_TRUE(slot != NULL);
    (void)slot;

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- multiple async tasks ----------------------------------------- */

static void test_multiple_async_tasks(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;
    vm.regs[4].vec3[0] = 5; vm.regs[4].vec3[1] = 5; vm.regs[4].vec3[2] = 5;

    /* Submit vis_test and nav_query. */
    aegis_decode_result_t d1 = {0};
    d1.raw_a = 10; d1.raw_b = 1; d1.raw_c = 2;
    ASSERT_TRUE(aegis_op_vis_test(&vm, &d1));
    int32_t h1 = vm.regs[10].i32;

    aegis_decode_result_t d2 = {0};
    d2.raw_a = 11; d2.raw_b = 1; d2.raw_c = 4;
    ASSERT_TRUE(aegis_op_nav_query(&vm, &d2));
    int32_t h2 = vm.regs[11].i32;

    /* Handles should be different. */
    ASSERT_TRUE(h1 != h2);

    /* Should have 2 tasks in buffer. */
    ASSERT_EQ(vm.async_task_count, (uint32_t)2);

    /* Complete the first, poll both. */
    vm.async_tasks[0].status = AEGIS_ASYNC_COMPLETE;

    vm.regs[20].i32 = h1;
    aegis_decode_result_t dp1 = {0};
    dp1.raw_a = 30; dp1.raw_b = 31; dp1.raw_c = 20;
    ASSERT_TRUE(aegis_op_poll(&vm, &dp1));
    ASSERT_EQ(vm.regs[31].u32, (uint32_t)AEGIS_ASYNC_COMPLETE);

    vm.regs[21].i32 = h2;
    aegis_decode_result_t dp2 = {0};
    dp2.raw_a = 32; dp2.raw_b = 33; dp2.raw_c = 21;
    ASSERT_TRUE(aegis_op_poll(&vm, &dp2));
    ASSERT_EQ(vm.regs[33].u32, (uint32_t)AEGIS_ASYNC_PENDING);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* --- poll with invalid handle ------------------------------------- */

static void test_poll_invalid_handle(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    vm.regs[5].i32 = 9999; /* bogus handle */
    aegis_decode_result_t d = {0};
    d.raw_a = 10; d.raw_b = 11; d.raw_c = 5;

    bool ok = aegis_op_poll(&vm, &d);
    ASSERT_TRUE(!ok); /* error — invalid handle */

    PASS();
}

/* ================================================================== */
/* VM integration tests (via IL assembly)                             */
/* ================================================================== */

/** Compile IL source into bytecode. */
static bool compile_il(const char *src, aegis_bytecode_t *bc) {
    aegis_asm_t as;
    memset(&as, 0, sizeof(as));
    uint32_t len = (uint32_t)strlen(src);
    return aegis_asm_compile(&as, src, len, bc);
}

static void test_vm_vis_test_and_poll(void) {
    const char *il =
        "resume\n"
        "vis_test r0, r1, r2\n"
        "poll r10, r11, r0\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    vm.regs[1].vec3[0] = 1.0f;
    vm.regs[1].vec3[1] = 2.0f;
    vm.regs[1].vec3[2] = 3.0f;
    vm.regs[2].vec3[0] = 0.0f;
    vm.regs[2].vec3[1] = -1.0f;
    vm.regs[2].vec3[2] = 0.0f;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED);

    /* r0 should have a handle, r11 should be PENDING. */
    ASSERT_TRUE(vm.regs[0].i32 >= 0);
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_PENDING);

    free(bc.instructions);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_vm_wait_yields_fiber(void) {
    const char *il =
        "resume\n"
        "vis_test r0, r1, r2\n"
        "wait r10, r11, r0\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    vm.regs[1].vec3[0] = 0; vm.regs[1].vec3[1] = 0; vm.regs[1].vec3[2] = 0;
    vm.regs[2].vec3[0] = 1; vm.regs[2].vec3[1] = 0; vm.regs[2].vec3[2] = 0;

    /* First run: vis_test submits, wait sees PENDING → wait-yield. */
    aegis_vm_status_t st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_WAIT_YIELDED);

    /* PC should NOT have advanced past the wait instruction. */
    ASSERT_EQ(vm.pc, (uint32_t)2); /* resume=0, vis_test=1, wait=2 */

    /* Simulate world completing the task. */
    vm.async_tasks[0].status = AEGIS_ASYNC_COMPLETE;
    float hit[3] = {7.0f, 8.0f, 9.0f};
    void *rp = vm.memory.base + vm.regs[0].i32;
    memcpy(rp, hit, sizeof(hit));

    /* Resume: wait should now see COMPLETE and advance. */
    aegis_vm_reset_fuel(&vm);
    vm.alive = true;
    vm.status = AEGIS_VM_YIELDED;
    st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED); /* reaches yield at end */

    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_COMPLETE);
    ASSERT_TRUE(vm.regs[10].vec3[0] == 7.0f);

    free(bc.instructions);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_vm_nav_query_integration(void) {
    const char *il =
        "resume\n"
        "nav_query r0, r1, r2\n"
        "poll r10, r11, r0\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    vm.regs[1].vec3[0] = 1; vm.regs[1].vec3[1] = 2; vm.regs[1].vec3[2] = 3;
    vm.regs[2].vec3[0] = 10; vm.regs[2].vec3[1] = 20; vm.regs[2].vec3[2] = 30;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_PENDING);

    /* Verify task in buffer. */
    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(out[0].task_type, (uint32_t)AEGIS_TASK_NAV_QUERY);

    free(bc.instructions);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void) {
    printf("=== Aegis Async Ops Tests ===\n\n");

    /* Direct handler tests */
    RUN(test_vis_test_submit);
    RUN(test_nav_query_submit);
    RUN(test_poll_pending);
    RUN(test_poll_complete);
    RUN(test_poll_error);
    RUN(test_wait_pending_yields);
    RUN(test_wait_complete_advances);
    RUN(test_max_async_tasks_exceeded);
    RUN(test_heap_survives_wait_yield);
    RUN(test_multiple_async_tasks);
    RUN(test_poll_invalid_handle);

    /* VM integration tests */
    RUN(test_vm_vis_test_and_poll);
    RUN(test_vm_wait_yields_fiber);
    RUN(test_vm_nav_query_integration);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
