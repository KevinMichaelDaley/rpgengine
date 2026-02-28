# Client State Socket (rpg-xg7p)

## Problem
The editor client needs a bidirectional TCP socket so the controller (TUI) can
query/command cursor, camera, and selection state, and the client can push
viewport events (clicks, box-select, context menu) back to the controller.
This unblocks camera commands, grab mode, and full editor interactivity.

## Approach
Mirror the existing `ctrl_conn` pattern but reversed: the **client listens**,
the **controller connects**. Same newline-delimited JSON protocol.
Single-threaded, non-blocking I/O polled in the client's main loop.

## Message Format
- **Controller → Client commands:** `{"cmd":"set_cursor","pos":[x,y,z]}\n`
- **Controller → Client queries:** `{"query":"cursor"}\n`
- **Client → Controller push events:** `{"event":"entity_clicked","entity":42,"pos":[x,y,z]}\n`

## Todos

- [ ] **tests-state-socket** — Write tests for state socket lifecycle, send/recv, queries
- [ ] **state-socket-header** — Create client_state_socket.h (types + API)
- [ ] **state-socket-impl** — Create client_state_socket.c (listen, accept, poll, recv, send)
- [ ] **state-socket-dispatch** — Create client_state_dispatch.c (parse incoming JSON, route to cursor/camera)
- [ ] **tests-editor-input** — Write tests for mouse input → push events
- [ ] **editor-input-impl** — Create client_editor_input.c (mouse raycast → push events)
- [ ] **verify** — Build, run all tests, commit and push

## Files to Create
- `include/ferrum/editor/client/client_state_socket.h`
- `src/editor/client/client_state_socket.c` (listen/accept/poll/send — 4 functions)
- `src/editor/client/client_state_dispatch.c` (recv line parse + command routing — ≤4 functions)
- `src/editor/client/client_editor_input.c` (mouse → push events)
- `tests/editor/client_state_socket_tests.c`
- `tests/editor/client_editor_input_tests.c`

## Notes
- Port default 9200, configurable via `--state-port`
- Gated behind EDITOR_ENABLE
- Non-blocking: must never stall rendering
- Single client connection at a time (controller)
- Push events: cursor_moved, entity_clicked, context_menu, box_select
