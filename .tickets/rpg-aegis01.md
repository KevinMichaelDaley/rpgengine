---
id: rpg-aegis01
status: closed
deps: [rpg-llm02c]
links: [rpg-llm02c]
created: 2026-05-03T20:00:00Z
type: bug
priority: 2
assignee: KMD
parent: rpg-llm02
tags: [aegis, tools, header, linker, trade]
---
# aegis_op_trade_* Declared in Header but Never Implemented

`include/ferrum/aegis/aegis_ops_tools.h:38-42` declares five public trade operation functions:
- `aegis_op_trade_init`
- `aegis_op_trade_sell`
- `aegis_op_trade_buy`
- `aegis_op_trade_accept`
- `aegis_op_trade_reject`

None of these have `.c` implementations. The actual trade logic is in static functions (`handle_trade_init`, etc.) inside `aegis_ops_tool.c`. Any external caller that links against these symbols will get a linker error.

## Fix
Option 1: Remove the declarations from the header (they're internal-only).
Option 2: Implement the functions as thin wrappers around the static handlers.
Option 3: Make the trade handlers accept `aegis_vm_t *vm, const char *args_json` signature and implement the declared functions.

## Acceptance
- [ ] No linker errors when external code references trade operations
- [ ] OR: declarations removed from public header with no external callers
