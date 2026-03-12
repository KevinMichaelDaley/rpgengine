---
id: rpg-rcyd
status: open
deps: []
links: []
created: 2026-03-12T06:48:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-h6z6
---
# §9.1 Disk Swap System

See ref/scene_editor_design.md §9.1. Swap-to-disk (serialize entity+mesh+textures, free memory). Swap-from-disk (background thread reads, main thread uploads). Bounding box retention (wireframe AABB). Outliner state (grayed-out, disk icon). Bulk swap (entire layer/group). Auto-swap (distance-based for static geometry).

## Acceptance Criteria

Swap objects to disk; wireframe AABBs remain visible. Swap back restores fully. Bulk swap works. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

