---
id: rpg-2ob4
status: closed
deps: []
links: []
created: 2026-02-09T04:35:45Z
type: epic
priority: 2
assignee: KMD
tags: [server, architecture, loop]
---
# Design main server loop

Design and document the production main server loop (no demo code) with clear subsystem boundaries.

Goal: a minimal, correct, testable loop that drives:
- networking pumps (RX/TX)
- simulation/gameplay dispatch
- physics tick runner (async + barrier)
- replication tick/broadcast

This should replace any lingering conceptual references to the removed demo loop.


## Design

Design sketch:
- Identify required threads/fibers (main thread, net pump thread, topic pump thread, sim job system, net job system).
- Define tick cadence + catch-up policy (max catch-up ticks, fixed timestep accumulator).
- Define ordering contracts:
  - drain inbound messages/events
  - kick physics (async)
  - run gameplay jobs / state update queue
  - run replication encode
  - wait for physics barrier if needed before swapping buffers / publishing state
- Define shutdown + backpressure handling.

Deliverables likely include a ref doc + a small integration test harness that exercises the loop without SDL/graphics.


## Acceptance Criteria

Acceptance criteria:
- A single authoritative doc describing loop ordering + threading.
- Minimal integration test proves:
  - loop advances ticks deterministically
  - physics job dispatch + wait works
  - replication tick executes without deadlocks
- No dependency on removed demo module.

