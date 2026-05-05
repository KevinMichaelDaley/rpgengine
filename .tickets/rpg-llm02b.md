---
id: rpg-llm02b
status: closed
deps: [rpg-llm01]
links: []
created: 2026-04-26T01:20:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, npc, ai, knowledge, graph, faiss, memory]
---
# KNOWLEDGE_QUERY + Per-NPC Knowledge Graph + FAISS Wrapper

Implement the `AEGIS_OP_KNOWLEDGE_QUERY = 0x4C` immediate opcode, the per-NPC knowledge graph data structures, and the FAISS C++ wrapper for semantic search.

## Requirements

### Tool Action Entry
KNOWLEDGE_QUERY does **not** have a standalone opcode. It is always invoked via `AEGIS_OP_TOOL_ACTION` with tool_id = 9. This ensures all results pass through the AEGIS VM, where deterministic callbacks can filter, sanitize, or augment the fact list before it reaches the LLM.

```json
{"name": "KNOWLEDGE_QUERY", "arguments": {"keyphrase": "where is the forge"}}
```

- Fuel cost: 100 (deducted by the tool_action handler)

### Knowledge Graph Data Structures
```c
typedef struct npc_kg_node {
    uint64_t node_id;
    uint32_t type;            /* ENTITY, EVENT, FACT, LOCATION, CONCEPT */
    float    embedding[768];  /* for semantic search */
    uint32_t edge_count;
    uint32_t edge_cap;
    struct npc_kg_edge *edges;
} npc_kg_node_t;

typedef struct npc_kg_edge {
    uint64_t    to_node_id;
    uint32_t    relation_type;  /* SAW_AT, HEARD_FROM, OWNS, FEARS, etc. */
    float       weight;         /* 0.0-1.0, decays over time */
    uint64_t    timestamp_us;
} npc_kg_edge_t;

typedef struct npc_knowledge_graph {
    npc_kg_node_t *nodes;
    uint32_t       node_count;
    uint32_t       node_cap;
    struct npc_knowledge_graph *shared;  /* faction/common subgraph */
    void          *faiss_index;          /* opaque FAISS handle */
} npc_knowledge_graph_t;
```

### Execution Flow (inside tool_action dispatch)
1. Embed keyphrase locally (lightweight model, e.g. `nomic-embed-text` via Ollama)
2. Search personal FAISS index + shared faction FAISS index
3. Merge, deduplicate by node_id
4. Graph traversal: one hop outward from candidate nodes
5. **Deterministic callback filter**: AEGIS VM applies registered filters (certainty threshold, faction censor) to the ranked fact list
6. Return filtered `aegis_knowledge_result_t` in heap arena

### FAISS C++ Wrapper (C-compatible shim)
```c
void *faiss_index_create(int dim);
void  faiss_index_add(void *index, int n, const float *vectors, const uint64_t *ids);
int   faiss_index_search(void *index, int nq, const float *queries, int k, float *distances, uint64_t *ids);
void  faiss_index_destroy(void *index);
```

### Execution Flow
1. Embed keyphrase locally (lightweight model, e.g. `nomic-embed-text` via Ollama)
2. Search personal FAISS index + shared faction FAISS index
3. Merge, deduplicate by node_id
4. Graph traversal: one hop outward from candidate nodes
5. Return ranked `aegis_knowledge_fact_t` array

### Result Layout
```c
typedef struct aegis_knowledge_result {
    int32_t  status;
    uint32_t fact_count;
    /* Followed by fact_count × aegis_knowledge_fact_t */
} aegis_knowledge_result_t;

typedef struct aegis_knowledge_fact {
    float    relevance;       /* 0.0-1.0 semantic match score */
    uint32_t certainty;       /* 0-100, decreases with gossip chains */
    char     text[];          /* null-terminated natural language fact */
} aegis_knowledge_fact_t;
```

## Files to Create
- `include/ferrum/npc/npc_knowledge_graph.h` — graph node/edge types
- `src/npc/graph/npc_kg_init.c` — graph init/destroy
- `src/npc/graph/npc_kg_insert.c` — node/edge insert (≤4 non-static functions)
- `src/npc/graph/npc_kg_search.c` — semantic search via FAISS wrapper
- `src/npc/graph/npc_kg_decay.c` — edge weight decay over time
- `src/npc/graph/npc_kg_faiss_wrapper.cpp` — FAISS C++ shim
- `src/aegis/ops/aegis_ops_knowledge.c` — KNOWLEDGE_QUERY tool handler (called from aegis_ops_tool.c dispatch table)
- `tests/npc/npc_knowledge_graph_tests.c` — insert/search/decay tests
- `tests/npc/npc_faiss_tests.c` — FAISS create/add/search/destroy tests

## Files to Modify
- `src/aegis/ops/aegis_ops_tool.c` — wire tool_id=9 to `aegis_op_knowledge_query()`
- `Makefile` — add FAISS build rules and `src/npc/` wildcard

## Acceptance
- [ ] Graph insert + search works with mock embeddings.
- [ ] FAISS wrapper compiles and links against `libfaiss.a` (CPU-only).
- [ ] FAISS search returns top-k nearest nodes by cosine similarity.
- [ ] Shared subgraph pointer queries both personal and faction indices.
- [ ] Edge decay reduces weight over simulated time.
- [ ] KNOWLEDGE_QUERY opcode returns facts in heap arena.
