---
id: rpg-o0a7
status: open
deps: [rpg-nun5]
links: []
created: 2026-03-02T18:38:32Z
type: task
priority: 2
assignee: KMD
---
# Phase 3: LCRS scene graph over entity set

Create a flat-array LCRS (left-child right-sibling) scene graph stored parallel to the entity pool. See ref/renderer_spec.md §4.

Deliverables:
- include/ferrum/renderer/scene/scene_node.h: scene_node_t struct (parent, first_child, next_sibling, flags, local_transform, world_transform), flag defines (DIRTY_LOCAL, DIRTY_WORLD, STATIC)
- include/ferrum/renderer/scene/scene_graph.h: scene_graph_t struct (nodes array parallel to entity pool, dirty_list for BFS update)
- src/renderer/scene/scene_graph_init.c: scene_graph_init() allocating parallel arrays to entity capacity
- src/renderer/scene/scene_graph_update.c: BFS from dirty roots propagating world = parent.world * local, cascade dirty to children, skip STATIC nodes
- src/renderer/scene/scene_graph_attach.c: scene_graph_attach(graph, entity_idx, parent_idx) — insert into LCRS tree
- src/renderer/scene/scene_graph_detach.c: scene_graph_detach(graph, entity_idx) — remove from tree, reparent children to detached node's parent
- Entity pool indices ARE scene node indices (parallel array design)
- Entities without scene presence have parent=first_child=next_sibling=UINT32_MAX
- Skeleton bones are entities in the LCRS tree parented under skeleton root entity
- Tests in tests/p004_renderer_scene_graph_tests.c

Depends on: rpg-nun5 (pipeline needs scene graph for world transforms)

