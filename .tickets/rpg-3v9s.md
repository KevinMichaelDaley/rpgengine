---
id: rpg-3v9s
status: closed
deps: [rpg-9i3f]
links: []
created: 2026-07-05T22:55:22Z
type: task
priority: 1
assignee: KMD
parent: rpg-9i3f
tags: [srd, optimiser]
---
# srd-config-01: srd_descent_config.c — budget mapping

Implement srd_descent_config_from_budget and the srd_descent_config_t struct definition (in a new header srd_descent_config.h). The function fills all fields based on the four budget tiers documented in the plan. Caller must still set rules and critic pointers.

## Design

Tiers: <2s, 2-10s, 10-60s, >60s. Also set temperature_init=1.0, temperature_decay=0.995, temperature_min=0.01, lbfgs_history_size=10, lbfgs_tolerance_grad=1e-5, lbfgs_tolerance_change=1e-9 for all tiers.

## Acceptance Criteria

budget=1.0 -> k=16; budget=5.0 -> k=64; budget=30.0 -> k=256; budget=120.0 -> k=512; all other fields populated; rules and critic left as NULL (caller responsibility)

