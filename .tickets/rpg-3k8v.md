---
id: rpg-3k8v
status: open
deps: [rpg-o8pq, rpg-x7eg, rpg-5p3e]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-hvyg
tags: [aegis, vm, security, testing]
---
# Aegis VM fuzz testing and security audit

Fuzz test the Aegis VM to find crashes, hangs, and security issues per ref/aegis_bytecode_spec.md §10 Phase 6.

Implement:
- Bytecode fuzzer: generate random valid and invalid bytecode programs, feed to aegis_execute
- Targets: instruction decoder, memory bounds checking, fuel metering, async status transitions, event payload access, update set construction
- Must verify: no crashes (segfaults, aborts), no hangs (fuel metering prevents), no memory corruption (bounds checking prevents), no undefined behavior (clean under -fsanitize=address,undefined)

Approach:
- Structure-aware fuzzer that generates syntactically valid but semantically adversarial bytecode
- Mutation-based fuzzer that corrupts valid bytecode at random positions
- Specifically test: invalid opcodes, out-of-range register indices, immediate values that cause overflow, zero-size alloc, negative offsets, concurrent async status writes

Files:
- tests/aegis/aegis_fuzz.c (fuzz harness)
- tests/aegis/aegis_adversarial_tests.c (hand-crafted adversarial programs)

Acceptance criteria:
- [ ] No crashes on 1M random bytecode programs
- [ ] No hangs (all programs terminate via yield, exit, or fuel exhaustion)
- [ ] Clean under AddressSanitizer
- [ ] Clean under UndefinedBehaviorSanitizer
- [ ] All bounds checks verified to reject out-of-range access
- [ ] Adversarial test suite covers: max recursion, arena exhaustion, async limit, update limit, invalid opcode, corrupt instruction

## Acceptance Criteria

No crashes/hangs/UB on random and adversarial bytecode under sanitizers

