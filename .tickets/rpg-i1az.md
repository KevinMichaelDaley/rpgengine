---
id: rpg-i1az
status: closed
deps: [rpg-yspo]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, memory]
---
# Aegis three-zone memory layout

Implement the three-zone per-script memory layout per ref/aegis_bytecode_spec.md §3.6.

Layout:
  [static array | call stack | heap arena]

Implement:
- aegis_memory_t: manages the three zones within a single contiguous allocation
- aegis_memory_init(mem, arena_buf, arena_size, static_size, stack_size): partition the arena
- Static array: fixed region at base, zero-initialized, accessed by offset, bounds-checked
- Call stack: fixed capacity, grows upward from static_end, stores frames (return PC, frame pointer, register save area)
- Heap arena: bump allocator from stack_limit to arena end, bounds-checked alloc
- aegis_memory_alloc(mem, size): bump-allocate from heap, returns offset; returns -1 if full
- aegis_memory_heap_reset(mem): reset heap bump pointer (called on explicit yield only)
- aegis_memory_push_frame(mem, return_pc): push call frame; returns false on overflow (script must exit)
- aegis_memory_pop_frame(mem, out_return_pc): pop call frame
- aegis_memory_stack_push(mem, reg): push 16-byte register value
- aegis_memory_stack_pop(mem, out_reg): pop 16-byte register value
- All load/store operations bounds-checked against zone boundaries

Files:
- include/ferrum/aegis/aegis_memory.h
- src/aegis/aegis_memory.c
- tests/aegis/aegis_memory_tests.c

Acceptance criteria:
- [ ] Three zones correctly partitioned within a single buffer
- [ ] Static array persists across heap resets
- [ ] Call stack push/pop works correctly; overflow returns error
- [ ] Heap alloc bump-allocates; returns -1 on overflow
- [ ] Heap reset clears only the heap zone, not static or stack
- [ ] All load/store operations reject out-of-bounds offsets
- [ ] Tests cover: init, static read/write, stack push/pop, stack overflow, heap alloc, heap overflow, heap reset preserves static, zone boundary enforcement

## Acceptance Criteria

Three-zone memory with bounds checking, heap reset on explicit yield only

