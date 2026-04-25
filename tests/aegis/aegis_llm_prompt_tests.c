/**
 * @file aegis_llm_prompt_tests.c
 * @brief Tests for AEGIS LLM prompt opcode and VM integration.
 *
 * Phase 1 (RED): Tests compile but fail until implementation lands.
 * Covers: opcode submit, poll/wait, fuel deduction, max_async_tasks,
 * VM integration via bytecode, and result layout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_ops_llm.h"
#include "ferrum/aegis/aegis_llm.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_decode.h"

/* ── Test harness ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                                        \
    do {                                                                 \
        printf("RUN  %s\n", #fn);                                       \
        fn();                                                            \
        printf("OK   %s\n", #fn);                                       \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                                 \
        if (!(expr)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);       \
            g_fail++;                                                    \
            return;                                                      \
        }                                                                \
    } while (0)

#define ASSERT_EQ(a, b)   ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))

#define PASS() g_pass++

/* ── Helpers ───────────────────────────────────────────────────── */

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

/* ================================================================= */
/* Direct handler tests                                              */
/* ================================================================= */

static void test_llm_prompt_submit(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Write prompt into heap at offset 64. */
    const char *prompt = "What is the weather?";
    size_t plen = strlen(prompt) + 1;
    memcpy(arena + 64, prompt, plen);

    /* Set registers: r1 = prompt offset, r2 = max_tokens. */
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 128;

    /* Instruction: llm_prompt r0, r1, r2. */
    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_llm_prompt(&vm, &d);
    ASSERT_TRUE(ok);

    /* r0 should contain a valid handle (>= 0). */
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    /* Drain and verify the submitted task. */
    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);
    ASSERT_EQ(out[0].task_type, (uint32_t)AEGIS_TASK_LLM_PROMPT);
    ASSERT_TRUE(out[0].result_ptr != NULL);

    /* Params should contain prompt offset and max_tokens. */
    int32_t prompt_off;
    int32_t max_tok;
    memcpy(&prompt_off, out[0].params, sizeof(prompt_off));
    memcpy(&max_tok, out[0].params + sizeof(prompt_off), sizeof(max_tok));
    ASSERT_EQ(prompt_off, 64);
    ASSERT_EQ(max_tok, 128);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_llm_prompt_max_tokens_clamped(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    memcpy(arena + 64, "Hello", 6);
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 99999; /* Excessive max_tokens. */

    aegis_decode_result_t d = {0};
    d.raw_a = 0; d.raw_b = 1; d.raw_c = 2;

    bool ok = aegis_op_llm_prompt(&vm, &d);
    ASSERT_TRUE(ok);

    aegis_async_task_t out[1];
    uint32_t n = aegis_async_buffer_drain(&buf, out, 1);
    ASSERT_EQ(n, (uint32_t)1);

    int32_t max_tok;
    memcpy(&max_tok, out[0].params + sizeof(int32_t), sizeof(max_tok));
    /* Should be clamped to engine default (e.g., 4096). */
    ASSERT_TRUE(max_tok <= 4096);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_llm_prompt_exceeds_max_async(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 32));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[8192];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    /* config.max_async_tasks = 8 */

    memcpy(arena + 64, "x", 2);
    aegis_decode_result_t d = {0};
    d.raw_a = 0; d.raw_b = 1; d.raw_c = 2;
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 100;

    for (uint32_t i = 0; i < 8; i++) {
        d.raw_a = (uint8_t)(i + 10);
        ASSERT_TRUE(aegis_op_llm_prompt(&vm, &d));
    }

    /* The 9th should fail. */
    d.raw_a = 20;
    bool ok = aegis_op_llm_prompt(&vm, &d);
    ASSERT_TRUE(!ok);

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ================================================================= */
/* VM integration tests (bytecode)                                   */
/* ================================================================= */

static bool compile_il(const char *src, aegis_bytecode_t *bc) {
    aegis_asm_t as;
    memset(&as, 0, sizeof(as));
    uint32_t len = (uint32_t)strlen(src);
    return aegis_asm_compile(&as, src, len, bc);
}

static void test_vm_llm_prompt_and_poll(void) {
    const char *il =
        "resume\n"
        "llm_prompt r0, r1, r2\n"
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

    memcpy(arena + 64, "Tell me a joke.", 16);
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 256;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED);

    /* r0 should have a handle, r11 should be PENDING. */
    ASSERT_TRUE(vm.regs[0].i32 >= 0);
    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_PENDING);

    free(bc.instructions);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_vm_llm_prompt_wait_yields(void) {
    const char *il =
        "resume\n"
        "llm_prompt r0, r1, r2\n"
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

    memcpy(arena + 64, "Explain recursion.", 19);
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 128;

    /* First run: llm_prompt submits, wait sees PENDING → wait-yield. */
    aegis_vm_status_t st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_WAIT_YIELDED);

    /* PC should NOT have advanced past the wait instruction. */
    ASSERT_EQ(vm.pc, (uint32_t)2); /* resume=0, llm_prompt=1, wait=2 */

    /* Simulate executor completing the task. */
    vm.async_tasks[0].status = AEGIS_ASYNC_COMPLETE;
    /* Write a mock result into the result slot. */
    aegis_llm_result_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.status = AEGIS_LLM_OK;
    mock.input_tokens = 10;
    mock.output_tokens = 20;
    mock.cost_usd = 0.001f;
    mock.total_cost_usd = 0.001f;
    mock.response_len = 5;
    mock.tool_call_count = 0;
    void *rp = vm.memory.base + vm.regs[0].i32;
    memcpy(rp, &mock, sizeof(mock));
    memcpy((char *)rp + sizeof(mock), "hello", 6);

    /* Resume: wait should now see COMPLETE and advance. */
    aegis_vm_reset_fuel(&vm);
    vm.alive = true;
    vm.status = AEGIS_VM_YIELDED;
    st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED); /* reaches yield at end */

    ASSERT_EQ(vm.regs[11].u32, (uint32_t)AEGIS_ASYNC_COMPLETE);

    free(bc.instructions);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_vm_llm_prompt_fuel_deduction(void) {
    const char *il =
        "resume\n"
        "llm_prompt r0, r1, r2\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;
    /* Fuel budget 600: enough for llm_prompt (burns 500) + loop overhead. */
    vm.config.fuel_budget = 600;
    vm.fuel = 600;

    memcpy(arena + 64, "Hi", 3);
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 64;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    /* Should yield normally, not force-yield from fuel exhaustion. */
    ASSERT_EQ(st, AEGIS_VM_YIELDED);
    /* PC should have advanced past llm_prompt to the yield. */
    ASSERT_EQ(vm.pc, (uint32_t)3);

    free(bc.instructions);
    aegis_async_buffer_destroy(&buf);
    PASS();
}

static void test_llm_prompt_empty_prompt_fails(void) {
    aegis_async_buffer_t buf;
    ASSERT_TRUE(aegis_async_buffer_init(&buf, 16));

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.async_buffer = &buf;

    /* Empty string at offset 64. */
    arena[64] = '\0';
    vm.regs[1].i32 = 64;
    vm.regs[2].i32 = 128;

    aegis_decode_result_t d = {0};
    d.raw_a = 0; d.raw_b = 1; d.raw_c = 2;

    bool ok = aegis_op_llm_prompt(&vm, &d);
    ASSERT_TRUE(!ok); /* Empty prompt is an error. */

    aegis_async_buffer_destroy(&buf);
    PASS();
}

/* ================================================================= */
/* Main                                                              */
/* ================================================================= */

int main(void) {
    printf("=== Aegis LLM Prompt Tests ===\n\n");

    RUN(test_llm_prompt_submit);
    RUN(test_llm_prompt_max_tokens_clamped);
    RUN(test_llm_prompt_exceeds_max_async);
    RUN(test_vm_llm_prompt_and_poll);
    RUN(test_vm_llm_prompt_wait_yields);
    RUN(test_vm_llm_prompt_fuel_deduction);
    RUN(test_llm_prompt_empty_prompt_fails);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
