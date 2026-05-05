/**
 * @file aegis_ops_knowledge.c
 * @brief KNOWLEDGE_QUERY tool handler for AEGIS VM.
 *
 * Called from aegis_ops_tool.c dispatch when tool_id = 9.
 * Parses the keyphrase from JSON args, searches the NPC knowledge graph,
 * and writes a result struct to the heap arena.
 *
 * If g_npc_state_registry is set, resolves vm->entity_id to
 * npc_state_t and uses the NPC's personal KG. Falls back to
 * g_aegis_knowledge_graph when no state is found.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/npc/npc_state_manager.h"
#include "ferrum/aegis/aegis_tools.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_memory.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define KNOWLEDGE_RESULT_SLOT_SIZE 512
#define KNOWLEDGE_FUEL_COST 100
#define KNOWLEDGE_MAX_FACTS 16

/* Global hook for the engine to inject the active knowledge graph.
 * In production this would be resolved via entity_id -> graph lookup. */
npc_knowledge_graph_t *g_aegis_knowledge_graph = NULL;

/* Per-NPC state registry; when present, resolves per-entity KG. */
extern npc_state_registry_t *g_npc_state_registry;

/* ------------------------------------------------------------------ */
/* Public setter                                                       */
/* ------------------------------------------------------------------ */

void aegis_set_knowledge_graph(npc_knowledge_graph_t *kg) {
    g_aegis_knowledge_graph = kg;
}

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
/* Result allocator with per-fact text                                 */
/* ------------------------------------------------------------------ */

static int32_t allocate_result_multi(aegis_vm_t *vm, int32_t status,
                                     uint32_t fact_count,
                                     const float *relevances,
                                     const uint32_t *certainties,
                                     const char **texts) {
    if (fact_count > KNOWLEDGE_MAX_FACTS) {
        fact_count = KNOWLEDGE_MAX_FACTS;
    }

    size_t need = sizeof(aegis_knowledge_result_t);
    for (uint32_t i = 0; i < fact_count; i++) {
        size_t len = texts[i] ? strlen(texts[i]) : 0;
        need += sizeof(aegis_knowledge_fact_t) + len + 1;
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
        size_t len = texts[i] ? strlen(texts[i]) : 0;
        size_t chunk = sizeof(aegis_knowledge_fact_t) + len + 1;
        if ((write + chunk) > (dst + need)) break;

        aegis_knowledge_fact_t *f = (aegis_knowledge_fact_t *)write;
        f->relevance = relevances ? relevances[i] : 1.0f;
        f->certainty = certainties ? certainties[i] : 100;
        if (texts[i]) {
            memcpy(f->text, texts[i], len);
            f->text[len] = '\0';
        } else {
            f->text[0] = '\0';
        }
        write += chunk;
    }
    return off;
}

/* ------------------------------------------------------------------ */
/* Graph search helpers                                                */
/* ------------------------------------------------------------------ */

static npc_kg_node_t *find_node_in_kg(npc_knowledge_graph_t *kg,
                                      uint64_t node_id) {
    if (!kg) return NULL;
    for (uint32_t i = 0; i < kg->node_count; i++) {
        if (kg->nodes[i].node_id == node_id) return &kg->nodes[i];
    }
    return NULL;
}

static const char *node_type_name(uint32_t type) {
    switch (type) {
    case NPC_KG_NODE_ENTITY:   return "entity";
    case NPC_KG_NODE_EVENT:    return "event";
    case NPC_KG_NODE_FACT:     return "fact";
    case NPC_KG_NODE_LOCATION: return "location";
    case NPC_KG_NODE_CONCEPT:  return "concept";
    default:                   return "unknown";
    }
}

/* Collect up to max_facts fact strings into texts array.
 * Returns the actual number collected. */
static uint32_t collect_graph_facts(npc_knowledge_graph_t *kg,
                                    const char *keyphrase,
                                    const char **texts, uint32_t max_facts) {
    if (!kg || !keyphrase || !texts || max_facts == 0) return 0;

    static char buf[16][256];
    uint32_t count = 0;

    /* Try to match keyphrase as a node ID. */
    char *end = NULL;
    uint64_t query_id = (uint64_t)strtoull(keyphrase, &end, 10);
    if (end != keyphrase && *end == '\0') {
        npc_kg_node_t *n = find_node_in_kg(kg, query_id);
        if (n) {
            snprintf(buf[count], sizeof(buf[count]),
                     "node %llu: %s, %u edges",
                     (unsigned long long)n->node_id,
                     node_type_name(n->type), n->edge_count);
            texts[count] = buf[count];
            count++;

            for (uint32_t j = 0; j < n->edge_count && count < max_facts; j++) {
                const char *rname = npc_kg_relation_name(n->edges[j].relation_id);
                snprintf(buf[count], sizeof(buf[count]),
                         "edge: --[%s]-->%llu w=%.2f",
                         rname ? rname : "?",
                         (unsigned long long)n->edges[j].to_node_id,
                         (double)n->edges[j].weight);
                texts[count] = buf[count];
                count++;
            }
            return count;
        }
    }

    /* Match keyphrase as a relation name. */
    uint32_t rel_id = npc_kg_relation_id(keyphrase);
    const char *rel_name = npc_kg_relation_name(rel_id);
    if (rel_name && strcmp(rel_name, keyphrase) == 0) {
        for (uint32_t i = 0; i < kg->node_count && count < max_facts; i++) {
            npc_kg_node_t *n = &kg->nodes[i];
            for (uint32_t j = 0; j < n->edge_count && count < max_facts; j++) {
                if (n->edges[j].relation_id == rel_id) {
                    snprintf(buf[count], sizeof(buf[count]),
                             "node %llu --[%s]--> %llu w=%.2f",
                             (unsigned long long)n->node_id,
                             rel_name,
                             (unsigned long long)n->edges[j].to_node_id,
                             (double)n->edges[j].weight);
                    texts[count] = buf[count];
                    count++;
                }
            }
        }
        if (count == 0 && max_facts > 0) {
            snprintf(buf[0], sizeof(buf[0]),
                     "relation '%s' exists but no matching edges found",
                     keyphrase);
            texts[0] = buf[0];
            count = 1;
        }
        return count;
    }

    /* Fallback: summarize all nodes. */
    snprintf(buf[count], sizeof(buf[count]),
             "graph has %u nodes", kg->node_count);
    texts[count] = buf[count];
    count++;

    for (uint32_t i = 0; i < kg->node_count && count < max_facts; i++) {
        npc_kg_node_t *n = &kg->nodes[i];
        snprintf(buf[count], sizeof(buf[count]),
                 "node %llu: %s, %u edges",
                 (unsigned long long)n->node_id,
                 node_type_name(n->type), n->edge_count);
        texts[count] = buf[count];
        count++;
    }

    return count;
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

    /* Resolve per-NPC KG via registry if available. */
    npc_knowledge_graph_t *target_kg = g_aegis_knowledge_graph;
    if (g_npc_state_registry) {
        npc_state_t *npc = npc_state_registry_find(g_npc_state_registry,
                                                    vm->entity_id);
        if (npc) {
            target_kg = &npc->kg;
            if (npc->shared_kg) {
                target_kg = npc->shared_kg;
            }
        }
    }

    if (!target_kg) {
        const char *no_graph_text = "knowledge_query: no graph bound";
        int32_t off = allocate_result_multi(vm, AEGIS_TOOL_OK, 1,
                                             NULL, NULL, &no_graph_text);
        if (off < 0) return false;
        vm->regs[0].i32 = off;
        return true;
    }

    if (!keyphrase[0]) {
        const char *empty_text = "knowledge_query: empty keyphrase";
        int32_t off = allocate_result_multi(vm, AEGIS_TOOL_OK, 1,
                                             NULL, NULL, &empty_text);
        if (off < 0) return false;
        vm->regs[0].i32 = off;
        return true;
    }

    const char *texts[KNOWLEDGE_MAX_FACTS];
    uint32_t fact_count = collect_graph_facts(target_kg, keyphrase, texts,
                                               KNOWLEDGE_MAX_FACTS);

    if (fact_count == 0) {
        const char *not_found = "knowledge_query: nothing found";
        int32_t off = allocate_result_multi(vm, AEGIS_TOOL_OK, 1,
                                             NULL, NULL, &not_found);
        if (off < 0) return false;
        vm->regs[0].i32 = off;
        return true;
    }

    int32_t off = allocate_result_multi(vm, AEGIS_TOOL_OK, fact_count,
                                         NULL, NULL, texts);
    if (off < 0) return false;
    vm->regs[0].i32 = off;
    return true;
}
