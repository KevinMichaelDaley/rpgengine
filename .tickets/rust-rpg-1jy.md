---
id: rust-rpg-1jy
status: closed
deps: []
links: []
created: 2026-02-02T15:16:07.985796455-08:00
type: task
priority: 2
---
# Queue sharding and CPU affinity

Implement sharded queue bias + stealing and optional Linux CPU affinity. Add APIs: job_system_queue_is_sharded, job_dispatch_to, job_system_enable_affinity, job_system_affinity_enabled. Add tests verifying sharding behavior and affinity toggling. Ensure deterministic mode fairness preserved. Push changes.


