---
id: rpg-kg06
status: open
deps: [rpg-llm02b]
links: [rpg-kg02]
created: 2026-05-03T20:00:00Z
type: bug
priority: 3
assignee: KMD
parent:
tags: [knowledge-graph, bug, faiss, type-cast, overflow]
---
# faiss_index_search int Cast to uint32 May Yield OOB on Negative Errors

`npc_kg_search` in `src/npc/graph/npc_kg_search.c:17` calls:
```c
int found = faiss_index_search(...);
if (found > 0) total_found = (uint32_t)found;
```
FAISS can return negative error codes (e.g., -1 for invalid query). If cast to `uint32_t`, `-1` becomes `4294967295`, causing out-of-bounds array access on `out_ids` and `out_scores`.

## Fix
Check for negative return before casting:
```c
if (found < 0) found = 0;
total_found = (uint32_t)found;
```

## Acceptance
- [ ] Negative FAISS return values are clamped to 0
- [ ] No OOB access when FAISS errors
