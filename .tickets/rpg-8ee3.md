---
id: rpg-8ee3
status: open
deps: []
links: []
created: 2026-03-21T06:22:02Z
type: bug
priority: 2
assignee: kmd
---
# Bone rotation axis mismatch in regular edit mode

When selecting a bone rotation ring (e.g., blue=Z) and dragging in regular edit mode, the bone rotates around the wrong axis. Only affects bones in regular mode — entity rotation and prefab mode bone rotation work correctly.

## Likely cause

The world-to-local transform at `scene_input.c:727` converts the gizmo delta from world space to parent-bone local space using `sk->rest_world[parent_idx]`. In regular mode, `sk` wraps the bone_pose_block's arrays. The rest_world may not correctly reflect the cumulative transform, causing the local-space conversion to use the wrong parent rotation.

## Key code

- `src/editor/scene/scene_input.c` lines 690-729
- `src/editor/scene/bone_pose/bone_pose_store.c` — ensure clones rest_world
