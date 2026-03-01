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
# engine scripting language build integration (third_party/scripting/)

Integrate engine scripting language into the project build system.

READ FIRST: ref/editor_design.md §10.2 for engine scripting language build integration (Makefile targets, static lib).

Requirements:
- Clone or vendor engine scripting language source into third_party/scripting/
- Makefile target to build libscript.a (static library)
- SCRIPTING_ENABLE compile flag gates all engine scripting language code
- Link editor_server against libscript.a + libm + libdl
- Verify engine scripting language builds cleanly with project CC and CFLAGS
- Must work on Linux x86_64 (ARM64 nice-to-have)
- Do NOT include engine scripting language's standalone interpreter (lua, luac) in the build

Files to create/modify:
- third_party/scripting/ (vendored source)
- Makefile additions for LUAJIT targets
- tests/editor/scripting_smoke_test.c (basic init/eval/shutdown)


## Notes

**2026-03-01T07:06:22Z**

SUPERSEDED: LuaJIT removed from project. Scripting language TBD.
