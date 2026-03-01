---
id: rpg-ped1
status: open
deps: [rpg-h84i, rpg-za2w]
links: []
created: 2026-03-01T21:28:02Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [aegis, editor, commands]
---
# Editor bridge command: send IL scripts to server

Add an editor command that sends Aegis IL script text to the server for compilation and execution.

The command reads IL source (from a file path or inline text), sends it over the editor TCP bridge as a JSON command, and the server-side handler compiles it via aegis_asm_compile() and loads it into the aegis_script_runtime.

Editor side (TUI command):
- `script load <path>` — read file, send contents as JSON command
- `script unload <name>` — send unload command by script name
- `script list` — query active scripts from server
- `script eval <inline_il>` — compile and run inline IL (one-shot, auto-exit)

JSON wire format:
  {"cmd": "script_load", "name": "ai_patrol", "source": "..."}
  {"cmd": "script_unload", "name": "ai_patrol"}
  {"cmd": "script_list"}

Server-side handler (in on_drain):
- script_load: aegis_asm_compile() → aegis_script_runtime_load() → respond with script_id or error
- script_unload: aegis_script_runtime_unload() by name lookup
- script_list: enumerate active instances, respond with names + status

Files:
- src/editor/commands/cmd_script.c (editor-side command parsing)
- src/editor/commands/edit_commands_register.c (register 'script' command)
- Server handler integrated into existing edit_cmd_ctx dispatch

Dependencies: rpg-h84i (IL assembler), rpg-za2w (script runtime)

Acceptance criteria:
- [ ] `script load path/to/file.ail` compiles and loads script on server
- [ ] `script unload name` removes running script
- [ ] `script list` shows active scripts with status
- [ ] Compilation errors returned to editor with line number
- [ ] Server-side handler wired into on_drain command dispatch
- [ ] Tests: load valid script, load invalid script (error response), unload, list

