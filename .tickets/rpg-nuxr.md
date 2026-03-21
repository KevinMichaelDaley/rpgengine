---
id: rpg-nuxr
status: closed
deps: [rpg-z9db]
links: []
created: 2026-03-17T06:48:33Z
type: feature
priority: 1
assignee: KMD
tags: [editor, skeleton, bone, gizmo, viewport]
---
# Bone rendering as capsules with transform gizmos

## Summary

Render skeleton bones as thin capsules from head to tail in the viewport, with full transform gizmo support. When an entity with a bound skeleton is selected, its bones are rendered as elongated capsules connecting each bone's head position to its tail position. Individual bones can be selected and transformed using the standard gizmo system, including multi-gizmo mode (T key, which shows gizmos on all selected bones simultaneously).

## Background

The skeleton system (`skeleton_def_t`) stores per-bone head positions (from rest transforms), tail positions (`tail_positions` array, 3 floats per joint), and parent indices. The `bone_collider_desc_t` system already defines capsule parameters per bone. The gizmo system (`scene_viewport_gizmo.c`, `scene_gizmo_per_object_draw.c`, `scene_gizmo_per_object_input.c`) already supports per-entity translate/rotate/scale gizmos with multi-object input.

What's missing: rendering bones as visual capsules, bone selection, and wiring bone transforms into the gizmo system.

## Deliverables

### Bone Capsule Rendering
- New overlay pass (or extension of existing overlay): `scene_viewport_bone_overlay.c`
- For each entity with a bound skeleton:
  - Iterate all joints in the skeleton
  - Compute capsule geometry: center = midpoint(head, tail), axis = normalize(tail - head), length = distance(head, tail), radius = proportional to length (e.g., length * 0.08, clamped to min/max)
  - Render as wireframe capsule (or solid semi-transparent capsule) using the flat shader
  - Color coding: unselected bones = light blue, selected bones = bright yellow, active bone = white
- Root bones (parent == UINT32_MAX) render from the bone's head to its tail
- Child bones render from parent's tail (their head) to their own tail
- Bones in bind pose initially; when animation is wired, they render in posed space

### Bone Selection
- Click on a bone capsule in the viewport selects that bone
- Selection state: extend `edit_selection_t` or add a parallel `bone_selection_t` that tracks (entity_id, bone_index) pairs
- Multi-select bones: Shift+click adds to bone selection
- Select-all bones: Ctrl+A when a skeleton entity is active
- Inspector switches to bone properties when a bone is selected (head/tail pos, collision shape, mass, joint type)

### Gizmo Integration
- When bones are selected, gizmos appear on bone heads (translate) or bone bodies (rotate/scale)
- T key toggles multi-gizmo mode: shows individual gizmos on ALL selected bones simultaneously (vs. single combined gizmo at selection centroid)
- Gizmo transforms modify the bone's rest pose in the skeleton definition
- Constraints: bone head movement propagates to child bone heads (parent-child chain)
- Undo/redo: bone transform changes are recorded as undo entries

### Key Files
- NEW: `src/editor/scene/scene_viewport_bone_overlay.c` — bone capsule rendering
- MODIFY: `src/editor/scene/scene_viewport_draw.c` — call bone overlay after entity draw
- MODIFY: `src/editor/scene/scene_input.c` — bone pick/select on click
- MODIFY: `src/editor/scene/scene_viewport_gizmo.c` — gizmo on bone heads
- MODIFY: `src/editor/scene/scene_gizmo_per_object_draw.c` — multi-gizmo for bones
- MODIFY: `src/editor/scene/scene_ui_inspector.c` — bone property display
- NEW: `include/ferrum/editor/edit_bone_selection.h` — bone selection state

## Acceptance Criteria
- [ ] Selecting an entity with a skeleton shows bones as thin capsules in the viewport
- [ ] Clicking a bone capsule selects it (highlighted in yellow/white)
- [ ] Shift+click adds bones to multi-selection
- [ ] Gizmo appears on selected bone head; translate moves the bone
- [ ] T key shows gizmos on all selected bones simultaneously
- [ ] Inspector shows bone properties when a bone is selected
- [ ] Bone transforms update the skeleton rest pose
- [ ] Undo/redo works for bone transforms
- [ ] Bones render in correct hierarchy (parent tail → child head chain)

