---
id: rpg-uuov
status: open
deps: [rpg-frx0]
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-c55w
---
# §3.2 Sculpt Mode

See ref/scene_editor_design.md §3.2. brush_engine_t with per-vertex influence. 8 sculpt tools (grab/smooth/flatten/inflate/crease/pinch/clay strips/draw). Brush cursor rendering. Radius/strength adjustment. Symmetry (X/Y/Z mirror). Local vertex delta cache. Batch flush to server. Masking (face set/color/vertex group/texture/stencil).

## Acceptance Criteria

All 8 sculpt tools work with brush cursor. Symmetry mirrors across axes. Masking constrains sculpt. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

