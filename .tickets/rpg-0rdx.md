---
id: rpg-0rdx
status: closed
deps: [rpg-vebd]
links: []
created: 2026-03-01T06:47:31Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, security]
---
# Functional purity: freeze globals and read-only tables

Enforce functional purity at runtime: scripts cannot mutate global state except through registered engine.* endpoints.

## Mechanism
- Set _G metatable with __newindex that errors on any global write
- Freeze all library tables (string, table, math, coroutine, bit) via read-only metatables
- The engine table is also frozen after API registration (write to engine.* errors)
- Only engine.log/warn/err/write_entity are allowed side effects

## API
- script_purity_init(L) — freeze everything (call AFTER script_api_register)
- script_purity_freeze_table(L, name) — make a named global table read-only
- script_purity_freeze_globals(L) — set _G __newindex to error

## Allowlist
- Local variables are unrestricted (pure computation)
- Table creation via {} is fine (local scope)
- engine.log/warn/err (registered, rate-limited)
- engine.write_entity (registered, validated)

## Files
- include/ferrum/editor/edit_script_purity.h (no types, just functions)
- src/editor/script/edit_script_purity.c
- tests/editor/edit_script_purity_tests.c

## Tests
- Global write errors (x = 5 → error)
- Local variables work (local x = 5 → ok)
- string table is frozen (string.foo = 1 → error)
- math table is frozen (math.foo = 1 → error)
- table lib frozen, coroutine frozen, bit frozen
- engine table frozen (engine.foo = 1 → error)
- engine.log still callable after freeze
- Nested table creation works (local t = {a={b=1}})
- pcall catches purity violations gracefully
- Function definitions work (local function f() end)


## Notes

**2026-03-01T09:58:49Z**

Superseded by Aegis VM implementation. See ref/aegis_bytecode_spec.md.
