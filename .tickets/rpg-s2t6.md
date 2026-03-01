---
id: rpg-s2t6
status: open
deps: [rpg-rh6r]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, arithmetic]
---
# Aegis arithmetic, logic, and comparison instructions

Implement arithmetic, logic, comparison, and type conversion instructions per ref/aegis_bytecode_spec.md §3.3.

Instructions:
- Arithmetic: add, sub, mul, div, mod, neg (integer overflow checked, IEEE 754 for floats)
- Bitwise: and, or, xor, not
- Comparison: eq, ne, lt, le, gt, ge (result is bool in r_dst)
- Type conversion: i32_to_f32, f32_to_i32, i64_to_f64, f64_to_i64, f64_to_f32, f32_to_f64

Each instruction is a pure function: reads source registers, writes destination register. Implement as handler functions with signature: bool aegis_op_add(aegis_register_t *dst, const aegis_register_t *a, const aegis_register_t *b).

Div/mod by zero sets a VM error flag rather than crashing.

Files:
- include/ferrum/aegis/aegis_ops_arith.h
- src/aegis/ops/aegis_ops_arith.c (add, sub, mul, div — 4 functions max per file)
- src/aegis/ops/aegis_ops_arith2.c (mod, neg + bitwise ops)
- src/aegis/ops/aegis_ops_compare.c (eq, ne, lt, le)
- src/aegis/ops/aegis_ops_compare2.c (gt, ge)
- src/aegis/ops/aegis_ops_convert.c (type conversions)
- tests/aegis/aegis_ops_arith_tests.c
- tests/aegis/aegis_ops_compare_tests.c

Acceptance criteria:
- [ ] All arithmetic operations produce correct results for i32 and f32
- [ ] Integer overflow detection (or wrapping with defined behavior)
- [ ] Division by zero returns error, does not crash
- [ ] Comparison operations set bool result (0 or 1)
- [ ] Type conversions handle edge cases (NaN, overflow, truncation)
- [ ] Tests: happy path for each op, edge cases (0, -1, MAX_INT, NaN, inf), div by zero

## Acceptance Criteria

All arith/logic/compare/convert ops correct, div-by-zero handled, edge cases tested

