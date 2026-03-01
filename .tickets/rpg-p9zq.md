---
id: rpg-p9zq
status: open
deps: [rpg-tiet]
links: []
created: 2026-02-26T04:28:42Z
type: epic
priority: 1
assignee: KMD
tags: [editor, scripting]
---
# Phase 3: Scripting

Epic for the engine scripting language scripting system with dedicated thread architecture. The script runtime runs on its own pthread, reads entity state from a frozen snapshot, writes updates via a double-buffered blob, and supports both scripting and native C code through a unified script_env_t interface.

Before starting any subtask, read:
- ref/editor_spec.md §2.5 (script runtime concurrency model)
- ref/editor_design.md §6 (full script runtime architecture: types, threading, double-buffer, native path, sandbox, safety)
- ref/editor_ux.md §8 (scripting workflow, REPL mode, script API)

Subtasks (in dependency order):
1. rpg-qdn7 — the engine scripting language build integration [DONE]
2. rpg-dvgo — Dynamic entity attribute storage (entity_attrs_t)
3. rpg-eccf — Script environment and snapshot (script_env_t)
4. rpg-jyx3 — Script sandbox (script state isolation)
5. rpg-zqex — Script runtime core (dedicated thread, double-buffered entity updates)
6. rpg-v1rs — Script rebase (apply entity updates onto tick state)
7. rpg-lqfl — Native script function registry
8. rpg-96oo — Scripting entity manipulation API bindings
9. rpg-7x16 — Math/vec3/quat scripting bindings
10. rpg-ec5e — run/eval commands for script execution
11. rpg-xqkt — REPL mode with continuation detection
12. rpg-kuw1 — Script undo grouping (begin_group/end_group)

Critical constraints:
- Script thread is a dedicated pthread (NOT main tick thread, NOT fibers)
- Reads frozen entity snapshot (no live state access)
- Writes via variable-length attribute blob (rebased by main tick)
- Edit commands via SPSC ring (same as I/O commands)
- Instruction-budgeted via instruction hook (default 100K instructions/tick)
- Multi-frame scripts use coroutines (yield script state, NOT C/fiber stack)
- FFI is DISABLED in sandbox
- Native C functions use same script_env_t interface as scripts

