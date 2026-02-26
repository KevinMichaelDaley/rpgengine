---
id: rpg-89t3
status: open
deps: [rpg-d6j3]
links: []
created: 2026-02-26T04:27:43Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, controller]
---
# Tab-completion for asset paths (async)

Implement async tab-completion in the controller, including server-side completion queries.

READ FIRST: ref/editor_design.md §9.5 for tab completion engine (async UX, stale handling), ref/editor_ux.md §3.2 for tab completion behavior, ref/editor_design.md §2.5 for completion protocol.

Requirements:
- Controller sends 'complete' command to server with context string
- Server returns candidates from: command names (local), asset paths (registry), entity IDs, component names
- Controller shows [...] loading indicator while waiting for server response
- Stale response handling: if user types more, new request issued, old response discarded (matched by request_id)
- Tab cycles forward, Shift+Tab backward
- Popup shows all candidates when ≤20 matches
- Local command name completion (no server roundtrip needed)

Files to create:
- src/editor/controller/ctrl_complete.c
- src/editor/commands/cmd_complete.c (server-side handler)
- tests/editor/ctrl_complete_tests.c

