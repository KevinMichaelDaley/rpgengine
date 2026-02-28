---
id: rpg-37uq
status: open
deps: [rpg-6fi0]
links: []
created: 2026-02-28T22:20:21Z
type: epic
priority: 1
assignee: KMD
tags: [editor, mesh, csg, integration]
---
# Phase 4: CSG, Polish & Integration

CSG operations, mesh data transfer, undo integration, TUI mesh mode, and brush-based creation. This phase polishes the mesh modeling system and integrates it with the broader editor.

READ FIRST: ref/mesh_modeling_spec.md §Brush/CSG Commands, §Data Transfer Commands, §TUI/CLI Interface

Clip and CSG operations provide constructive solid geometry workflows (hollow, merge, subtract, intersect). Data transfer commands enable import/export and clipboard operations. mesh_commit bakes editable meshes into permanent world entities. Full undo/redo support for all mesh operations. TUI gains mesh-mode keybindings and visual feedback.

Key considerations:
- CSG operations require robust boolean mesh operations (intersect, subtract, union)
- Clip tool splits mesh along an arbitrary plane, keeping front/back/both
- mesh_commit freezes editable geometry into a static mesh entity
- Undo must snapshot mesh state efficiently (delta compression if large)
- TUI keybindings are modal: different keys active in different selection modes

