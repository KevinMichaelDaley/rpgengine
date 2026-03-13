---
id: rpg-97do
status: in_progress
deps: [rpg-4d9g]
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-1iyt
---
# §0.2 Embedded TUI Panel

See ref/scene_editor_design.md §0.2. TUI panel with text buffer (scrollback ring buffer, colored spans), Clay-based text rendering, scrollback via CLAY_SCROLL, command-line input with cursor/selection/tab completion, status bar, key routing.

## Acceptance Criteria

TUI panel renders text, accepts commands, sends to server, shows responses. Colored spans render correctly. Scrollback works. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

