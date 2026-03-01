---
id: rpg-rh6r
status: closed
deps: [rpg-yspo]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, decoder]
---
# Aegis instruction decoder

Implement the instruction decoder per ref/aegis_bytecode_spec.md §3.4.

Decodes a 128-bit instruction into opcode + resolved operands (register values or immediates).

Implement:
- aegis_decode_result_t: decoded instruction (opcode, operand_a/b/c values, is_imm_a/b/c flags)
- aegis_decode(instruction, registers, out_result): decode one instruction, resolving register references to values and passing through immediates
- Immediate mode: if bit N is set in opcode word, operand N is a literal value (uint32/float); otherwise it's a register index (0-255) and the decoder reads the register file
- Validate register indices are 0-255; invalid index → decode error

Files:
- include/ferrum/aegis/aegis_decode.h
- src/aegis/aegis_decode.c
- tests/aegis/aegis_decode_tests.c

Acceptance criteria:
- [ ] Correctly extracts opcode from bits 0-15
- [ ] Correctly extracts immediate flags from bits 16-18
- [ ] Register mode: reads register file at given index
- [ ] Immediate mode: passes through raw uint32 value
- [ ] Invalid register index (>255) returns error
- [ ] Tests cover: all-register instruction, all-immediate, mixed, zero operands (yield), boundary register indices (0, 255)

## Acceptance Criteria

Decoder extracts opcode + operands with immediate flag handling, validates indices

