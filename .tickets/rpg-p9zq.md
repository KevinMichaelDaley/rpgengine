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

Epic for the LuaJIT scripting system. This phase adds LuaJIT 2.1 integration, the instruction-budgeted script runtime on the main tick thread, entity/math/utility API bindings, run/eval commands, and REPL mode with server-side continuation detection.

Before starting any subtask, read:
- ref/editor_spec.md §2.5 (script runtime)
- ref/editor_design.md §6 (script runtime: LuaJIT integration, execution model, REPL continuation, C→Lua bindings, safety)
- ref/editor_ux.md §8 (scripting workflow, REPL mode, script API)

Critical constraints:
- LuaJIT runs on MAIN TICK THREAD only (never on fibers)
- Instruction-budgeted via lua_sethook (default 100K instructions/tick)
- Multi-frame scripts use Lua coroutines (yield Lua state, NOT C/fiber stack)
- FFI is DISABLED in sandbox
- All entity operations are deferred (enqueued, executed during drain)

