---
id: rpg-4mwa
status: open
deps: [rpg-5j3b]
links: []
created: 2026-07-05T06:25:47Z
type: task
priority: 2
assignee: KMD
parent: rpg-t6ia
tags: [procgen]
---
# srd-008: srd_anneal.cpp -- temperature schedule

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Temp annealing: start 0.5, decay 0.995/step, floor 0.01. RED-phase: verify decay curve.

## Acceptance Criteria

Decay matches expected values at steps 0, 100, 1000; converges to 0.01 floor

