---
id: rpg-mnnh
status: closed
deps: [rpg-pf3x, rpg-00d1]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, controller]
---
# Controller to server TCP connection

Implement the controller's TCP client that connects to the server's edit protocol port.

READ FIRST: ref/editor_design.md §2 for edit protocol details, ref/editor_spec.md §5.1 for protocol format, ref/editor_ux.md §3 for command-line interface.

The controller sends JSON commands to the server and receives JSON responses. Commands typed in the command-line are serialized to JSON and sent over TCP. Responses are parsed and displayed in the log area.

Requirements:
- TCP client connect to server_host:server_port (configurable via CLI args)
- Non-blocking TCP I/O integrated into poll() event loop
- Send JSON command on Enter in command mode
- Receive and parse JSON responses, display results in log area
- Handle connection loss gracefully (reconnect or error message)
- Correlate responses to requests via id field
- Command-line argument: --server host:port

Files to create:
- src/editor/controller/ctrl_server_conn.c
- tests/editor/ctrl_server_conn_tests.c

Dependencies: json_parse, ctrl_tui

