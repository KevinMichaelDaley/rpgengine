---
id: rpg-ec5e
status: open
deps: [rpg-zqex, rpg-96oo]
links: []
created: 2026-03-01T05:36:10Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# run/eval commands for script execution

Implement the 'run' and 'eval' editor commands that execute script code on the script runtime.

READ FIRST: ref/editor_design.md §6.5-6.6 for execution model and REPL continuation.

Requirements:
- 'eval <code>' command: enqueue script code string for one-shot execution on script thread, return result JSON
- 'run <path>' command: read script file from asset registry, enqueue for execution
- Both commands go through the edit command dispatch (registered like other commands)
- Script thread picks up eval requests from a queue (separate from cmd_ring — these are code strings, not entity operations)
- Result delivery: script thread writes result to a response slot, I/O thread reads and sends to controller
- Error handling: syntax errors and runtime errors returned as JSON with error field
- Instruction budget applies (100K default, configurable)

Files to create:
- src/editor/commands/cmd_eval.c (eval command handler)
- src/editor/commands/cmd_run.c (run command handler)
- tests/editor/cmd_eval_tests.c

