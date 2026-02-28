---
id: rpg-xg7p
status: closed
deps: [rpg-2zmv, rpg-00d1]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, client]
---
# Client state socket (bidirectional TCP)

Implement the bidirectional TCP state socket on the client that the controller connects to.

READ FIRST: ref/editor_design.md §4.2 for cursor synchronization architecture, ref/editor_spec.md §5.2 for client state protocol (push events), ref/editor_spec.md §7.2 for client module table.

The client listens on a TCP port (default 9200). The controller connects and can query/command cursor, camera, and selection state. The client PUSHES events to the controller for viewport interactions.

Requirements:
- TCP listener on client_state_port (configurable, default 9200)
- Non-blocking reads in SDL event loop (polled alongside SDL_PollEvent)
- Receives from controller: cursor commands, camera commands, selection commands, grab_begin/grab_end
- Pushes TO controller: cursor_moved (after raycast), entity_clicked (click on entity), context_menu (right-click), box_select (drag-select complete)
- JSON format matching edit protocol style (newline-delimited)
- Command-line argument: --state-port port
- Gated behind EDITOR_ENABLE

Files to create:
- include/ferrum/editor/client/client_state_socket.h
- src/editor/client/client_state_socket.c
- src/editor/client/client_editor_input.c (mouse input → push events)

Dependencies: json_parse, 3D cursor

