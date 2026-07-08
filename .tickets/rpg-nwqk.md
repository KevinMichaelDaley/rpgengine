---
id: rpg-nwqk
status: closed
deps: [rpg-3sff, rpg-mnwh, rpg-053s, rpg-vy6w, rpg-rtxv]
links: []
created: 2026-07-05T22:56:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-3sff
tags: [srd, integration]
---
# srd-loop-01: srd_descent_loop.cpp — outer loop

Implement srd_descent_optimize: the main outer loop calling continuous and discrete phases in alternation with temperature annealing. Track elapsed time via clock_gettime. Return final loss value. Expose a C-compatible entry point srd_descent_optimize_c for calling from srd_bridge.

## Design

srd_descent_optimize(srd_sdf_layout_t *layout, const srd_descent_config_t *cfg): struct timespec t0; clock_gettime. While elapsed < budget: srd_continuous_phase(layout,cfg); current_loss = critic.score(...); srd_discrete_phase(layout, current_loss, cfg, &rng); T *= decay. Log loss every 10 outer iterations if verbose. Return current_loss.

## Acceptance Criteria

Loop terminates within 110% of time_budget_s; loss at end <= loss at start on a 4-room test layout; temperature decreases monotonically; verbose mode prints loss each 10 iterations; no memory leak across iterations (no heap allocation inside loop)


## Notes

**2026-07-06T06:09:51Z**

DESIGN REVISION (2026-07-06): The outer loop no longer alternates continuous/discrete phases. It is a pure discrete rewrite loop: sample K candidate rule applications on the srd_sdf_grid_t, evaluate each via the grid-based critic, select via greedy max-cover, apply, run repair pass. Temperature annealing controls exploration. No L-BFGS. The critic evaluates grid-based metrics (room volume, flood-fill reachability, bounds violation). Depends on E8 (grid type) and E9 (rewrite rules).
