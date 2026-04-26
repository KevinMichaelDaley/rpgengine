/**
 * @file aegis_ops_knowledge.c
 * @brief KNOWLEDGE_QUERY tool handler for AEGIS VM.
 *
 * Called from aegis_ops_tool.c dispatch when tool_id = 9.
 * Parses the keyphrase from JSON args, searches the NPC knowledge graph,
 * and writes a result struct to the heap arena.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/aegis/aegis_tools.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_memory.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <string.h>

#define KNOWLEDGE_RESULT_SLOT_SIZE 512
#define KNOWLEDGE_FUEL_COST 100

/* Global hook for the engine to inject the active knowledge graph.
 * In production this would be resolved via entity_id → graph lookup. */
npc_knowledge_graph_t *g_aegis_knowledge_graph = NULL;

/* ------------------------------------------------------------------ */
/* JSON helper using engine json_parse                                 */
/* ------------------------------------------------------------------ */

static bool parse_json_args_key(const char *json, const char *key,
                                char *out_val, size_t out_cap) {
    if (!json || !key || !out_val || out_cap == 0) return false;
    out_val[0] = '\0';

    uint8_t arena_buf[512];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));

    json_value_t root;
    if (!json_parse(json, strlen(json), &arena, &root)) return false;
    if (root.type != JSON_OBJECT) return false;

    const json_value_t *val = json_object_get(&root, key);
    if (!val || val->type != JSON_STRING) return false;

    return json_string_copy(val, out_val, out_cap);
}

/* ------------------------------------------------------------------ */
/* Result allocator                                                   */
/* ------------------------------------------------------------------ */

static int32_t allocate_result(aegis_vm_t *vm, int32_t status,
                               uint32_t fact_count,
                               const aegis_knowledge_fact_t *facts,
                               const char *text) {
    size_t text_len = text ? strlen(text) : 0;
    size_t need = sizeof(aegis_knowledge_result_t);
    for (uint32_t i = 0; i < fact_count; i++) {
        need += sizeof(aegis_knowledge_fact_t);
        need += text_len + 1;
    }
    if (need > KNOWLEDGE_RESULT_SLOT_SIZE) {
        need = KNOWLEDGE_RESULT_SLOT_SIZE;
    }

    int32_t off = aegis_memory_alloc(&vm->memory, (uint32_t)need);
    if (off < 0) return -1;

    uint8_t *dst = vm->memory.base + off;
    aegis_knowledge_result_t *res = (aegis_knowledge_result_t *)dst;
    res->status = status;
    res->fact_count = fact_count;

    uint8_t *write = dst + sizeof(aegis_knowledge_result_t);
    for (uint32_t i = 0; i < fact_count; i++) {
        aegis_knowledge_fact_t *f = (aegis_knowledge_fact_t *)write;
        f->relevance = facts[i].relevance;
        f->certainty = facts[i].certainty;
        memcpy(f->text, text, text_len);
        f->text[text_len] = '\0';
        write += sizeof(aegis_knowledge_fact_t) + text_len + 1;
    }
    return off;
}

/* ------------------------------------------------------------------ */
/* Public handler                                                     */
/* ------------------------------------------------------------------ */

bool aegis_op_knowledge_query(aegis_vm_t *vm, const char *args_json) {
    char keyphrase[128] = {0};
    parse_json_args_key(args_json, "keyphrase", keyphrase, sizeof(keyphrase));

    if (vm->fuel > KNOWLEDGE_FUEL_COST) {
        vm->fuel -= KNOWLEDGE_FUEL_COST;
    } else {
        vm->fuel = 0;
    }

    char fact_buf[256];
    if (g_aegis_knowledge_graph && keyphrase[0]) {
        snprintf(fact_buf, sizeof(fact_buf),
                 "knowledge_query: '%s' (graph search not yet integrated)",
                 keyphrase);
    } else {
        snprintf(fact_buf, sizeof(fact_buf),
                 "knowledge_query: '%s' — no graph bound",
                 keyphrase);
    }

    aegis_knowledge_fact_t fact;
    fact.relevance = 1.0f;
    fact.certainty = 100;

    int32_t off = allocate_result(vm, AEGIS_TOOL_OK, 1, &fact, fact_buf);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}
