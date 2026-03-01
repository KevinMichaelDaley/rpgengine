---
id: rpg-h84i
status: closed
deps: [rpg-o8pq]
links: []
created: 2026-03-01T21:27:19Z
type: task
priority: 1
assignee: KMD
parent: rpg-p9zq
tags: [aegis, vm, compiler]
---
# Aegis text IL assembler and bytecode compiler

Implement a text-based intermediate language (IL) assembler for Aegis bytecode.

The IL uses a simple asm-like syntax matching the opcode mnemonics in ref/aegis_bytecode_spec.md §3.3. Each line is one instruction with snake_case mnemonics and register/immediate operands.

Syntax:
- Registers: r0..r255
- Immediates: decimal integers, hex (0x...), float literals (1.5f)
- Labels: name followed by colon (loop:), referenced by jump/call targets
- Comments: ; or // to end of line
- Blank lines ignored

Example:
  .static 64
  event_type r0
  event_src r1
  event_field r2, 0, 12
  load_imm r10, 100
  add r3, r2, r10
  yield

Implement:
- aegis_asm_t: assembler state (label table, output instruction buffer, diagnostics)
- aegis_asm_init(asm, arena, arena_size): initialize assembler with output arena
- aegis_asm_compile(asm, source, source_len, out_bytecode): parse IL text → aegis_bytecode_t
- aegis_asm_error(asm): return human-readable error string on failure
- Label resolution: forward references resolved in a second pass
- Directive extraction: .topic populates bytecode metadata (topic_hash), .static sets static_size, .fuel sets fuel_budget

Files:
- include/ferrum/aegis/aegis_asm.h
- src/aegis/aegis_asm_parse.c (tokenizer + line parser, 4 functions max)
- src/aegis/aegis_asm_compile.c (label resolution + bytecode emit, 4 functions max)
- src/aegis/aegis_asm_directives.c (directive parsing, if needed)
- tests/aegis/aegis_asm_tests.c

Acceptance criteria:
- [ ] Compiles valid IL text to correct aegis_bytecode_t
- [ ] All 66 opcodes recognized by mnemonic
- [ ] Registers parsed as r0..r255
- [ ] Immediates: decimal, hex, float
- [ ] Labels: forward and backward references resolved
- [ ] Directives: .topic, .static, .fuel extracted
- [ ] Malformed input produces clear error (line number + message)
- [ ] Tests: round-trip (asm→bytecode→execute→verify), label resolution, all directive types, error cases

