---
id: rust-rpg-it1.18
status: in_progress
deps: [rust-rpg-it1.1, rust-rpg-it1.15, rust-rpg-aqq]
links: []
created: 2026-01-18T22:10:22.040840466-08:00
type: task
priority: 2
parent: rust-rpg-it1
---
# P_007: Ghost table + ECS mapping tests

Write RED tests for entity ghosting and ECS mapping.

Covers:
- Create/update/destroy replication path
- ID mapping (remote entity_t -> local handle)
- Leak prevention across churn


