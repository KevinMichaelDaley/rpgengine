/**
 * @file aegis_tools_tests.c
 * @brief Tests for the VM tool_action opcode: whitelist, dispatch, fuel, args.
 *
 * Covers:
 * - tool_action opcode compilation from IL
 * - Unknown tool_id rejection (whitelist)
 * - Known tool_id dispatch to stub functions
 * - Fuel consumption (50 per call)
 * - Empty args {} and missing optional fields
 * - Integration: AEGIS script -> tool_action -> verify result register
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_tools.h"
#include "ferrum/aegis/aegis_ops_tools.h"
#include "ferrum/npc/npc_knowledge_graph.h"

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
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((a) == (b))

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

static bool compile_il(const char *src, aegis_bytecode_t *bc) {
    aegis_asm_t as;
    memset(&as, 0, sizeof(as));
    uint32_t len = (uint32_t)strlen(src);
    return aegis_asm_compile(&as, src, len, bc);
}

/* Write a null-terminated JSON string to the heap arena at offset. */
static void write_json(aegis_vm_t *vm, int32_t offset, const char *json) {
    uint8_t *dst = vm->memory.base + offset;
    size_t len = strlen(json);
    memcpy(dst, json, len + 1);
}

/* ================================================================== */
/* Tests                                                              */
/* ================================================================== */

/* --- Direct handler: unknown tool_id ------------------------------ */

static void test_tool_action_unknown_tool(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    /* Set tool_id = 99 (unknown) in r1, args handle = 0 in r2. */
    vm.regs[1].i32 = 99;
    vm.regs[2].i32 = 0;

    aegis_decode_result_t d = {0};
    d.raw_a = 0; /* result reg */
    d.raw_b = 1; /* tool_id reg */
    d.raw_c = 2; /* args handle reg */

    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(!ok); /* false = error */
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    int32_t *status_ptr = (int32_t *)(vm.memory.base + vm.regs[0].i32);
    ASSERT_EQ(*status_ptr, (int32_t)AEGIS_TOOL_UNKNOWN);

    PASS();
}

/* --- Direct handler: combat tools return error status ------------- */

static void test_tool_action_combat_error(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    int32_t args_off = 256;
    write_json(&vm, args_off, "{}");

    /* ATTACK = 6 */
    vm.regs[1].i32 = AEGIS_TOOL_ATTACK;
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    int32_t *status_ptr = (int32_t *)(vm.memory.base + vm.regs[0].i32);
    ASSERT_EQ(*status_ptr, (int32_t)AEGIS_TOOL_UNKNOWN);

    const char *result = (const char *)(vm.memory.base + vm.regs[0].i32 + sizeof(int32_t));
    ASSERT_TRUE(strstr(result, "Combat system not available") != NULL);

    PASS();
}

/* --- Direct handler: TRADE_SELL with item argument ---------------- */

static void test_tool_action_trade_sell_with_item(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    int32_t args_off = 256;
    write_json(&vm, args_off, "{\"item\":\"iron_axe\"}");

    vm.regs[1].i32 = AEGIS_TOOL_TRADE_SELL;
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    const char *result = (const char *)(vm.memory.base + vm.regs[0].i32 + sizeof(int32_t));
    ASSERT_TRUE(strstr(result, "Broadcasting") != NULL);

    PASS();
}

/* --- Direct handler: DEFEND with target argument ------------------ */

static void test_tool_action_defend_with_target(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    int32_t args_off = 256;
    write_json(&vm, args_off, "{\"target\":\"player_42\"}");

    vm.regs[1].i32 = AEGIS_TOOL_DEFEND;
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    int32_t *status_ptr = (int32_t *)(vm.memory.base + vm.regs[0].i32);
    ASSERT_EQ(*status_ptr, (int32_t)AEGIS_TOOL_UNKNOWN);

    const char *result = (const char *)(vm.memory.base + vm.regs[0].i32 + sizeof(int32_t));
    ASSERT_TRUE(strstr(result, "Combat system not available") != NULL);

    PASS();
}

/* --- Direct handler: GOTO dispatches to real nav handler --------- */

static void test_tool_action_goto_dispatches(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    aegis_async_buffer_t async_buf;
    ASSERT_TRUE(aegis_async_buffer_init(&async_buf, 4));
    vm.async_buffer = &async_buf;

    int32_t args_off = 256;
    write_json(&vm, args_off, "{\"target\":\"orc_camp\"}");

    vm.regs[1].i32 = AEGIS_TOOL_GOTO;
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    int32_t *status_ptr = (int32_t *)(vm.memory.base + vm.regs[0].i32);
    ASSERT_EQ(*status_ptr, (int32_t)AEGIS_TOOL_NAV);

    const char *result = (const char *)(vm.memory.base + vm.regs[0].i32 + sizeof(int32_t));
    ASSERT_TRUE(strstr(result, "GOTO failed: nav system not available") != NULL);

    aegis_async_buffer_destroy(&async_buf);

    PASS();
}

/* --- Fuel consumption --------------------------------------------- */

static void test_tool_action_fuel_consumption(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    vm.fuel = 100;

    int32_t args_off = 256;
    write_json(&vm, args_off, "{}");

    vm.regs[1].i32 = AEGIS_TOOL_TRADE_INIT;
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    /* Fuel should be reduced by 50. */
    ASSERT_EQ(vm.fuel, (uint32_t)50);

    PASS();
}

/* --- Fuel exhaustion on repeated calls ---------------------------- */

static void test_tool_action_fuel_exhaustion(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    vm.fuel = 75;

    int32_t args_off = 256;
    write_json(&vm, args_off, "{}");

    vm.regs[1].i32 = AEGIS_TOOL_TRADE_INIT;
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    /* First call: fuel 75 -> 25. */
    bool ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_EQ(vm.fuel, (uint32_t)25);

    /* Second call: fuel 25 -> 0 (clamped). */
    ok = aegis_op_tool_action(&vm, &d);
    ASSERT_TRUE(ok);
    ASSERT_EQ(vm.fuel, (uint32_t)0);

    PASS();
}

/* --- All 10 tool IDs are whitelisted ------------------------------ */

static void test_tool_action_all_whitelisted(void) {
    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    int32_t args_off = 256;
    write_json(&vm, args_off, "{}");
    vm.regs[2].i32 = args_off;

    aegis_decode_result_t d = {0};
    d.raw_a = 0;
    d.raw_b = 1;
    d.raw_c = 2;

    for (int id = 0; id <= 9; id++) {
        vm.regs[1].i32 = id;
        bool ok = aegis_op_tool_action(&vm, &d);
        /* All known IDs dispatch to stubs which return true. */
        ASSERT_TRUE(ok);
        ASSERT_TRUE(vm.regs[0].i32 >= 0);
    }

    PASS();
}

/* --- VM integration: tool_action via IL assembly ------------------ */

static void test_vm_tool_action_integration(void) {
    const char *il =
        "resume\n"
        "tool_action r0, r1, r2\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    /* Set up registers before run. */
    vm.regs[1].i32 = AEGIS_TOOL_TRADE_INIT;
    int32_t args_off = 256;
    write_json(&vm, args_off, "{}");
    vm.regs[2].i32 = args_off;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    ASSERT_EQ(st, AEGIS_VM_YIELDED);

    /* r0 should contain a heap offset (result string). */
    ASSERT_TRUE(vm.regs[0].i32 >= 0);
    const char *result = (const char *)(vm.memory.base + vm.regs[0].i32 + sizeof(int32_t));
    ASSERT_TRUE(strstr(result, "ok") != NULL);

    free(bc.instructions);
    PASS();
}

/* --- VM integration: unknown tool via IL assembly ----------------- */

static void test_vm_tool_action_unknown_integration(void) {
    const char *il =
        "resume\n"
        "tool_action r0, r1, r2\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    vm.regs[1].i32 = 99; /* unknown */
    vm.regs[2].i32 = 0;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    /* The interpreter calls vm_error when the opcode returns false. */
    ASSERT_EQ(st, AEGIS_VM_ERROR);
    ASSERT_EQ(vm.exit_code, (uint32_t)0xFFBA);

    free(bc.instructions);
    PASS();
}

/* --- VM integration: fuel deducted by interpreter ----------------- */

static void test_vm_tool_action_fuel_deducted(void) {
    const char *il =
        "resume\n"
        "tool_action r0, r1, r2\n"
        "yield\n";

    aegis_bytecode_t bc;
    ASSERT_TRUE(compile_il(il, &bc));

    aegis_vm_t vm;
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));

    /* Set a low fuel budget to test that tool_action doesn't force-yield. */
    vm.config.fuel_budget = 60;
    vm.fuel = 60;

    vm.regs[1].i32 = AEGIS_TOOL_TRADE_INIT;
    int32_t args_off = 256;
    write_json(&vm, args_off, "{}");
    vm.regs[2].i32 = args_off;

    aegis_vm_status_t st = aegis_vm_run(&vm);
    /* Should yield normally, not force-yield. */
    ASSERT_EQ(st, AEGIS_VM_YIELDED);

    free(bc.instructions);
    PASS();
}

/* --- KNOWNLEDGE_QUERY with bound global KG ------------------------ */

static void test_knowledge_query_with_global_kg(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 1, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(&kg, 2, NPC_KG_NODE_ENTITY, emb);
    npc_kg_add_edge(&kg, 1, 2, npc_kg_relation_id("trusts"), 0.9f, 0);

    aegis_set_knowledge_graph(&kg);

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.fuel = 500;

    int32_t args_off = 256;
    write_json(&vm, args_off, "{\"keyphrase\":\"1\"}");
    const char *args_str = (const char *)(vm.memory.base + args_off);

    bool ok = aegis_op_knowledge_query(&vm, args_str);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    aegis_knowledge_result_t *res =
        (aegis_knowledge_result_t *)(vm.memory.base + vm.regs[0].i32);
    ASSERT_EQ(res->status, (int32_t)AEGIS_TOOL_OK);
    ASSERT_TRUE(res->fact_count > 0);

    aegis_knowledge_fact_t *facts =
        (aegis_knowledge_fact_t *)(vm.memory.base + vm.regs[0].i32
                                    + sizeof(aegis_knowledge_result_t));
    ASSERT_TRUE(strstr(facts[0].text, "node 1") != NULL);

    aegis_set_knowledge_graph(NULL);
    npc_kg_destroy(&kg);

    PASS();
}

/* --- KNOWLEDGE_QUERY with NULL KG --------------------------------- */

static void test_knowledge_query_no_graph(void) {
    aegis_set_knowledge_graph(NULL);

    aegis_vm_t vm;
    aegis_bytecode_t bc = {0};
    uint8_t arena[4096];
    make_vm(&vm, &bc, arena, sizeof(arena));
    vm.fuel = 500;

    int32_t args_off = 256;
    write_json(&vm, args_off, "{\"keyphrase\":\"test\"}");
    const char *args_str = (const char *)(vm.memory.base + args_off);

    bool ok = aegis_op_knowledge_query(&vm, args_str);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(vm.regs[0].i32 >= 0);

    aegis_knowledge_result_t *res =
        (aegis_knowledge_result_t *)(vm.memory.base + vm.regs[0].i32);
    ASSERT_EQ(res->status, (int32_t)AEGIS_TOOL_OK);

    aegis_knowledge_fact_t *facts =
        (aegis_knowledge_fact_t *)(vm.memory.base + vm.regs[0].i32
                                    + sizeof(aegis_knowledge_result_t));
    ASSERT_TRUE(strstr(facts[0].text, "no graph bound") != NULL);

    PASS();
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void) {
    printf("=== Aegis Tools Tests ===\n\n");

    RUN(test_tool_action_unknown_tool);
    RUN(test_tool_action_combat_error);
    RUN(test_tool_action_trade_sell_with_item);
    RUN(test_tool_action_defend_with_target);
    RUN(test_tool_action_goto_dispatches);
    RUN(test_tool_action_fuel_consumption);
    RUN(test_tool_action_fuel_exhaustion);
    RUN(test_tool_action_all_whitelisted);
    RUN(test_vm_tool_action_integration);
    RUN(test_vm_tool_action_unknown_integration);
    RUN(test_vm_tool_action_fuel_deducted);
    RUN(test_knowledge_query_with_global_kg);
    RUN(test_knowledge_query_no_graph);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
