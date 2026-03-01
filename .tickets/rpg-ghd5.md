---
id: rpg-ghd5
status: open
deps: [rpg-zqex, rpg-96oo]
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Run and eval commands

Implement the run and eval commands for executing scripts from the command line.

READ FIRST: ref/editor_design.md §2.4 dispatch table (cmd_run, cmd_eval entries), ref/editor_ux.md §8.1 for running scripts.

Requirements:
- cmd_eval: evaluate inline scripting expression, return result in JSON response
- cmd_run: load and execute script file, with optional args passed as global table
- Both go through script_runtime, so they are instruction-budgeted
- Long scripts become multi-frame coroutines; server sends intermediate 'running' status
- Script output (print()) is captured and sent as log messages to controller
- Error messages include script traceback

Files to create:
- src/editor/commands/cmd_run.c
- src/editor/commands/cmd_eval.c
- tests/editor/cmd_script_tests.c

