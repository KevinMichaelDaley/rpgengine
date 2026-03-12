---
id: rpg-p18w
status: open
deps: []
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-o25i
---
# §6 Engine: Velocity Derivation + Damping

See ref/scene_editor_design.md Engine-Side Work table. Velocity derivation for kinematic bones (src/physics/animated/kinematic_velocity.c) and animation damping factor (bone_joint_desc_t.anim_damping field).

## Acceptance Criteria

Kinematic velocity derivation works. Damping factor field exists on joints. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

