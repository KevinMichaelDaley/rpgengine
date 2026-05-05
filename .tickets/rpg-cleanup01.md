---
id: rpg-cleanup01
status: open
deps: []
links: []
created: 2026-05-03T20:00:00Z
type: chore
priority: 4
assignee: KMD
parent:
tags: [cleanup, dead-code]
---
# Dead Code: Unused Static Functions

Two static functions are defined but never called:

1. `aabb_contains_point_` in `src/npc/nav/npc_svo_floodfill.c:25` — defined as `static inline bool`, never referenced. Compiler may optimize it away but it's dead code.

2. `find_node_by_id` in `src/aegis/ops/aegis_ops_kg_path.c:46` — defined as `static npc_kg_node_t *`, never called in this translation unit. A separate `find_node_by_id` in `aegis_ops_related.c:47` IS used.

## Fix
Remove both unused functions. No functional impact.

## Acceptance
- [ ] `aabb_contains_point_` removed from floodfill
- [ ] `find_node_by_id` removed from aegis_ops_kg_path.c
- [ ] Build succeeds with no new warnings
