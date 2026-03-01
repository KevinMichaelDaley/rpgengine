---
id: rpg-yspo
status: open
deps: []
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, types]
---
# Aegis core types and instruction encoding

Define all core Aegis VM types per ref/aegis_bytecode_spec.md §3.1, §3.2, §3.4.

Implement:
- aegis_register_t: 128-bit register union (i32, i64, f32, f64, u32, u64, vec2, vec3, vec4/quat, entity_id, handle, bytes[16])
- aegis_instruction_t: 4 x uint32_t (opcode word + 3 operand words)
- Opcode enum covering all instructions in §3.3 (snake_case enum values)
- Immediate mode flag extraction macros (bits 16-18 of opcode word)
- aegis_bytecode_t: compiled bytecode container (instruction array, constant pool, static_size declaration, topic hash)
- aegis_config_t: VM configuration (arena_size, static_max, stack_max, fuel_budget, wall_time_budget_ns, max_updates, max_async_tasks)

Files:
- include/ferrum/aegis/aegis_types.h (register, instruction, opcode enum)
- include/ferrum/aegis/aegis_bytecode.h (bytecode container)
- include/ferrum/aegis/aegis_config.h (configuration)

Acceptance criteria:
- [ ] All types defined with correct sizes (static_assert)
- [ ] Opcode enum covers every instruction in §3.3
- [ ] Immediate mode macros correctly extract bits 16-18
- [ ] sizeof(aegis_register_t) == 16
- [ ] sizeof(aegis_instruction_t) == 16
- [ ] Types compile clean under -Wall -Wextra -Wpedantic
- [ ] Unit tests verify register union layout, opcode encoding/decoding, immediate flag extraction

## Acceptance Criteria

All types match spec §3.1/3.2/3.4, compile clean, sizes verified by static_assert

