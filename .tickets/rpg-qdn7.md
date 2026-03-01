---
id: rpg-qdn7
status: closed
deps: []
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, build]
---
# LuaJIT 2.1 build integration (third_party/luajit/)

Integrate LuaJIT 2.1 into the project build system.

READ FIRST: ref/editor_design.md §10.2 for LuaJIT build integration (Makefile targets, static lib).

Requirements:
- Clone or vendor LuaJIT 2.1 source into third_party/luajit/
- Makefile target to build libluajit.a (static library)
- LUAJIT_ENABLE compile flag gates all LuaJIT code
- Link editor_server against libluajit.a + libm + libdl
- Verify LuaJIT builds cleanly with project CC and CFLAGS
- Must work on Linux x86_64 (ARM64 nice-to-have)
- Do NOT include LuaJIT's standalone interpreter (lua, luac) in the build

Files to create/modify:
- third_party/luajit/ (vendored source)
- Makefile additions for LUAJIT targets
- tests/editor/luajit_smoke_test.c (basic init/eval/shutdown)

