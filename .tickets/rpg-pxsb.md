---
id: rpg-pxsb
status: open
deps: []
links: []
created: 2026-03-12T06:48:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-h6z6
---
# §9.2 LOD and Async Loading

See ref/scene_editor_design.md §9.2. LOD levels per static mesh (precomputed decimation). Screen-space size calculation for LOD selection. Async mesh load (background thread). Async texture load (background mip levels). GPU upload queue (budgeted per frame).

## Acceptance Criteria

LOD levels select based on screen-space size. Async loading prevents frame hitches. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

