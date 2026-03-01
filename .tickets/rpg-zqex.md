---
id: rpg-zqex
status: open
deps: [rpg-qdn7, rpg-dvgo, rpg-eccf]
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script runtime core (dedicated thread, double-buffered entity updates)

Implement the core script runtime on a dedicated pthread with double-buffered entity state exchange.

READ FIRST: ref/editor_design.md §6 for the full threaded runtime architecture, ref/editor_spec.md §2.5 for concurrency model.

Architecture:
- Dedicated script thread (pthread), NOT main tick thread
- Reads entity state from a read-only snapshot (copied by main tick after drain)
- Writes entity updates to a double-buffered update array (rebased by main tick)
- Submits edit commands (spawn, delete, etc.) via SPSC ring → main tick
- script_env_t provides unified interface for both scripting and native C code

Requirements:
- script_runtime_t: pthread, script_state, update_buf, snapshot, cmd_ring, env
- script_runtime_init() → spawn thread, allocate snapshot/update buffers, create script state with arena allocator (8 MB)
- Script thread loop: wait for snapshot seq → copy snapshot → run scripts → swap update buffer
- script_update_buffer_t: double-buffered array with atomic ready flag
- script_entity_snapshot_t: read-only entity view (id, type, name, pos, rot, scale)
- Budget hook via instruction hook: forces yield when budget exhausted (100K instructions default)
- Sandbox: remove os, io, loadlib, debug, ffi libraries
- script_runtime_eval(rt, code, result) → enqueue code for execution, return result
- script_runtime_shutdown(rt) → signal stop, join thread, close script state, free arena
- Native code path: script_runtime_register_native(rt, fn, userdata) for C functions using same script_env_t
- Main tick integration: drain script cmd_ring, read front update buffer, apply rebase

Files to create:
- include/ferrum/editor/edit_script_runtime.h (script_runtime_t, script_env_t)
- src/editor/script/edit_script_runtime.c (init, thread loop, shutdown)
- src/editor/script/edit_script_env.c (env setup, snapshot copy, update swap)
- src/editor/script/edit_script_sandbox.c (script sandbox setup)
- src/editor/script/edit_script_rebase.c (apply entity updates onto tick state)
- tests/editor/edit_script_runtime_tests.c

