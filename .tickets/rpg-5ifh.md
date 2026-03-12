---
id: rpg-5ifh
status: open
deps: [rpg-97do]
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-1iyt
---
# §0.3 Server Connection

See ref/scene_editor_design.md §0.3. TCP connection to server edit socket, UDP connection for replication snapshots, command send/receive (JSON over TCP), entity state receive (snapshots over UDP), connection status in TUI status bar.

## Acceptance Criteria

Editor connects to running server. Commands are sent and responses received. Entity state snapshots arrive via UDP. Connection status visible in TUI. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

