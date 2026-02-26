---
id: rpg-95es
status: open
deps: [rpg-00d1, rpg-pc4k]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Editor I/O thread (edit_io_thread.c)

Implement the dedicated editor I/O thread that handles all TCP connections for the editor subsystem (edit protocol + asset downloads).

READ FIRST: ref/editor_design.md §2.3 for epoll architecture, ref/editor_spec.md §2.1 for threading contract, ref/editor_design.md §11 for threading model.

This thread is the TCP gateway. It runs an epoll event loop, accepts connections, reads/parses JSON commands, pushes them into the SPSC command ring, and reads responses from the response ring to send back.

Requirements:
- Dedicated pthread, started by editor_ctx_init()
- epoll-based event loop (Linux)
- Accepts edit protocol connections on configurable port (default 9100)
- Accepts asset download connections on configurable port (default 9101)
- Reads newline-delimited JSON from edit clients
- Parses JSON into command structs using json_parse
- Pushes commands into SPSC command ring
- Reads response ring and writes JSON responses to correct client fd
- Handles multiple simultaneous edit clients (though typically just one)
- Graceful shutdown via atomic flag
- Must never touch world state or entity data directly

Files to create:
- include/ferrum/editor/edit_io_thread.h
- src/editor/io/edit_io_thread.c
- src/editor/io/edit_io_accept.c (connection accept handling)

Dependencies: json_parse, edit_cmd_ring

