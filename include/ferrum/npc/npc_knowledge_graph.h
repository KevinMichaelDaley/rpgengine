/**
 * @file npc_knowledge_graph.h
 * @brief Per-NPC knowledge graph types and FAISS wrapper.
 *
 * Defines the graph node/edge structures, result types for KNOWLEDGE_QUERY,
 * and a minimal C-compatible FAISS wrapper.
 */

#ifndef FERRUM_NPC_KNOWLEDGE_GRAPH_H
#define FERRUM_NPC_KNOWLEDGE_GRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ======================================================================= */
/* Node types                                                              */
/* ======================================================================= */

#define NPC_KG_NODE_ENTITY   0
#define NPC_KG_NODE_EVENT    1
#define NPC_KG_NODE_FACT     2
#define NPC_KG_NODE_LOCATION 3
#define NPC_KG_NODE_CONCEPT  4

/* ======================================================================= */
/* Graph structures                                                        */
/* ======================================================================= */

typedef struct npc_kg_edge {
    uint64_t to_node_id;
    uint32_t relation_id;    /* index into runtime relation name table */
    float    weight;
    uint64_t timestamp_us;
} npc_kg_edge_t;

typedef struct npc_kg_node {
    uint64_t node_id;
    uint32_t type;
    float   *embedding;
    uint32_t edge_count;
    uint32_t edge_cap;
    npc_kg_edge_t *edges;
} npc_kg_node_t;

typedef struct npc_knowledge_graph {
    npc_kg_node_t *nodes;
    uint32_t       node_count;
    uint32_t       node_cap;
    struct npc_knowledge_graph *shared;
    void          *faiss_index;
    uint32_t       embedding_dim;
} npc_knowledge_graph_t;

/* ======================================================================= */
/* Result types                                                            */
/* ======================================================================= */

typedef struct aegis_knowledge_fact {
    float    relevance;
    uint32_t certainty;
    char     text[];
} aegis_knowledge_fact_t;

typedef struct aegis_knowledge_result {
    int32_t  status;
    uint32_t fact_count;
    /* Followed by fact_count * aegis_knowledge_fact_t */
} aegis_knowledge_result_t;

/* ======================================================================= */
/* Relation registry                                                       */
/* ======================================================================= */

/**
 * @brief Get the name of a relation by its runtime ID.
 *
 * Built-in relations occupy IDs 0..N and are pre-registered at init.
 * Custom relations can be added at runtime.
 *
 * @return Pointer to the name string, or NULL if not found.
 */
const char *npc_kg_relation_name(uint32_t relation_id);

/**
 * @brief Look up a relation ID by name, registering a new one if needed.
 *
 * Built-in names return stable IDs. Unknown names are appended dynamically.
 *
 * @return The relation ID.
 */
uint32_t npc_kg_relation_id(const char *name);

/* ======================================================================= */
/* Graph lifecycle                                                         */
/* ======================================================================= */

bool npc_kg_init(npc_knowledge_graph_t *kg, uint32_t node_cap,
                 uint32_t embedding_dim);
void npc_kg_destroy(npc_knowledge_graph_t *kg);

/* ======================================================================= */
/* Graph mutation                                                          */
/* ======================================================================= */

npc_kg_node_t *npc_kg_insert_node(npc_knowledge_graph_t *kg, uint64_t node_id,
                                  uint32_t type, const float *embedding);
bool npc_kg_add_edge(npc_knowledge_graph_t *kg, uint64_t from_id,
                     uint64_t to_id, uint32_t relation_id,
                     float weight, uint64_t timestamp_us);

/* ======================================================================= */
/* Graph query                                                             */
/* ======================================================================= */

uint32_t npc_kg_search(npc_knowledge_graph_t *kg, const float *query_emb,
                       uint32_t k, uint64_t *out_ids, float *out_scores);

/* ======================================================================= */
/* Edge decay                                                              */
/* ======================================================================= */

void npc_kg_decay_edges(npc_knowledge_graph_t *kg, float dt_seconds,
                        float lambda);

/* ======================================================================= */
/* FAISS C++ wrapper (C-compatible)                                        */
/* ======================================================================= */

void *faiss_index_create(int dim);
void  faiss_index_add(void *index, int n, const float *vectors,
                      const uint64_t *ids);
int   faiss_index_search(void *index, int nq, const float *queries,
                         int k, float *distances, uint64_t *ids);
void  faiss_index_destroy(void *index);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_KNOWLEDGE_GRAPH_H */
