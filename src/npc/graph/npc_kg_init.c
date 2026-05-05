/**
 * @file npc_kg_init.c
 * @brief Knowledge graph init/destroy and relation registry.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <stdlib.h>
#include <string.h>

/* ======================================================================= */
/* Built-in relation table                                                 */
/* ======================================================================= */

static const char *g_builtin_relations[] = {
    "saw_at",
    "heard_from",
    "owns",
    "fears",
    "trusts",
    "located_at",
    "possesses",
    "aware_of",
    "bartered_with",
    "reputation",
    "built",
    "destroyed",
    "near",
    "inside",
    "adjacent_to",
    "visible_from",
    "reachable_from",
    "path_to",
};

#define NPC_KG_BUILTIN_COUNT \
    (sizeof(g_builtin_relations) / sizeof(g_builtin_relations[0]))

#define NPC_KG_MAX_RELATIONS 256

static char g_relation_names[NPC_KG_MAX_RELATIONS][32];
static uint32_t g_relation_count = 0;
static int g_relation_init_done = 0;

static void npc_kg_relation_init(void) {
    if (g_relation_init_done) return;
    g_relation_init_done = 1;
    for (size_t i = 0; i < NPC_KG_BUILTIN_COUNT; i++) {
        strncpy(g_relation_names[i], g_builtin_relations[i], 31);
        g_relation_names[i][31] = '\0';
    }
    g_relation_count = (uint32_t)NPC_KG_BUILTIN_COUNT;
}

const char *npc_kg_relation_name(uint32_t relation_id) {
    npc_kg_relation_init();
    if (relation_id >= g_relation_count) return NULL;
    return g_relation_names[relation_id];
}

uint32_t npc_kg_relation_id(const char *name) {
    npc_kg_relation_init();
    if (!name) return (uint32_t)-1;

    for (uint32_t i = 0; i < g_relation_count; i++) {
        if (strcmp(g_relation_names[i], name) == 0) {
            return i;
        }
    }

    if (g_relation_count >= NPC_KG_MAX_RELATIONS) {
        return (uint32_t)-1;
    }

    uint32_t id = g_relation_count++;
    strncpy(g_relation_names[id], name, 31);
    g_relation_names[id][31] = '\0';
    return id;
}

/* ======================================================================= */
/* Graph lifecycle                                                         */
/* ======================================================================= */

bool npc_kg_init(npc_knowledge_graph_t *kg, uint32_t node_cap,
                 uint32_t embedding_dim) {
    if (!kg || node_cap == 0) return false;
    memset(kg, 0, sizeof(*kg));
    kg->nodes = (npc_kg_node_t *)calloc(node_cap, sizeof(npc_kg_node_t));
    if (!kg->nodes) return false;
    kg->node_cap = node_cap;
    kg->embedding_dim = embedding_dim;
    return true;
}

void npc_kg_destroy(npc_knowledge_graph_t *kg) {
    if (!kg) return;
    for (uint32_t i = 0; i < kg->node_count; i++) {
        npc_kg_node_t *n = &kg->nodes[i];
        free(n->embedding);
        free(n->edges);
    }
    free(kg->nodes);
    if (kg->faiss_index) {
        faiss_index_destroy(kg->faiss_index);
    }
    memset(kg, 0, sizeof(*kg));
}
