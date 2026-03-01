---
id: rpg-o8pq
status: in_progress
deps: [rpg-s2t6, rpg-fiwu, rpg-a10n, rpg-uch2, rpg-5h7o]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, interpreter]
---
# Aegis main interpreter loop

Implement the main interpreter loop that ties all instruction handlers together per ref/aegis_bytecode_spec.md §3, §6, §8.

Implements aegis_execute() and aegis_resume() from the C API (§8):
- aegis_execute(script, input, fuel_budget, out_updates): first invocation of a script
- aegis_resume(script, input, fuel_budget, out_updates): resume after yield/force-yield/wait

Main loop:
1. Fetch instruction at PC
2. Decode (extract opcode + operands)
3. Dispatch to handler (switch or function pointer table)
4. Decrement fuel counter
5. Check for yield/exit/error
6. Advance PC (unless jump/yield changed it)
7. Return aegis_result_t indicating outcome (YIELDED, EXITED, FORCE_YIELDED, ERROR)

The dispatch table covers all opcodes from §3.3. Unknown opcodes → error.

Also implement:
- aegis_script_create(bytecode, config, arena): allocate and init script instance
- aegis_bytecode_hash(bc): content hash of normalized bytecode

Files:
- include/ferrum/aegis/aegis_vm.h (if not already created by yield ticket — extend)
- src/aegis/aegis_vm.c (main loop, dispatch)
- src/aegis/aegis_script.c (script create, destroy)
- tests/aegis/aegis_vm_tests.c (integration tests)

Acceptance criteria:
- [ ] Simple bytecode programs execute correctly (arithmetic → register → yield)
- [ ] All instruction types dispatch to correct handlers
- [ ] Unknown opcode returns AEGIS_RESULT_ERROR
- [ ] Fuel metering integrated into loop
- [ ] aegis_execute starts fresh; aegis_resume continues from continuation
- [ ] Tests: trivial program (load_imm + add + yield), program with branches, program with call/ret, fuel exhaustion mid-program, resume after force-yield, exit with code

## Acceptance Criteria

Full interpreter loop executing bytecode programs, all instructions dispatched, fuel-metered

