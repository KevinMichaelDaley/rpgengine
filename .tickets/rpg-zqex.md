---
id: rpg-zqex
status: open
deps: [rpg-qdn7]
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script runtime core (edit_script_runtime.c)

Implement the core script runtime that executes LuaJIT on the main tick thread.

READ FIRST: ref/editor_design.md §6.1-6.2 for runtime architecture (script_runtime_t, execution model, budget hook), ref/editor_spec.md §2.5 for concurrency model.

Requirements:
- script_runtime_t: lua_State, editor back-pointer, instruction_budget (100K default), continuation_pending flag
- script_runtime_init(editor_ctx) → allocate Lua state with custom arena allocator (8 MB limit)
- script_runtime_tick(rt) → called during drain, resumes pending coroutines
- Budget hook via lua_sethook: forces lua_yield when budget exhausted
- Sandbox: remove os, io, loadlib, debug, ffi libraries
- script_runtime_eval(rt, code, result) → run code, return result as JSON
- script_runtime_load_file(rt, path) → load and start executing a script file
- Multi-frame scripts: create Lua coroutine, yield/resume across ticks
- script_runtime_shutdown(rt) → close Lua state, free arena

Files to create:
- include/ferrum/editor/edit_script_runtime.h
- src/editor/script/edit_script_runtime.c
- src/editor/script/edit_script_sandbox.c
- tests/editor/edit_script_runtime_tests.c

