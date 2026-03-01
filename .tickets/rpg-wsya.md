---
id: rpg-wsya
status: open
deps: [rpg-yspo]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-ssr8
tags: [aegis, vm, security]
---
# Aegis bytecode canonicalization passes

Implement bytecode normalization passes per ref/aegis_bytecode_spec.md §4.1.

Passes (applied in order):
1. Alpha renormalization: rename all registers by order of first definition
2. Commutative sorting: sort operands of commutative ops (add, mul, and, or) by register index
3. Dead code elimination: remove computations not consumed by yield, push_update, or async submissions
4. Constant folding (limited): evaluate pure arithmetic on constants at compile time; preserve div-by-zero traps
5. Graph linearization: serialize CFG in reverse postorder (RPO) for canonical ordering

Input: raw aegis_bytecode_t. Output: normalized aegis_bytecode_t with canonical form.

Files:
- include/ferrum/aegis/aegis_canon.h
- src/aegis/aegis_canon_alpha.c (alpha renormalization)
- src/aegis/aegis_canon_commute.c (commutative sorting)
- src/aegis/aegis_canon_dce.c (dead code elimination)
- src/aegis/aegis_canon_fold.c (constant folding)
- tests/aegis/aegis_canon_tests.c

Acceptance criteria:
- [ ] Alpha renorm: r5=r1+r2 and r8=r3+r4 produce identical normalized bytecode if dataflow-equivalent
- [ ] Commutative sort: add r1,r3,r2 → add r1,r2,r3
- [ ] DCE: removes unused computations, preserves side-effecting instructions
- [ ] Constant folding: load_imm 2 + load_imm 3 → load_imm 5
- [ ] Div-by-zero preserved (not folded away)
- [ ] Tests: each pass individually + combined pipeline, idempotency (normalizing twice = same result)

## Acceptance Criteria

Normalization produces canonical bytecode invariant to register naming and operand order

