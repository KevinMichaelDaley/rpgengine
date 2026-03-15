---
id: rpg-nnfd
status: open
deps: []
links: []
created: 2026-02-26T16:34:26Z
type: task
priority: 2
assignee: KMD
tags: [editor, physics]
---
# Editor physics constraints (joints/hinges/springs)

## Overview

Add a complete editor workflow for creating and managing physics constraints between entities. The physics engine already has 11 joint types (phys-800 series): distance, ball, hinge, lock, twist, cone-twist, copy-rotation, limit-rotation, limit-position, aim, IK.

## Constraint Anchors

Each entity can have one or more **constraint anchors** — local-space attachment points where constraints connect. Anchors are positioned and oriented relative to the entity, similar to how pivots work.

- Anchors are per-entity, stored alongside entity data
- Anchors can be created, moved, rotated, and deleted via gizmo interaction (same as pivot/cursor manipulation)
- Each anchor has: local position, local orientation, optional name
- Viewport renders anchors as small markers (e.g. diamonds) on their parent entity
- Anchors are selectable in the viewport — clicking an anchor selects it for manipulation or constraint creation

## Constraint Creation

Constraints are created between two selected anchors (one on each entity):

1. Select first anchor (click in viewport)
2. Select second anchor (shift-click or dedicated "connect" mode)
3. Choose constraint type from available types
4. Constraint is created between the two anchors with default parameters

Commands:
- `anchor_create <entity_id>` — add anchor at entity origin (or cursor position in local space)
- `anchor_delete <anchor_id>` — remove anchor
- `anchor_list [entity_id]` — list anchors on entity
- `constraint_create <type> <anchor_a> <anchor_b> [params]` — create constraint between selected anchors
- `constraint_delete <id>` — remove constraint
- `constraint_edit <id> <param> <value>` — modify limits/stiffness/damping
- `constraint_list` — list all constraints
- `constraint_types` — list available constraint types with descriptions

## Viewport Rendering

Constraints are rendered as visual relations between their anchor points:

- Lines/curves connecting the two anchors, color-coded by constraint type
- Type-specific indicators (e.g. arc for hinge axis, sphere for ball, spring coil for distance)
- Selected constraints highlight (same selection system as entities)
- Constraints are selectable — clicking a constraint line/indicator selects it
- Selected constraints can be deleted (X/Delete) or edited (inspector panel)
- Broken/invalid constraints (e.g. deleted entity) rendered in red/dashed

## Supported Types

All 11 physics joint types: distance, ball, hinge, lock, twist, cone-twist, copy-rotation, limit-rotation, limit-position, aim, IK.

## Requirements

- Wire through physics bridge to phys_joint_* APIs
- Undo support for anchor and constraint create/delete/edit
- Constraints included in scene save/load and prefab serialization
- Constraint parameters editable in inspector panel

## Files

- `include/ferrum/editor/edit_anchor.h` — anchor types and store
- `include/ferrum/editor/edit_constraint.h` — constraint types and store
- `src/editor/state/edit_anchor_store.c` — anchor lifecycle
- `src/editor/state/edit_constraint_store.c` — constraint lifecycle
- `src/editor/commands/cmd_anchor.c` — anchor commands
- `src/editor/commands/cmd_constraint.c` — constraint create/delete/edit
- `src/editor/commands/cmd_constraint_list.c` — list constraints and types
- `src/editor/scene/scene_viewport_constraints.c` — viewport rendering of anchors and constraints
- `tests/editor/cmd_anchor_tests.c`
- `tests/editor/cmd_constraint_tests.c`

