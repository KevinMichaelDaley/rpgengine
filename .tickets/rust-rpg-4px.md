---
id: rust-rpg-4px
status: in_progress
deps: []
links: []
created: 2026-02-01T22:57:07.118193593-08:00
type: feature
priority: 2
---
# Server networking: one fiber per client + per-client channels

Re-architect server networking to use one fiber per connected client scheduled by the job system.

Requirements:
- Each client fiber owns at least two fiber-local channels: reliable and unreliable.
- Fiber reads incoming packets, reconstructs streams per channel, and publishes decoded inputs to a global update queue.
- Fiber polls outbound per-client channels to serialize/send updates.
- Ensure at least two OS worker threads are dedicated to running client fibers.

Deliverables:
- Server net runtime module with explicit per-client context.
- Scheduling integration with job system.
- Benchmarks/regression tests to ensure 100–200 clients make progress.


