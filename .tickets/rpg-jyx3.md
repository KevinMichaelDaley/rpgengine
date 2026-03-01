---
id: rpg-jyx3
status: closed
deps: [rpg-qdn7]
links: []
created: 2026-03-01T05:35:29Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script sandbox (script state isolation)

Implement the script sandbox that strips dangerous libraries from the script script state.

READ FIRST: ref/editor_design.md §6.7 for safety constraints, §6.5 for engine scripting language integration.

Requirements:
- script_sandbox_init(L) — called after luaL_openlibs to remove: os, io, loadlib/package, debug, ffi
- Whitelist only: base, string, table, math, coroutine, bit (engine scripting language bit ops)
- Custom allocator wrapper that enforces arena memory limit (8 MB default)
- script_sandbox_alloc(ud, ptr, osize, nsize) — scripting allocator function backed by arena
- Verify that pcall/xpcall still work (needed for error handling)
- Verify that coroutine.create/resume/yield work (needed for multi-frame scripts)

Files to create:
- src/editor/script/edit_script_sandbox.c (sandbox_init, arena allocator)
- tests/editor/edit_script_sandbox_tests.c


## Notes

**2026-03-01T07:06:22Z**

SUPERSEDED: LuaJIT removed from project. Sandbox will be reimplemented for the engine scripting language (TBD).
