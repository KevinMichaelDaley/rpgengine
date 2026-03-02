---
id: rpg-dyq5
status: open
deps: []
links: []
created: 2026-03-01T23:45:19Z
type: feature
priority: 1
assignee: KMD
parent: rpg-p9zq
---
# Event signaling opcodes (SIGNAL, SUBSCRIBE, AWAIT_EVENT) and exit-driven unscheduling

## Overview

Add event signaling opcodes to the Aegis VM, enabling scripts to:
1. **Signal** events to the server (rate-limited to prevent queue/job saturation)
2. **Subscribe** to event topics and **await** specific events
3. **Auto-unschedule** on EXIT if a scripts subscribed event does not fire within a grace period

## New Opcodes

### SIGNAL (0x42)
`signal r_dst, r_topic_hash, r_payload_reg`

Sends an event to the server. The server enqueues it into the topic tables event routing system with a **rate limit** (~1 event per 200-500 microseconds per script, configurable) to prevent saturation of the event queue or creation of excessive jobs.

- `r_topic_hash`: i32 register containing the topic hash (scripts define custom topic names, represented internally as integer IDs via `aegis_topic_hash()`)
- `r_payload_reg`: register containing event payload data (up to 16 bytes from register, rest zeroed)
- `r_dst`: receives an integer status code:
  - 0 = success (event enqueued)
  - 1 = rate-limited (too many signals, try again next tick)
  - 2 = invalid topic
  - 3 = queue full

Events sent via SIGNAL are subscribable by other scripts through the existing topic table infrastructure.

### SUBSCRIBE (0x43)
`subscribe r_dst, r_topic_hash`

Subscribes the calling script to a topic. Returns status in r_dst:
- 0 = success
- 1 = already subscribed
- 2 = subscription table full

This wires into the existing `aegis_topic_subscribe()` infrastructure.

### AWAIT_EVENT (0x44)
`await_event r_dst, r_topic_hash`

Polls the scripts event queue for an event matching the given topic hash. Behaves like WAIT (does not advance PC if no matching event yet):

- If a matching event is found: pops it from the queue, writes the event struct into r_dst (type + source + status info packed into 16 bytes), returns true (advances PC)
- If no matching event: returns false, VM goes to WAIT_YIELDED state (does not advance PC, yields to scheduler)

The result code packed into r_dst can indicate:
- Event hasnt fired (still waiting)
- Event arrived with a status code (which could be the result code from another scripts SIGNAL)

### EXIT Behavior Change
When a script executes EXIT (existing opcode), the runtime should:
1. Mark the script as "pending unschedule" rather than immediately destroying it
2. Track an `idle_ticks` counter on the instance
3. If the scripts subscribed event fires again within a grace window (configurable, default ~4-8 ticks), reset the counter and re-enter the script
4. If the grace window expires without a matching event, fully unschedule the script (set active=false, free resources)

This allows event-driven scripts to efficiently handle bursty events without the overhead of spawn/destroy cycles.

## Implementation Scope

### Types / Headers
- Add 3 new opcodes to `aegis_opcode_t` enum in `aegis_types.h` (0x42, 0x43, 0x44)
- Add `aegis_ops_signal.h` with handler declarations
- Add `signal_rate_limit_us` (uint32_t) and `idle_grace_ticks` (uint32_t) to config or runtime
- Add `last_signal_time_us` (uint64_t) and `idle_ticks` (uint32_t) and `pending_unschedule` (bool) to `aegis_script_instance_t`

### Source Files
- `src/aegis/ops/aegis_ops_signal.c` — SIGNAL + SUBSCRIBE handlers (2 non-static fns)
- `src/aegis/ops/aegis_ops_await.c` — AWAIT_EVENT handler (1 non-static fn)
- `src/aegis/aegis_runtime_signal.c` — runtime-side signal processing: rate-limit check, enqueue, publish (≤4 non-static fns)
- `src/aegis/aegis_runtime_idle.c` — exit-driven idle tracking: mark pending, tick idle counter, unschedule expired (≤4 non-static fns)
- Wire opcodes into `aegis_vm_run.c` switch

### IL Changes
- Add SIGNAL, SUBSCRIBE, AWAIT_EVENT to assembler keyword table
- Add encoding for the 3 new instruction formats

### Test Files
- `tests/aegis/aegis_ops_signal_tests.c` — unit tests for SIGNAL + SUBSCRIBE handlers
- `tests/aegis/aegis_ops_await_tests.c` — unit tests for AWAIT_EVENT handler  
- `tests/aegis/aegis_runtime_signal_tests.c` — runtime signal processing + rate limiting tests
- `tests/aegis/aegis_runtime_idle_tests.c` — idle tracking + unschedule tests
- `tests/aegis/aegis_signal_integration_tests.c` — end-to-end: script signals → topic routes → subscriber awaits → receives event

## Rate Limiting Design
Per-script timestamp tracking (monotonic clock). On SIGNAL:
1. Read current time
2. If `now - last_signal_time_us < signal_rate_limit_us`, return status=1 (rate-limited)
3. Otherwise, build aegis_event_t from params, publish via topic table, update timestamp, return status=0

## Dependencies
- Depends on existing event queue infrastructure (aegis_event.h)
- Depends on existing topic table (aegis_topic_subscribe/publish)
- Depends on existing runtime instance management
- Parent: rpg-p9zq (Phase 3: Scripting)

