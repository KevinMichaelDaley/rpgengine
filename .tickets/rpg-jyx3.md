---
id: rpg-jyx3
status: open
deps: [rpg-qdn7]
links: []
created: 2026-03-01T05:35:29Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script sandbox (Lua state isolation)

Implement the Lua sandbox that strips dangerous libraries from the script Lua state.

READ FIRST: ref/editor_design.md §6.7 for safety constraints, §6.5 for LuaJIT integration.

Requirements:
- script_sandbox_init(L) — called after luaL_openlibs to remove: os, io, loadlib/package, debug, ffi
- Whitelist only: base, string, table, math, coroutine, bit (LuaJIT bit ops)
- Custom allocator wrapper that enforces arena memory limit (8 MB default)
- script_sandbox_alloc(ud, ptr, osize, nsize) — Lua allocator function backed by arena
- Verify that pcall/xpcall still work (needed for error handling)
- Verify that coroutine.create/resume/yield work (needed for multi-frame scripts)

Files to create:
- src/editor/script/edit_script_sandbox.c (sandbox_init, arena allocator)
- tests/editor/edit_script_sandbox_tests.c

