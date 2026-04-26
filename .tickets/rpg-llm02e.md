---
id: rpg-llm02e
status: open
deps: [rpg-llm01]
links: []
created: 2026-04-26T01:20:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, vm, opcode, whitelist, dispatch, integration]
---
# VM Tool Action Opcode: Framework + Whitelist + Integration Test

Implement the `AEGIS_OP_TOOL_ACTION = 0x4D` immediate opcode that dispatches all immediate tools. This is the foundation that rpg-llm02c and rpg-llm02d build on.

## Requirements

### Opcode
```
tool_action r_result, r_tool_id, r_args_handle
```
- `r_tool_id`: enum value (TRADE_INIT=0, TRADE_SELL=1, TRADE_BUY=2, TRADE_ACCEPT=3, TRADE_REJECT=4, DEFEND=5, ATTACK=6, FLEE=7, GOTO=8, KNOWLEDGE_QUERY=9)
- `r_args_handle`: heap offset of null-terminated JSON args string
- `r_result`: output — status code (0=ok, negative=error)
- Fuel cost: 50 per call

### Tool ID Enum
```c
typedef enum {
    AEGIS_TOOL_TRADE_INIT   = 0,
    AEGIS_TOOL_TRADE_SELL   = 1,
    AEGIS_TOOL_TRADE_BUY    = 2,
    AEGIS_TOOL_TRADE_ACCEPT = 3,
    AEGIS_TOOL_TRADE_REJECT = 4,
    AEGIS_TOOL_DEFEND       = 5,
    AEGIS_TOOL_ATTACK       = 6,
    AEGIS_TOOL_FLEE         = 7,
    AEGIS_TOOL_GOTO         = 8,
    AEGIS_TOOL_KNOWLEDGE_QUERY = 9,
} aegis_tool_id_t;
```

### Dispatch Flow
1. Read `r_tool_id` from register
2. Whitelist check: if `tool_id > 9`, return `AEGIS_LLM_TOOL_UNKNOWN`
3. Parse JSON args from heap at `r_args_handle`
4. Dispatch to tool-specific validator:
   - Trade tools → `npc_trade_*()` functions (stubs until rpg-llm02c)
   - Combat/nav tools → `npc_combat_action_*()` / `npc_nav_action_*()` (stubs until rpg-llm02d)
5. Validator returns status string; write to `r_result`
6. Deduct fuel (50)

### Error Codes
```c
#define AEGIS_TOOL_OK          0
#define AEGIS_TOOL_UNKNOWN    -1
#define AEGIS_TOOL_RANGE      -2
#define AEGIS_TOOL_LANGUAGE   -3
#define AEGIS_TOOL_STATE      -4
#define AEGIS_TOOL_INVENTORY  -5
#define AEGIS_TOOL_NAV        -6
```

### Stub Layer
Since rpg-llm02c/d implement the actual validators, this ticket provides:
- `include/ferrum/aegis/aegis_tools.h` — tool IDs + result types
- `include/ferrum/aegis/aegis_ops_tools.h` — opcode handler declarations
- `src/aegis/ops/aegis_ops_tool.c` — dispatcher with function pointer table
- Stub functions for all tools that return `"not yet implemented"` to the LLM

## Files to Create
- `include/ferrum/aegis/aegis_tools.h` — `aegis_tool_id_t`, error codes, `aegis_tool_result_t`
- `include/ferrum/aegis/aegis_ops_tools.h` — `aegis_op_tool_action()` declaration
- `src/aegis/ops/aegis_ops_tool.c` — dispatcher, whitelist, JSON parse, fuel deduct
- `tests/aegis/aegis_tools_tests.c` — whitelist enforcement, unknown tool rejection, fuel consumption, stub dispatch

## Files to Modify
- `include/ferrum/aegis/aegis_types.h` — add `AEGIS_OP_TOOL_ACTION = 0x4D`
- `src/aegis/aegis_vm_run.c` — add case to interpreter switch

## Acceptance
- [ ] `tool_action` opcode reads tool_id and args from registers.
- [ ] Unknown tool_id returns `AEGIS_TOOL_UNKNOWN` with error text.
- [ ] Known tool_id dispatches to correct stub function.
- [ ] Fuel consumption: 50 per call.
- [ ] Rapid tool-call loops force yield when fuel exhausted.
- [ ] JSON args parsing handles empty args `{}` and missing optional fields.
- [ ] Integration test: AEGIS script → `tool_action` → verify result register.
