---
id: rpg-21zg
status: closed
deps: [rpg-97do]
links: [rpg-ebpv, rpg-bhqa]
created: 2026-03-13T06:44:03Z
type: task
priority: 2
assignee: KMD
parent: rpg-1iyt
---
# §0.6 Scene Tree — LCRS outliner with collapse/expand, multi-select, prefabs

Add an entity-ID-based left-child right-sibling (LCRS) binary tree to the scene editor outliner, replacing the current flat entity list. The renderer already has an LCRS scene_graph_t (src/renderer/scene/); this ticket wires a parallel structure into the editor entity store and outliner UI.

## Deliverables

### Data Structure
- Add parent/first_child/next_sibling uint32_t fields to edit_entity_t (or parallel array)
- Entity IDs are tree node IDs (no separate node allocation)
- Root entities have parent=UINT32_MAX
- Maintain LCRS invariants on spawn, delete, reparent

### Outliner UI (Clay)
- Hierarchical indented display with expand/collapse triangles
- Collapsed nodes hide all descendants
- Expand/collapse state persisted per-entity (bitfield or parallel bool array)
- Keyboard: arrow keys navigate, Left collapses/goes to parent, Right expands/goes to first child
- Click triangle to toggle expand/collapse

### Multi-Select + Children
- Shift-click to select range in visible tree order
- Ctrl-click to toggle individual selection
- Select All Children command: select entity + all descendants (recursive LCRS walk)
- Delete with children selected removes entire subtree

### Prefab Save/Load
- Save selected entity and all children as a prefab (.prefab JSON)
- Prefab stores relative transforms (child pos/rot/scale relative to root)
- Load prefab: instantiate all entities, re-link LCRS parent/child, place at cursor
- Prefab references stored in entity_attrs (SCRIPT_KEY for prefab source path)

### Commands
- parent <child_id> <parent_id> — reparent entity in LCRS tree
- unparent <entity_id> — detach to root, children stay attached
- collapse <entity_id> / expand <entity_id> — toggle outliner visibility
- save_prefab <name> — save selection + children as prefab
- load_prefab <name> [x y z] — instantiate prefab at position

### Integration
- Wire into existing scene_graph_t from renderer for transform propagation
- Bridge callbacks (on_spawn, on_delete) maintain LCRS consistency
- Undo/redo support for reparent operations
- Replication: parent_id sent in spawn messages

## Links
- rpg-ebpv: Phase 3 scene graph integration (backend transforms)
- rpg-bhqa: Prefab system (asset registry side)
- rpg-o0a7: Existing LCRS scene_graph_t implementation

## Acceptance Criteria

- [ ] LCRS tree maintained on spawn/delete/reparent
- [ ] Outliner shows indented hierarchy with collapse/expand
- [ ] Collapse hides all descendants in outliner
- [ ] Arrow key navigation respects tree structure
- [ ] Multi-select: shift-click range, ctrl-click toggle
- [ ] Select All Children selects entity + all descendants
- [ ] Delete subtree removes entity and all children
- [ ] save_prefab serializes selection subtree with relative transforms
- [ ] load_prefab instantiates prefab at position with correct hierarchy
- [ ] parent/unparent commands update LCRS links
- [ ] Undo/redo works for reparent and prefab operations
- [ ] Tests for LCRS invariants, collapse/expand, prefab round-trip

