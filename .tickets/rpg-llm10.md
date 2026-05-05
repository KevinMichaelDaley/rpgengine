---
id: rpg-llm10
status: closed
deps: [rpg-llm0d-combat]
links: [rpg-llm02d]
created: 2026-05-03T20:00:00Z
type: bug
priority: 2
assignee: KMD
parent: rpg-llm02
tags: [aegis, tools, stub, defend, attack, flee, goto]
---
# Tools 5-8 (DEFEND/ATTACK/FLEE/GOTO) Return Stub to LLM

In `src/aegis/ops/aegis_ops_tool.c:340`, tools DEFEND(5), ATTACK(6), FLEE(7), GOTO(8) all dispatch to `tool_stub_not_implemented` which returns `"<TOOL>: not yet implemented"` with status `AEGIS_TOOL_OK` (0).

The LLM receives a success status code with a "not implemented" message — misleading. The LLM cannot distinguish between "the tool worked" and "the engine doesn't support this tool yet."

## Root Cause
GOTO was split into `rpg-llm0d-nav` (closed) but the dispatcher was never wired. DEFEND/ATTACK/FLEE depend on a combat subsystem that doesn't exist yet.

## Fix
1. Wire GOTO (tool_id=8) into the real handler via `npc_nav_action_goto()` or a similar dispatch
2. For DEFEND/ATTACK/FLEE, return proper error status and message like `"Combat system not available"` instead of `"not yet implemented"`
3. Return `AEGIS_TOOL_UNKNOWN` or a more specific error code instead of `AEGIS_TOOL_OK`

## Acceptance
- [ ] GOTO dispatches to real handler (not stub)
- [ ] DEFEND returns meaningful error with negative status
- [ ] ATTACK returns meaningful error with negative status
- [ ] FLEE returns meaningful error with negative status
