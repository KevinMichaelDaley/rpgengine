---
id: rpg-x7eg
status: open
deps: [rpg-re46]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-ktq6
tags: [aegis, vm, async]
---
# Aegis async instructions (vis_test, nav_query, poll, wait)

Implement async bytecode instructions per ref/aegis_bytecode_spec.md §3.3, §3.5.

Instructions:
- vis_test r_handle, r_origin, r_ray_vec: submit async raycast; direction of r_ray_vec gives ray direction, magnitude gives max distance; allocates result slot in heap arena, enqueues task to async buffer, stores handle in r_handle
- nav_query r_handle, r_from, r_to: submit async nav mesh query between two points; same pattern as vis_test
- poll r_result, r_flag, r_handle: read async task status (non-blocking); if COMPLETE: copy result into r_result, set r_flag to status; if PENDING: r_flag = PENDING; if ERROR: r_flag = error code
- wait r_result, r_flag, r_handle: execute poll; if PENDING: yield from script fiber (job_yield()); on next resume, re-execute wait (PC does not advance); if COMPLETE/ERROR: behave like poll and advance past instruction

wait yields the FIBER (not just the script coroutine) via job_yield(), ensuring the script thread can run other scripts while waiting. The heap arena is NOT reset on wait-yield (async result slots must survive).

Enforces max_async_tasks limit per yield (default 16).

Files:
- src/aegis/ops/aegis_ops_async.c (vis_test, nav_query — submission + handle allocation)
- src/aegis/ops/aegis_ops_poll.c (poll, wait — status reading + fiber yield)
- tests/aegis/aegis_ops_async_tests.c

Acceptance criteria:
- [ ] vis_test allocates result slot, enqueues task, returns handle
- [ ] nav_query same pattern as vis_test
- [ ] poll reads PENDING status correctly (non-blocking)
- [ ] poll reads COMPLETE status and copies result into register
- [ ] wait yields fiber when PENDING; resumes and re-polls on next tick
- [ ] wait advances PC when COMPLETE
- [ ] Heap arena preserved across wait-yield
- [ ] max_async_tasks enforced; excess → error
- [ ] Tests: submit + poll (pending), submit + complete + poll (result), wait loop with simulated completion, multiple async ops, limit exceeded

## Acceptance Criteria

Async ops submit tasks, poll reads results, wait yields fiber on pending, heap survives

