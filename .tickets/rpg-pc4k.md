---
id: rpg-pc4k
status: closed
deps: []
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# SPSC command ring (edit_cmd_ring.c)

Implement the lock-free SPSC (single-producer single-consumer) ring buffer that bridges the editor I/O thread to the main tick thread.

READ FIRST: ref/editor_design.md §2.3 for the ring architecture, ref/editor_spec.md §2.1 for threading constraints.

The I/O thread produces parsed commands; the main tick thread consumes them during Stage 1 drain. The ring must be lock-free (atomic load/store only, no mutexes).

Requirements:
- edit_cmd_ring_t with configurable capacity (default 1024)
- edit_cmd_ring_push() → bool (false if full)
- edit_cmd_ring_pop() → bool (false if empty)
- Memory-order correct: release on push, acquire on pop
- Also implement a response ring (same structure) for main tick → I/O thread
- Must handle variable-size command payloads (JSON strings up to 1MB)
- Commands stored as: {uint32_t id, uint32_t cmd_type, char payload[]}

Files to create:
- include/ferrum/editor/edit_cmd_ring.h
- src/editor/io/edit_cmd_ring.c
- tests/editor/edit_cmd_ring_tests.c

TDD: test concurrent push/pop from two threads, full-ring behavior, empty-ring behavior, wrap-around correctness.

