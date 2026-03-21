---
id: rpg-8ee1
status: open
deps: [rpg-21zg]
links: []
created: 2026-03-21T04:06:11Z
type: task
priority: 2
assignee: kmd
---
# Show bones in outliner as children of skeleton entities (regular mode)

Currently in regular edit mode, bones appear in the inspector panel's bone list (`scene_ui_bone_list.c`). They should instead appear in the outliner as expandable children of entities that have a `SCRIPT_KEY_SKEL_PATH` attribute, matching how bones are shown in the prefab outliner (`prefab_ui_outliner.c`).

## Current state

- Prefab mode: bones shown hierarchically in `scene_ui_build_prefab_outliner()` with expand/collapse, click-to-select, shift-click multi-select
- Regular mode: bones shown in inspector panel via `scene_ui_build_bone_list()` — flat list, separated from the outliner

## Deliverables

- Add an expand/collapse triangle to skeleton entities in the regular outliner (`scene_ui_outliner.c`)
- When expanded, render indented bone rows (name, index) as children of the entity row
- Clicking a bone row selects it via `edit_bone_selection_add/toggle`
- Shift-click for multi-select (same as prefab outliner)
- Remove or gate `scene_ui_build_bone_list()` from the inspector (avoid duplicate display)
- Account for bone rows in scroll/total count calculations
- Implement using the LCRS scene tree from rpg-21zg — bones are child nodes of their skeleton entity in the hierarchy
- Reuse pattern from `prefab_ui_outliner.c` bone rendering

## Notes

Blocked on rpg-21zg (LCRS scene tree). Bones should be LCRS children of skeleton entities, not a separate display mechanism.
