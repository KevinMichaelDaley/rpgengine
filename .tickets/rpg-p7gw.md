---
id: rpg-p7gw
status: open
deps: [rpg-5ifh]
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-1iyt
---
# §0.4 Persistence & Sync

See ref/scene_editor_design.md §0.4. Google Drive-style sync: track in-flight edit commands and server acks, TUI status bar sync indicator, :save force command, :save status command, offline queue (buffer edits during disconnect, replay on reconnect), Ctrl+S mapped to :save force.

## Acceptance Criteria

Sync status shows in TUI status bar. :save force and :save status work. Offline queue buffers edits during disconnect and replays on reconnect. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

