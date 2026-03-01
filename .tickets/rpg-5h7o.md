---
id: rpg-5h7o
status: closed
deps: [rpg-i1az, rpg-fiwu]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, yield, fuel]
---
# Aegis yield/resume/exit and fuel metering

Implement the coroutine lifecycle and fuel metering per ref/aegis_bytecode_spec.md §3.3, §6.

Coroutine instructions:
- resume: entry point marker; sets initial PC, loads event/world data into VM context
- yield: explicit yield; serializes continuation state (PC, registers, call stack), resets heap arena, resets fuel counter, returns update_set to engine
- exit r_code: permanent termination; script is deregistered

Fuel metering (§6.1):
- Fuel counter decremented by 1 per instruction executed
- On explicit yield: fuel counter reset to budget
- On fuel exhaustion (counter reaches 0): force-yield — serialize continuation WITHOUT resetting heap arena, flag script, resume from same PC next tick
- Force-yield is transparent to the script; it resumes exactly where it left off

Continuation state (serialized on yield/force-yield):
- Program counter
- All 256 registers (4 KB)
- Call stack state (frame pointer, depth)
- Heap arena state (bump pointer — NOT reset on force-yield)
- Static array is implicitly preserved (in-place, never moves)

Three yield types (§6.1 table):
| Type          | Heap reset | Stack preserved | Fuel reset |
|---------------|------------|-----------------|------------|
| Explicit yield| Yes        | Must be depth 0 | Yes        |
| Force-yield   | No         | Yes             | Yes        |
| Wait-yield    | No         | Yes             | No         |

Files:
- include/ferrum/aegis/aegis_vm.h (VM state: registers, PC, fuel, memory, continuation)
- src/aegis/aegis_yield.c (yield, force-yield, continuation serialization)
- src/aegis/aegis_fuel.c (fuel decrement, exhaustion check)
- tests/aegis/aegis_yield_tests.c

Acceptance criteria:
- [ ] resume correctly initializes VM state from bytecode + input
- [ ] yield serializes continuation and resets heap
- [ ] yield at call depth > 0 returns validation error
- [ ] exit stops execution with error code
- [ ] Fuel counter decrements per instruction
- [ ] Fuel exhaustion triggers force-yield (not error)
- [ ] Force-yield preserves heap, stack, registers
- [ ] Resumed script continues from exact instruction after force-yield
- [ ] Tests: simple yield/resume cycle, fuel exhaustion in loop, force-yield preserves state, exit codes, yield-at-depth error

## Acceptance Criteria

Coroutine lifecycle correct, three yield types with correct heap/stack/fuel semantics

