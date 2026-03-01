---
id: rpg-xqkt
status: closed
deps: [rpg-ec5e]
links: []
created: 2026-03-01T05:36:23Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, controller]
---
# REPL mode with continuation detection

Implement REPL mode in the TUI controller for interactive scripting scripting with server-side continuation detection.

READ FIRST: ref/editor_design.md §6.6 for continuation detection logic, ref/editor_ux.md for REPL workflow.

Requirements:
- 'repl' command enters REPL mode (changes TUI prompt to 'lua> ')
- In REPL mode, input is sent as eval requests to the server
- Server-side continuation detection: script_loadstring → check for '<eof>' in error message
- If incomplete: server returns {"status":"incomplete"}, controller shows '...> ' continuation prompt
- Controller accumulates lines until complete, then sends full block
- 'exit' or Ctrl-D exits REPL mode, returns to normal command mode
- Results printed to TUI log (formatted script values)
- Errors printed with line numbers relative to REPL input

Files to create:
- src/editor/controller/ctrl_repl.c (REPL mode state machine)
- src/editor/script/edit_script_complete.c (continuation detection on server)
- tests/editor/ctrl_repl_tests.c


## Notes

**2026-03-01T09:58:49Z**

Superseded by Aegis VM implementation. See ref/aegis_bytecode_spec.md.
