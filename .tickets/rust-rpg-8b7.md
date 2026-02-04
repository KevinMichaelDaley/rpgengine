---
id: rust-rpg-8b7
status: closed
deps: []
links: []
created: 2026-02-01T22:57:07.331425379-08:00
type: task
priority: 2
---
# Server: global state update queue consumed by simulation jobs

Implement the global state update queue for server networking -> simulation jobs.

Requirements:
- Client fibers push decoded messages/commands into a global queue.
- Simulation jobs pop and apply updates without blocking.
- Clearly define ownership: queue nodes either copy payload bytes or reference stable storage with lifetime guarantees.

Deliverables:
- Queue API + implementation (lock-free or low-contention).
- Tests for ordering constraints (per-client ordering for reliable commands) and multi-producer behavior.
- Integration notes for how simulation systems subscribe/consume.


