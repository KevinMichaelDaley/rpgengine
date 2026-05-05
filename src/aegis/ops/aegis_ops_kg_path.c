/**
 * @file aegis_ops_kg_path.c
 * @brief KG_SHORTEST_PATH tool handler for AEGIS VM (tool_id = 11).
 *
 * Parses JSON args for "target", resolves to node_id, runs A* from
 * the NPC's self node, and returns the path chain in the heap arena.
 *
 * ≤4 non-static functions:
 *   1. aegis_op_kg_path (public)
 *   2. find_node_by_id (static)
 *   3. write_path_result (static)
 *   4. parse_path_args (static)
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

#define PATH_RESULT_SLOT_SIZE 2048

/* Global hook shared with knowledge query handler. */
extern npc_knowledge_graph_t *g_aegis_knowledge_graph;
extern npc_state_registry_t *g_npc_state_registry;

typedef struct aegis_kg_path_step {
    uint32_t relation_id;
    uint64_t to_entity_id;
    char     relation_name[32];
    char     entity_name[32];
} aegis_kg_path_step_t;

typedef struct aegis_kg_path_result {
    int32_t  status;
    uint32_t step_count;
} aegis_kg_path_result_t;

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
/* Parse JSON args for "target"                                       */
/* ------------------------------------------------------------------ */

static int parse_path_args(const char *json, uint64_t *out_target_id) {
    if (!json) return -1;

    uint8_t arena_buf[512];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));

    json_value_t root;
    if (!json_parse(json, strlen(json), &arena, &root)) return -1;
    if (root.type != JSON_OBJECT) return -1;

    const json_value_t *target_val = json_object_get(&root, "target");
    if (!target_val) return -1;

    if (target_val->type == JSON_STRING) {
        char buf[64];
        if (json_string_copy(target_val, buf, sizeof(buf))) {
            char *end = NULL;
            *out_target_id = (uint64_t)strtoull(buf, &end, 10);
            if (end == buf) return -1;
        } else {
            return -1;
        }
    } else if (target_val->type == JSON_NUMBER) {
        *out_target_id = (uint64_t)target_val->number;
    } else {
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Write path result to heap arena                                    */
/* ------------------------------------------------------------------ */

static bool write_path_result(aegis_vm_t *vm,
                               const npc_kg_path_result_t *astar_res) {
    uint32_t step_count = astar_res->found ? astar_res->step_count : 0;

    size_t need = sizeof(aegis_kg_path_result_t)
                  + step_count * sizeof(aegis_kg_path_step_t);
    if (need > PATH_RESULT_SLOT_SIZE) need = PATH_RESULT_SLOT_SIZE;

    int32_t off = aegis_memory_alloc(&vm->memory, (uint32_t)need);
    if (off < 0) return false;

    uint8_t *dst = vm->memory.base + off;
    aegis_kg_path_result_t *res = (aegis_kg_path_result_t *)dst;
    res->status = astar_res->found ? 0 : -1;
    res->step_count = step_count;

    if (astar_res->found) {
        aegis_kg_path_step_t *steps = (aegis_kg_path_step_t *)(dst +
            sizeof(aegis_kg_path_result_t));

        for (uint32_t i = 0; i < step_count; i++) {
            steps[i].relation_id = astar_res->relation_ids[i];
            steps[i].to_entity_id = astar_res->node_ids[i + 1];

            const char *rname = npc_kg_relation_name(astar_res->relation_ids[i]);
            if (rname) {
                snprintf(steps[i].relation_name,
                         sizeof(steps[i].relation_name), "%s", rname);
            } else {
                snprintf(steps[i].relation_name,
                         sizeof(steps[i].relation_name), "%u",
                         astar_res->relation_ids[i]);
            }

            snprintf(steps[i].entity_name, sizeof(steps[i].entity_name),
                     "%llu",
                     (unsigned long long)astar_res->node_ids[i + 1]);
        }
    }

    vm->regs[0].i32 = off;
    return true;
}

/* ================================================================== */
/* Public handler                                                     */
/* ================================================================== */

bool aegis_op_kg_path(aegis_vm_t *vm, const char *args_json) {
    uint64_t target_id = 0;
    if (parse_path_args(args_json, &target_id) != 0) {
        int32_t off = aegis_memory_alloc(&vm->memory, 64);
        if (off < 0) return false;
        aegis_kg_path_result_t *res =
            (aegis_kg_path_result_t *)(vm->memory.base + off);
        res->status = -1;
        res->step_count = 0;
        vm->regs[0].i32 = off;
        return true;
    }

    /* Start node = NPC's entity_id. */
    uint64_t start_id = (uint64_t)vm->entity_id;

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

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = start_id;
    req.goal_node_id = target_id;
    req.max_cost = 1e10f;

    npc_kg_path_result_t astar_res;
    memset(&astar_res, 0, sizeof(astar_res));

    npc_kg_astar(target_kg, &req, &astar_res);

    bool ok = write_path_result(vm, &astar_res);

    free(astar_res.node_ids);
    free(astar_res.relation_ids);

    return ok;
}
