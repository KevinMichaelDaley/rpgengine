---
id: rpg-fiwu
status: open
deps: [rpg-rh6r, rpg-i1az]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, control]
---
# Aegis control flow instructions

Implement control flow instructions per ref/aegis_bytecode_spec.md §3.3.

Instructions:
- jmp label: unconditional jump (set PC to label offset)
- jmp_if r_cond, label: jump if register is truthy (nonzero)
- jmp_if_not r_cond, label: jump if register is falsy (zero)
- call label: push return PC + frame pointer to call stack, jump to label
- ret: pop call frame, restore PC from saved return address

Call stack integration:
- call uses aegis_memory_push_frame() from the memory module
- ret uses aegis_memory_pop_frame()
- Call depth counter incremented on call, decremented on ret
- Call depth exceeding 256 → script exit with error

Label resolution:
- Labels are instruction indices (offsets into bytecode array)
- Jump targets validated at load time (within bytecode bounds)

Files:
- include/ferrum/aegis/aegis_ops_flow.h
- src/aegis/ops/aegis_ops_flow.c
- tests/aegis/aegis_ops_flow_tests.c

Acceptance criteria:
- [ ] jmp correctly sets PC
- [ ] jmp_if jumps on nonzero, falls through on zero
- [ ] jmp_if_not jumps on zero, falls through on nonzero
- [ ] call pushes frame and jumps; ret returns to correct PC
- [ ] Nested calls (up to 256 depth) work correctly
- [ ] Call depth > 256 returns error (script exit)
- [ ] Out-of-bounds jump target returns error
- [ ] Tests: unconditional jump, conditional true/false, call+ret, nested calls, max depth, invalid target

## Acceptance Criteria

All branch/call/ret ops work, call depth enforced at 256, bounds-checked jumps

