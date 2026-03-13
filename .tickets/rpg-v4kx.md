---
id: rpg-v4kx
status: closed
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-1iyt
---
# §0 Engine: Server Auto-Sync

See ref/scene_editor_design.md Engine-Side Work table. Server auto-sync (world persistence) in src/editor/protocol/edit_autosave.c. Server-side component that auto-saves world state to disk and handles :save force flush requests.

## Acceptance Criteria

Server auto-saves world state. Flush requests from editor trigger immediate save. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

