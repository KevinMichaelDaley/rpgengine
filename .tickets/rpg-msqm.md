---
id: rpg-msqm
status: open
deps: []
links: []
created: 2026-02-26T04:30:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-b5ma
tags: [editor, mcp, controller]
---
# MCP server (JSON-RPC 2.0 over TCP, port 9300)

Implement the MCP server as a TCP listener in the controller process.

READ FIRST: ref/editor_design.md §8 for full MCP architecture (TCP listener, tool mapping, resource mapping, distributed setup), ref/editor_spec.md §4.5 for MCP spec, ref/editor_ux.md §10 for MCP interface.

CRITICAL: MCP uses JSON-RPC 2.0 over a dedicated TCP socket. NEVER stdio. The AI agent connects from any machine on the network.

Requirements:
- mcp_server_t: TCP listen socket on configurable port (default 9300)
- Newline-delimited JSON-RPC 2.0 messages
- Integrated into controller poll() event loop (no extra thread)
- Tool calls map to edit protocol commands (spawn, delete, move, etc.)
- Resource reads map to state queries (entities, cursor, assets)
- Support single AI client connection (reject additional connections)
- Handle disconnect/reconnect gracefully
- Command-line argument: --mcp-port port
- Distributed setup: controller on machine C, AI agent on machine D

Files to create:
- include/ferrum/editor/mcp/mcp_server.h
- src/editor/mcp/mcp_server.c (TCP listener + message dispatch)
- src/editor/mcp/mcp_tools.c (tool handlers: spawn, delete, move, run_script, etc.)
- src/editor/mcp/mcp_resources.c (resource handlers: entities, cursor, assets)
- src/editor/mcp/mcp_jsonrpc.c (JSON-RPC 2.0 parsing/serialization)
- tests/editor/mcp_server_tests.c

