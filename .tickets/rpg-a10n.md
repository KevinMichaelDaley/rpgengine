---
id: rpg-a10n
status: closed
deps: [rpg-rh6r, rpg-i1az]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, data]
---
# Aegis data movement instructions

Implement data movement and memory instructions per ref/aegis_bytecode_spec.md §3.3.

Data movement:
- mov r_dst, r_src: copy full 128-bit register
- load_imm r_dst, imm: load 32-bit immediate into register (zero-extends to 128 bits)
- load_imm64 r_dst, imm_lo, imm_hi: load 64-bit immediate (b=low, c=high)

Memory (heap arena):
- alloc r_dst, size: bump-allocate from heap arena; r_dst = offset; error if full
- load r_dst, r_base, off: load 16 bytes from arena[r_base + off]; bounds-checked
- store r_base, off, r_val: store 16 bytes to arena[r_base + off]; bounds-checked

Memory (static array):
- static_load r_dst, off: load 16 bytes from static array at offset; bounds-checked
- static_store off, r_val: store 16 bytes to static array at offset; bounds-checked

Memory (call stack):
- push r_val: push register onto call stack
- pop r_dst: pop top of call stack into register

Files:
- include/ferrum/aegis/aegis_ops_data.h
- src/aegis/ops/aegis_ops_data.c (mov, load_imm, load_imm64, alloc)
- src/aegis/ops/aegis_ops_mem.c (load, store, static_load, static_store)
- src/aegis/ops/aegis_ops_stack.c (push, pop)
- tests/aegis/aegis_ops_data_tests.c

Acceptance criteria:
- [ ] mov copies full 128-bit register
- [ ] load_imm sets low 32 bits, zeroes upper 96 bits
- [ ] load_imm64 correctly assembles 64-bit value from two 32-bit halves
- [ ] alloc returns sequential offsets; returns error when arena full
- [ ] load/store correctly address heap arena with bounds checking
- [ ] static_load/static_store address static array, survive heap reset
- [ ] push/pop maintain LIFO order; underflow returns error
- [ ] Tests: each instruction happy path, bounds violations, heap full, stack underflow

## Acceptance Criteria

All data/memory ops correct, bounds-checked, static array survives heap reset

