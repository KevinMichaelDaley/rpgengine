---
id: rpg-c6o7
status: open
deps: [rpg-haqg]
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-0n7d
---
# §5.2 Collision Body and Joint Setup

See ref/scene_editor_design.md §5.2. Inspector: collision body section (shape/radius/height/mass), auto-fit from mesh. Joint type dropdown with per-type panels. Cone twist (3-axis limits with viewport cones). Hinge, twist, ball socket, distance, lock, aim, copy/limit rotation/position. Joint physics (stiffness/damping/compliance/drive). Constraint stack with enable/disable. Animation damping factor.

## Acceptance Criteria

Inspector shows joint type with all parameters. Constraint limits visible as colored overlays. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

