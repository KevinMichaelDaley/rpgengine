---
id: rpg-ebpv
status: open
deps: [rpg-m3nv, rpg-o0a7]
links: [rpg-tqrt, rpg-q6fa, rpg-eabp, rpg-m3nv, rpg-2nxx, rpg-hrrd, rpg-21zg, rpg-bhqa]
created: 2026-03-03T03:00:39Z
type: task
priority: 2
assignee: KMD
---
# Editor integration: scene graph and entity hierarchy (Phase 3)

Wire the LCRS scene graph into the editor entity system for hierarchical transforms. See ref/renderer_spec.md Phase 3.

Deliverables:
- Allocate scene_graph_t parallel to edit_entity_store at server startup
- Extend bridge on_spawn: call scene_graph_attach(graph, entity_idx, parent_idx) — parent from 'parent' arg on spawn command
- Extend bridge on_delete: call scene_graph_detach(graph, entity_idx) — reparents children
- New editor command 'parent' to reparent entity: parent <child_id> <parent_id> — updates LCRS tree + marks DIRTY_LOCAL
- New editor command 'unparent' to detach entity to root: unparent <entity_id>
- Add SCRIPT_KEY_PARENT (u32, key=17) to entity_attrs.h for parent entity ID
- IL support: set_field with key=17 to dynamically reparent at runtime (e.g., pickup attachment)
- scene_graph_update() called after physics tick and script updates, before draw list build
- World transforms from scene graph feed into draw commands (replaces per-body pos/rot)
- Joint chain entities auto-parent: when joint command creates a chain, parent child to root in scene graph
- Replication: send parent_id in spawn message so client builds matching hierarchy
- Tests for parent/unparent commands, dirty propagation, joint chain auto-parenting

Depends on: rpg-m3nv (draw list submission), rpg-o0a7 (scene graph implementation)

