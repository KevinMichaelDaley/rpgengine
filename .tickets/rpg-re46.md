---
id: rpg-re46
status: open
deps: [rpg-o8pq]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-ktq6
tags: [aegis, vm, async]
---
# Aegis async task buffer (MPSC ring)

Implement the MPSC async task buffer per ref/aegis_bytecode_spec.md §3.5 and §8.

The async task buffer is a lock-free ring that bridges script fibers (producers) and the world subsystem (consumer). Scripts enqueue async operations (raycasts, nav queries); the world subsystem drains and executes them without contention.

Implement:
- aegis_async_task_t: task descriptor with atomic status, task_type, result_ptr (points into script heap arena), result_cap, params[64]
- aegis_async_buffer_t: MPSC ring buffer (multiple script fibers can enqueue, world subsystem drains)
- aegis_async_buffer_init(buf, capacity): allocate task slots
- aegis_async_buffer_submit(buf, task): enqueue task (atomic, lock-free)
- aegis_async_buffer_drain(buf, out_tasks, max_tasks): dequeue pending tasks for execution
- Status enum: AEGIS_ASYNC_PENDING (0), AEGIS_ASYNC_COMPLETE (1), AEGIS_ASYNC_ERROR (2)

Result flow:
1. Script allocates result slot in heap arena (survives wait-yields)
2. Task's result_ptr points to this slot
3. World subsystem writes result data to result_ptr on completion
4. World subsystem atomically sets status to COMPLETE (release ordering)
5. Script reads status via poll/wait (acquire ordering)

Files:
- include/ferrum/aegis/aegis_async.h
- src/aegis/aegis_async_buffer.c
- tests/aegis/aegis_async_buffer_tests.c

Acceptance criteria:
- [ ] Submit/drain round-trip works correctly
- [ ] Multiple producers can submit concurrently (MPSC)
- [ ] Status transitions are atomic with correct memory ordering
- [ ] Result data written to result_ptr is visible after status reads COMPLETE
- [ ] Buffer full → submit returns error
- [ ] Tests: single submit/drain, concurrent multi-producer (2 threads), status transitions, buffer overflow

## Acceptance Criteria

MPSC async task buffer with atomic status transitions, lock-free submit/drain

