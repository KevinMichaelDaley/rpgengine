---
id: rpg-litt
status: open
deps: [rpg-jyx3]
links: []
created: 2026-03-01T06:47:16Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, security]
---
# Instruction budget: hook-based coroutine yielding

Install lua_sethook with LUA_MASKCOUNT to yield scripts after N instructions per tick.

## Mechanism
- lua_sethook(L, hook_fn, LUA_MASKCOUNT, budget) installed before script execution
- Hook callback calls lua_yield(L, 0) to suspend the coroutine
- Budget is configurable (default 100K instructions per tick)
- Budget resets each tick via script_budget_reset()

## API
- script_budget_init(budget, max_instructions) — initialize
- script_budget_install(budget, L) — install hook on Lua state
- script_budget_reset(budget) — reset counter for new tick
- script_budget_exhausted(budget) — check if budget was hit

## Coroutine integration
- Scripts run inside coroutines (lua_newthread + lua_resume)
- When hook fires, yield suspends the coroutine
- Next tick, lua_resume continues from where it left off
- Budget tracks total instructions across yields within a tick

## Files
- include/ferrum/editor/edit_script_budget.h (type: script_budget_t)
- src/editor/script/edit_script_budget.c
- tests/editor/edit_script_budget_tests.c

## Tests
- Budget init sets correct values
- Install hook on state succeeds
- Script completes within budget (no yield)
- Infinite loop yields after budget exhaustion
- Budget reset allows resumption
- Budget exhausted flag set correctly
- Very small budget (10 instructions) yields quickly
- Multiple scripts share budget within one tick

