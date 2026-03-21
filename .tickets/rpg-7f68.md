---
id: rpg-7f68
status: closed
deps: []
links: [rpg-4418, rpg-8ee1]
created: 2026-03-21T05:53:29Z
type: task
priority: 2
assignee: kmd
---
# Prefab mode bone edits should persist as per-entity overrides on exit

When bone transforms are edited in prefab mode (P key), those edits modify the shared skeleton_def_t in the registry. On exiting prefab mode, the edited bone rest_local transforms should be copied into the bone_pose_store as per-entity overrides for that specific entity instance. Currently the edits are lost on exit because the shared skeleton reverts and no per-entity override is created.

## Deliverables

- On prefab mode exit (`prefab_mode_exit`), copy the modified rest_local/rest_world/tail_positions from the registry skeleton into a bone_pose_block_t for the prefab root entity
- Only create overrides for bones that actually changed (compare against original)
- Respect the existing bone_pose_store API (bone_pose_store_ensure + memcpy)
- The shared skeleton in the registry should be restored to its original state on exit (so other instances aren't affected)

## Key files

- `src/editor/scene/prefab/prefab_mode_enter.c` — could save original skeleton state on enter
- `src/editor/scene/scene_main.c` or wherever `prefab_mode_exit` is implemented
- `src/editor/scene/bone_pose/bone_pose_store.h` — per-entity override storage
