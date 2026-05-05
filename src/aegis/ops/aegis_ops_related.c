/**
 * @file aegis_ops_related.c
 * @brief RELATED_ENTITIES tool handler for AEGIS VM (tool_id = 10).
 *
 * Parses JSON args for "entity" and "relation", looks up the entity
 * in the knowledge graph, and returns all related entities matching
 * the given relation.
 *
 * ≤4 non-static functions:
 *   1. aegis_op_related_entities (public)
 *   2. find_node_by_id (static)
 *   3. write_related_result (static)
 *   4. parse_related_args (static)
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

#define RELATED_RESULT_SLOT_SIZE 1024

/* Global hook shared with knowledge query handler. */
extern npc_knowledge_graph_t *g_aegis_knowledge_graph;
extern npc_state_registry_t *g_npc_state_registry;

typedef struct aegis_related_entity {
    uint32_t relation_id;
    uint64_t entity_id;
    float    weight;
    char     name[32];
} aegis_related_entity_t;

typedef struct aegis_related_entities_result {
    int32_t  status;
    uint32_t count;
} aegis_related_entities_result_t;

/* ------------------------------------------------------------------ */
/* Find node by id                                                    */
/* ------------------------------------------------------------------ */

static npc_kg_node_t *find_node_by_id(npc_knowledge_graph_t *kg,
                                       uint64_t node_id) {
    if (!kg) return NULL;
    for (uint32_t i = 0; i < kg->node_count; i++) {
        if (kg->nodes[i].node_id == node_id) return &kg->nodes[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Parse JSON args for "entity" and "relation"                        */
/* ------------------------------------------------------------------ */

static int parse_related_args(const char *json, uint64_t *out_entity_id,
                               uint32_t *out_rel_id) {
    if (!json) return -1;

    uint8_t arena_buf[512];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));

    json_value_t root;
    if (!json_parse(json, strlen(json), &arena, &root)) return -1;
    if (root.type != JSON_OBJECT) return -1;

    /* Parse "entity" as uint64. */
    const json_value_t *entity_val = json_object_get(&root, "entity");
    if (!entity_val) return -1;

    if (entity_val->type == JSON_STRING) {
        char buf[64];
        if (json_string_copy(entity_val, buf, sizeof(buf))) {
            char *end = NULL;
            *out_entity_id = (uint64_t)strtoull(buf, &end, 10);
            if (end == buf) return -1;
        } else {
            return -1;
        }
    } else if (entity_val->type == JSON_NUMBER) {
        *out_entity_id = (uint64_t)entity_val->number;
    } else {
        return -1;
    }

    /* Parse "relation" as string, resolve to relation id. */
    const json_value_t *rel_val = json_object_get(&root, "relation");
    if (!rel_val || rel_val->type != JSON_STRING) return -1;

    char rel_buf[64];
    if (!json_string_copy(rel_val, rel_buf, sizeof(rel_buf))) return -1;

    const char *rel_name = npc_kg_relation_name((uint32_t)0);
    /* First, try to look up by name. */
    (void)rel_name;
    *out_rel_id = npc_kg_relation_id(rel_buf);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Write result to heap arena                                         */
/* ------------------------------------------------------------------ */

static bool write_related_result(aegis_vm_t *vm, npc_kg_node_t *node,
                                  uint32_t rel_id) {
    aegis_related_entity_t buf[64];
    uint32_t count = 0;

    if (node) {
        for (uint32_t j = 0; j < node->edge_count && count < 64; j++) {
            if (node->edges[j].relation_id == rel_id) {
                buf[count].relation_id = node->edges[j].relation_id;
                buf[count].entity_id = node->edges[j].to_node_id;
                buf[count].weight = node->edges[j].weight;
                snprintf(buf[count].name, sizeof(buf[count].name),
                         "%llu", (unsigned long long)node->edges[j].to_node_id);
                count++;
            }
        }
    }

    size_t need = sizeof(aegis_related_entities_result_t)
                  + count * sizeof(aegis_related_entity_t);
    if (need > RELATED_RESULT_SLOT_SIZE) need = RELATED_RESULT_SLOT_SIZE;

    int32_t off = aegis_memory_alloc(&vm->memory, (uint32_t)need);
    if (off < 0) return false;

    uint8_t *dst = vm->memory.base + off;
    aegis_related_entities_result_t *res = (aegis_related_entities_result_t *)dst;
    res->status = 0;
    res->count = count;

    aegis_related_entity_t *items = (aegis_related_entity_t *)(dst +
        sizeof(aegis_related_entities_result_t));
    memcpy(items, buf, count * sizeof(aegis_related_entity_t));

    vm->regs[0].i32 = off;
    return true;
}

/* ================================================================== */
/* Public handler                                                     */
/* ================================================================== */

bool aegis_op_related_entities(aegis_vm_t *vm, const char *args_json) {
    uint64_t entity_id = 0;
    uint32_t rel_id = 0;

    if (parse_related_args(args_json, &entity_id, &rel_id) != 0) {
        int32_t off = aegis_memory_alloc(&vm->memory, 64);
        if (off < 0) return false;
        aegis_related_entities_result_t *res =
            (aegis_related_entities_result_t *)(vm->memory.base + off);
        res->status = -1;
        res->count = 0;
        vm->regs[0].i32 = off;
        return true;
    }

    /* Resolve KG: per-entity if registry available, else global. */
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

    npc_kg_node_t *node = find_node_by_id(target_kg, entity_id);
    return write_related_result(vm, node, rel_id);
}
