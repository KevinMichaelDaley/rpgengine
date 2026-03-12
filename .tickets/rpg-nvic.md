---
id: rpg-nvic
status: open
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-zwcc
---
# §2.3 Multi-Editor Sync

See ref/scene_editor_design.md §2.3. editor_id assignment, edit broadcast receive, conflict resolution (last-write-wins), presence broadcast (camera at 2Hz), presence rendering (colored frustum wireframes + name labels), remote cursor rendering.

## Acceptance Criteria

Two editors see each other's cameras. Remote edits appear in real-time. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

