---
id: rpg-v61z
status: open
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-zwcc
---
# §2 Engine: Lock Table

See ref/scene_editor_design.md Engine-Side Work table. Server-side lock table in src/editor/protocol/edit_lock_table.c.

## Acceptance Criteria

Server validates mutations against locks. Unauthorized edits rejected. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

