---
id: rpg-b5ma
status: open
deps: [rpg-vtza]
links: []
created: 2026-02-26T04:30:12Z
type: epic
priority: 2
assignee: KMD
tags: [editor, mcp, polish]
---
# Phase 5: Polish and MCP Server

Epic for polish features and the MCP server. This phase adds the MCP server (JSON-RPC 2.0 over TCP), full keybinding system, grab mode with client-side provisional positioning, grid/snap refinement, prefab system, and camera commands.

Before starting any subtask, read:
- ref/editor_spec.md §4.5 (MCP server, TCP-only, separate machine support)
- ref/editor_design.md §8 (MCP implementation: TCP listener, tool/resource mapping, distributed setup)
- ref/editor_ux.md §9 (keybinding system), §10 (MCP interface)

Key: MCP is JSON-RPC 2.0 over TCP (port 9300), NEVER over stdio. All four processes (server, client, controller, AI agent) can run on separate machines.

