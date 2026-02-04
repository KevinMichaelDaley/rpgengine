---
id: rust-rpg-g98
status: open
deps: []
links: []
created: 2026-02-01T22:57:07.486568223-08:00
type: feature
priority: 2
---
# Spawned-entity management job: arena/pool-fed, fiber-safe

Implement spawned-entity management as a fiber-safe job system module, fed by the networking channel abstraction.

Requirements:
- Subscribes to spawn-related topics/commands.
- Allocates entities/components using an arena allocator supplied to the job, backed by a pool reserved for network-driven allocations.
- Idempotent application: duplicate spawn commands must not allocate duplicates.
- No socket/protocol knowledge here; consumes only decoded messages from reliable UDP stream/channel.

Deliverables:
- Module boundary + public header.
- Tests for idempotency and arena allocation behavior.
- Integration with ECS entity creation.


