---
id: rpg-vovh
status: closed
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-rkj9
---
# §1.3 Inspector Panel

See ref/scene_editor_design.md §1.3. Clay-based widget library (float/int fields, vec3/quat, dropdown, checkbox, text, color picker, collapsible sections). Entity inspector (transform, mesh, physics, materials, layer stack stub, attributes). Multi-select inspector. Property edits send to server. Shared context menu component (clay_context_menu.c/.h).

## Acceptance Criteria

Inspector shows selected entity properties. Edits send to server. Multi-select shows shared/mixed values. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

