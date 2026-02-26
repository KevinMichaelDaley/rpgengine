---
id: rpg-wzgk
status: closed
deps: []
links: []
created: 2026-02-26T03:49:32Z
type: feature
priority: 1
assignee: KMD
tags: [editor, architecture, documentation]
---
# Level Editor Architecture - Spec, UX, and Design Documents

Write three reference documents for the level editor system:

1. ref/editor_spec.md - Architectural specification for the MVC-based level editor:
   - Model: persistent server (existing physics/entity simulation)
   - View: client in editor mode (3D cursor, camera, asset preview)
   - Controller: curses-style terminal process connected via TCP edit socket
   - Procedural-first design with full scripting support
   - MCP server for AI agent control

2. ref/editor_ux.md - UX specification:
   - Keyboard-driven 3D cursor on a grid
   - Command-line at bottom with tab-completion and asset browsing
   - Keybinding system
   - Script-based texture synthesis with bake-to-UV support
   - Workflow examples

3. ref/editor_design.md - Implementation design:
   - TCP edit protocol between controller and server
   - Asset downloader (TCP) for server→client asset transfer
   - 3D cursor synchronization
   - Script engine integration
   - MCP server protocol
   - File/module layout

All documents should account for the existing ferrum engine architecture (fiber job system, RUDP networking, physics world, OpenGL 4.6 renderer).

