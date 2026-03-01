---
id: rpg-ped1
status: closed
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

Add an editor command that sends Aegis IL script text to the server for compilation and registration. Scripts are NOT immediately started — they are lazily spawned when their subscribed topic fires for the first time.

## Lazy spawn model

The server maintains a **script registry** (compiled bytecodes with topic subscriptions) separate from the **active instance table** (running fibers). When an event is published:

1. The runtime checks if any registered-but-not-yet-spawned scripts subscribe to that topic
2. If found, it lazily spawns the script: allocates VM instance, dispatches fiber, delivers the triggering event
3. Once spawned, the script stays alive until it exits, errors, or is explicitly unloaded
4. Scripts that have already been spawned receive events normally through the existing topic routing

This means `script load` compiles and registers a script (stores bytecode + topic hash) but does NOT start a fiber. The fiber is created on-demand when the first matching event arrives. This avoids wasting fibers on scripts whose events never fire (e.g., level-specific AI scripts for a level the player hasn't entered yet, or event handlers for rare collisions).

The registry is per-level: loading a new level clears and repopulates the registry.

## Editor side (TUI command)
- `script load <path>` — read file, send contents as JSON command (registers on server)
- `script unload <name>` — send unload command by script name (removes from registry + kills active fiber if any)
- `script list` — query registered and active scripts from server
- `script eval <inline_il>` — compile and run inline IL immediately (one-shot, bypasses lazy spawn)

## JSON wire format
  {"cmd": "script_load", "name": "ai_patrol", "source": "..."}
  {"cmd": "script_unload", "name": "ai_patrol"}
  {"cmd": "script_list"}

## Server-side handler (in on_drain)
- script_load: aegis_asm_compile() → register bytecode in script registry → respond with OK or compilation error
- script_unload: remove from registry, unload active instance if spawned
- script_list: enumerate registered scripts with status (registered/active/exited/error)

## Runtime changes needed
- Add aegis_script_registry_t to aegis_runtime.h: fixed-capacity array of compiled bytecodes with names and topic hashes
- aegis_script_runtime_register(rt, name, bc): store bytecode, record topic hash (no fiber yet)
- aegis_script_runtime_unregister(rt, name): remove from registry, unload if active
- Modify aegis_script_runtime_publish(): before routing to active instances, check registry for unspawned scripts that match the event topic → lazy-spawn them

## Files
- include/ferrum/aegis/aegis_runtime.h (add registry types)
- src/aegis/aegis_runtime_registry.c (register, unregister, lazy spawn on publish)
- src/editor/commands/cmd_script.c (editor-side command parsing)
- src/editor/commands/edit_commands_register.c (register 'script' command)
- Server handler integrated into existing edit_cmd_ctx dispatch

Dependencies: rpg-h84i (IL assembler), rpg-za2w (script runtime)

Acceptance criteria:
- [ ] `script load path/to/file.ail` compiles and registers script on server (no fiber yet)
- [ ] First matching event lazily spawns the script's fiber
- [ ] `script unload name` removes from registry + kills active fiber if any
- [ ] `script list` shows registered and active scripts with status
- [ ] Compilation errors returned to editor with line number
- [ ] Server-side handler wired into on_drain command dispatch
- [ ] Tests: register script, lazy spawn on event, unload registered, unload active, list

