/**
 * @file npc_state_tests.c
 * @brief NPC State Manager tests: init/destroy, compaction, registry,
 *        prompt assembly, KG prepopulation, context persistence.
 */

#include "ferrum/npc/npc_state_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN(fn) do { printf("  %-48s ", #fn); fn(); } while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        printf("FAIL (%s:%d) expected %d got %d\n", \
               __FILE__, __LINE__, (int)(exp), (int)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

/* ------------------------------------------------------------------ */
/* Context helper: append text and update token estimate               */
/* ------------------------------------------------------------------ */

static void context_append(npc_state_t *npc, const char *text) {
    if (!text) return;
    size_t txt_len = strlen(text);
    if (txt_len == 0) return;

    uint32_t need = npc->context_len + (uint32_t)txt_len;
    if (need + 1 > npc->context_cap) {
        uint32_t new_cap = npc->context_cap == 0 ? 64 : npc->context_cap * 2;
        while (new_cap < need + 1) new_cap *= 2;
        char *new_buf = (char *)realloc(npc->context_buffer, new_cap);
        if (!new_buf) return;
        npc->context_buffer = new_buf;
        npc->context_cap = new_cap;
    }
    memcpy(npc->context_buffer + npc->context_len, text, txt_len);
    npc->context_len += (uint32_t)txt_len;
    npc->context_buffer[npc->context_len] = '\0';
    npc->context_token_estimate = npc->context_len / 4;
    if (npc->context_token_estimate == 0 && npc->context_len > 0)
        npc->context_token_estimate = 1;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_state_init_destroy(void) {
    npc_state_t state;
    npc_state_init(&state, 42);

    ASSERT_INT_EQ(42, state.npc_id);
    ASSERT_INT_EQ(4096, state.context_max_tokens);
    ASSERT_TRUE(state.active);
    ASSERT_TRUE(!state.context_dirty);
    ASSERT_TRUE(state.system_prompt[0] != '\0');
    ASSERT_INT_EQ(0, state.kg.node_count);
    ASSERT_INT_EQ(16, state.kg.node_cap);
    ASSERT_INT_EQ(0, state.awareness.count);

    npc_state_destroy(&state);
    ASSERT_TRUE(state.context_buffer == NULL);
    ASSERT_INT_EQ(0, state.npc_id);  /* memset zeroed */
    PASS();
}

static void test_state_destroy_null_safe(void) {
    npc_state_destroy(NULL);
    PASS();
}

static void test_compact_reduces_over_budget(void) {
    npc_state_t state;
    npc_state_init(&state, 1);
    state.context_max_tokens = 10;
    state.context_dirty = false;

    /* Fill context with many chars to create token overflow. */
    char fill[512];
    memset(fill, 'A', sizeof(fill));
    fill[511] = '\0';
    context_append(&state, fill);

    uint32_t orig_len = state.context_len;
    ASSERT_TRUE(orig_len > 0);

    state.context_token_estimate = state.context_len; /* hack high estimate */
    state.context_dirty = true;

    bool ok = npc_state_compact(&state);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(state.context_len < orig_len);

    /* Iterate compaction until under budget. */
    for (int i = 0; i < 10; i++) {
        if (!state.context_dirty) break;
        npc_state_compact(&state);
    }
    ASSERT_TRUE(state.context_token_estimate <= state.context_max_tokens);

    npc_state_destroy(&state);
    PASS();
}

static void test_compact_noop_under_budget(void) {
    npc_state_t state;
    npc_state_init(&state, 1);
    state.context_max_tokens = 4096;
    context_append(&state, "Hello, world!");
    state.context_token_estimate = 1; /* under budget */
    state.context_dirty = true;

    uint32_t orig_len = state.context_len;
    npc_state_compact(&state);
    ASSERT_INT_EQ(orig_len, state.context_len);
    ASSERT_TRUE(!state.context_dirty);

    npc_state_destroy(&state);
    PASS();
}

static void test_registry_add_find(void) {
    npc_state_registry_t reg;
    npc_state_registry_init(&reg);
    ASSERT_INT_EQ(0, reg.count);

    npc_state_t s1, s2;
    npc_state_init(&s1, 100);
    npc_state_init(&s2, 200);

    ASSERT_TRUE(npc_state_registry_add(&reg, &s1));
    ASSERT_INT_EQ(1, reg.count);
    ASSERT_TRUE(npc_state_registry_add(&reg, &s2));
    ASSERT_INT_EQ(2, reg.count);

    npc_state_t *found = npc_state_registry_find(&reg, 100);
    ASSERT_TRUE(found != NULL);
    ASSERT_INT_EQ(100, found->npc_id);

    found = npc_state_registry_find(&reg, 200);
    ASSERT_TRUE(found != NULL);
    ASSERT_INT_EQ(200, found->npc_id);

    found = npc_state_registry_find(&reg, 999);
    ASSERT_TRUE(found == NULL);

    npc_state_destroy(&s2);
    npc_state_destroy(&s1);
    npc_state_registry_destroy(&reg);
    PASS();
}

static void test_registry_find_none(void) {
    npc_state_registry_t reg;
    npc_state_registry_init(&reg);

    npc_state_t *found = npc_state_registry_find(&reg, 1);
    ASSERT_TRUE(found == NULL);

    npc_state_registry_destroy(&reg);
    PASS();
}

static void test_prompt_assemble_includes_sections(void) {
    npc_state_t state;
    npc_state_init(&state, 1);
    strncpy(state.system_prompt, "SYS_PROMPT", 4095);
    strncpy(state.statblock, "{hp:100}", 2047);
    strncpy(state.status_line, "STATUS_OK", 511);
    context_append(&state, "PriorContext...");

    char *prompt = npc_state_prompt_assemble(&state, "User says hi");
    ASSERT_TRUE(prompt != NULL);

    ASSERT_TRUE(strstr(prompt, "SYS_PROMPT") != NULL);
    ASSERT_TRUE(strstr(prompt, "{hp:100}") != NULL);
    ASSERT_TRUE(strstr(prompt, "STATUS_OK") != NULL);
    ASSERT_TRUE(strstr(prompt, "PriorContext...") != NULL);
    ASSERT_TRUE(strstr(prompt, "User says hi") != NULL);

    free(prompt);
    npc_state_destroy(&state);
    PASS();
}

static void test_prompt_assemble_awareness(void) {
    npc_state_t state;
    npc_state_init(&state, 1);

    /* Manually add awareness entries. */
    state.awareness.entries[0].entity_id = 10;
    state.awareness.entries[0].last_salience = 0.75f;
    state.awareness.count = 1;

    char *prompt = npc_state_prompt_assemble(&state, NULL);
    ASSERT_TRUE(prompt != NULL);
    ASSERT_TRUE(strstr(prompt, "Awareness:") != NULL);

    free(prompt);
    npc_state_destroy(&state);
    PASS();
}

static void test_kg_prepopulate_copies_nodes(void) {
    npc_state_t npc;
    npc_state_init(&npc, 1);

    npc_knowledge_graph_t src;
    npc_kg_init(&src, 8, 4);

    float emb[4] = {1.0f, 0.5f, 0.0f, 0.0f};
    npc_kg_insert_node(&src, 10, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(&src, 20, NPC_KG_NODE_LOCATION, emb);

    uint32_t copied = npc_state_kg_prepopulate(&npc, &src);
    ASSERT_INT_EQ(2, copied);
    ASSERT_INT_EQ(2, npc.kg.node_count);
    ASSERT_TRUE(npc.shared_kg != NULL);

    npc_kg_destroy(&src);
    npc_state_destroy(&npc);
    PASS();
}

static void test_kg_prepopulate_empty_source(void) {
    npc_state_t npc;
    npc_state_init(&npc, 1);

    npc_knowledge_graph_t src;
    npc_kg_init(&src, 8, 4);

    uint32_t copied = npc_state_kg_prepopulate(&npc, &src);
    ASSERT_INT_EQ(0, copied);
    ASSERT_INT_EQ(0, npc.kg.node_count);

    npc_kg_destroy(&src);
    npc_state_destroy(&npc);
    PASS();
}

static void test_context_survives_multiple_updates(void) {
    npc_state_t state;
    npc_state_init(&state, 1);

    context_append(&state, "Turn1: Hello.\n");
    context_append(&state, "Turn2: How are you?\n");
    context_append(&state, "Turn3: I'm fine.\n");

    ASSERT_TRUE(strstr(state.context_buffer, "Turn1") != NULL);
    ASSERT_TRUE(strstr(state.context_buffer, "Turn2") != NULL);
    ASSERT_TRUE(strstr(state.context_buffer, "Turn3") != NULL);

    uint32_t len_after_three = state.context_len;
    ASSERT_TRUE(len_after_three > 10);

    npc_state_destroy(&state);
    PASS();
}

static void test_registry_null_safe(void) {
    npc_state_registry_init(NULL);
    npc_state_registry_destroy(NULL);
    ASSERT_TRUE(npc_state_registry_find(NULL, 0) == NULL);
    ASSERT_TRUE(npc_state_registry_add(NULL, NULL) == false);
    PASS();
}

static void test_prompt_survives_sys_prompt_on_overflow(void) {
    npc_state_t state;
    npc_state_init(&state, 1);

    strncpy(state.system_prompt, "SYS_PROMPT", 4095);
    strncpy(state.statblock, "{hp:100}", 2047);
    strncpy(state.status_line, "STATUS_OK", 511);

    state.context_max_tokens = 5;

    char fill[512];
    memset(fill, 'X', sizeof(fill));
    fill[511] = '\0';
    context_append(&state, fill);
    context_append(&state, " more_context_data_here");

    char *prompt = npc_state_prompt_assemble(&state, "Hi");
    ASSERT_TRUE(prompt != NULL);

    ASSERT_TRUE(strstr(prompt, "SYS_PROMPT") != NULL);
    ASSERT_TRUE(strstr(prompt, "{hp:100}") != NULL);
    ASSERT_TRUE(strstr(prompt, "STATUS_OK") != NULL);

    free(prompt);
    npc_state_destroy(&state);
    PASS();
}

static void test_prompt_truncates_only_context(void) {
    npc_state_t state;
    npc_state_init(&state, 1);

    strncpy(state.system_prompt, "Guard", 4095);
    strncpy(state.statblock, "{hp:50}", 2047);
    strncpy(state.status_line, "idle", 511);

    context_append(&state, "AAAA");
    context_append(&state, "BBBB");

    uint32_t orig_ctx_len = state.context_len;

    state.context_max_tokens = 10;

    char *prompt = npc_state_prompt_assemble(&state, NULL);
    ASSERT_TRUE(prompt != NULL);

    ASSERT_TRUE(strstr(prompt, "Guard") != NULL);
    ASSERT_TRUE(strstr(prompt, "{hp:50}") != NULL);
    ASSERT_TRUE(strstr(prompt, "idle") != NULL);

    size_t total_len = strlen(prompt);
    size_t fixed_str = strlen("Guard\n") + strlen("{hp:50}\n")
                     + strlen("idle\n") + strlen("Awareness: none\n") + 1;
    ASSERT_TRUE(total_len > fixed_str);
    ASSERT_TRUE(total_len < fixed_str + orig_ctx_len + 1);

    free(prompt);
    npc_state_destroy(&state);
    PASS();
}

static void test_prompt_overflow_no_context_dropped(void) {
    npc_state_t state;
    npc_state_init(&state, 1);

    strncpy(state.system_prompt, "SYS", 4095);
    strncpy(state.statblock, "BLK", 2047);
    strncpy(state.status_line, "OK", 511);

    context_append(&state, "context_data");

    state.context_max_tokens = 1;

    char *prompt = npc_state_prompt_assemble(&state, "user");
    ASSERT_TRUE(prompt != NULL);

    ASSERT_TRUE(strstr(prompt, "SYS") != NULL);
    ASSERT_TRUE(strstr(prompt, "BLK") != NULL);
    ASSERT_TRUE(strstr(prompt, "OK") != NULL);

    free(prompt);
    npc_state_destroy(&state);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("npc_state_tests:\n");
    RUN(test_state_init_destroy);
    RUN(test_state_destroy_null_safe);
    RUN(test_compact_reduces_over_budget);
    RUN(test_compact_noop_under_budget);
    RUN(test_registry_add_find);
    RUN(test_registry_find_none);
    RUN(test_prompt_assemble_includes_sections);
    RUN(test_prompt_assemble_awareness);
    RUN(test_kg_prepopulate_copies_nodes);
    RUN(test_kg_prepopulate_empty_source);
    RUN(test_context_survives_multiple_updates);
    RUN(test_registry_null_safe);
    RUN(test_prompt_survives_sys_prompt_on_overflow);
    RUN(test_prompt_truncates_only_context);
    RUN(test_prompt_overflow_no_context_dropped);
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
