---
id: rpg-8qnd
status: open
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-rkj9
---
# §1 Engine: pivot_offset Entity Field

See ref/scene_editor_design.md Engine-Side Work table. Add pivot_offset to entity fields in src/game/entity/entity_fields.c. Server-side entity field for storing per-entity pivot offset.

## Acceptance Criteria

pivot_offset field exists on entities. Server stores and replicates it. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

