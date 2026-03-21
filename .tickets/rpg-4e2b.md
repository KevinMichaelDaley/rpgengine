---
id: rpg-4e2b
status: open
deps: [rpg-b7bc]
links: [rpg-c6o7]
created: 2026-03-21T07:00:00Z
type: task
priority: 2
assignee: kmd
parent: rpg-c6o7
---
# §5.2.4 Joint type editor

Inspector panel for creating and editing physics joints between two constraint anchors.

## Joint Creation Workflow

1. Select first anchor (click in viewport or pick from inspector dropdown)
2. Select second anchor (on a different bone)
3. Choose joint type from dropdown
4. Joint created with default parameters; inspector shows per-type panel

## Per-Type Parameter Panels

| Joint Type | Parameters |
|---|---|
| Distance | rest_length, min_distance, max_distance, stiffness, damping |
| Ball | stiffness, damping (free rotation, no axis) |
| Hinge | axis (local), angle_min, angle_max, stiffness, damping |
| Lock | break_force, break_torque (no free DOF) |
| Twist | axis, twist_min, twist_max, stiffness, damping |
| Cone-twist | swing1_limit, swing2_limit, twist_limit (3-axis) |
| Copy-rotation | source_bone, influence (0-1), axis_mask |
| Limit-rotation | per-axis min/max angles |
| Limit-position | per-axis min/max offsets |
| Aim | target_bone, up_axis, influence |
| IK | target, pole_target, chain_length, iterations |

## Common Parameters (all types)

- Stiffness, damping, compliance
- Drive mode (off / position / velocity), drive target, max_force
- Break force / break torque (0 = unbreakable)
- Enable/disable toggle
- Animation damping factor (blend between animation and physics)

## Constraint Stack

- Each bone pair can have multiple constraints (stacked)
- Constraint list in inspector with drag-to-reorder
- Per-constraint enable/disable checkbox
- Delete button per constraint

## Commands

- `joint_create <type> <anchor_a> <anchor_b>` — create joint
- `joint_delete <joint_id>` — remove joint
- `joint_edit <joint_id> <param> <value>` — modify parameters
- `joint_list` — list all joints
- `joint_types` — list available types with descriptions

## Key files

- src/editor/scene/scene_ui_bone_inspector.c — joint type panels
- src/editor/commands/cmd_joint.c — existing joint command (extend)
- include/ferrum/animation/constraint_params.h — constraint_def_t, bone_joint_desc_t
- Wire through physics bridge to phys_joint_* APIs
