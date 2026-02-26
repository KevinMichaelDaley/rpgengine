---
id: rpg-6lii
status: open
deps: [rpg-ghd5]
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server, controller]
---
# REPL mode with continuation detection

Implement the interactive Lua REPL mode with server-side continuation detection.

READ FIRST: ref/editor_design.md §6.3 for REPL continuation detection (script_is_complete using luaL_loadstring), ref/editor_ux.md §8.2 for REPL UX flow.

Requirements:
- cmd_repl on server: toggles REPL mode for the connection
- In REPL mode, each line is tested with luaL_loadstring for completeness
- Incomplete input returns {"status": "incomplete"} → controller shows '...>' prompt
- Controller accumulates lines locally during continuation
- Complete input is executed and result returned
- Syntax errors shown in red with line number
- Runtime errors include traceback
- Exit REPL with 'exit()' or Ctrl+D
- REPL state persists across lines (local variables survive)

Files to create:
- src/editor/commands/cmd_repl.c
- src/editor/script/edit_script_repl.c
- src/editor/controller/ctrl_repl.c (controller-side REPL state)
- tests/editor/edit_script_repl_tests.c

