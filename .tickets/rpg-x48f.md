---
id: rpg-x48f
status: closed
deps: [rpg-j0v8, rpg-3sff]
links: []
created: 2026-07-05T22:57:34Z
type: task
priority: 1
assignee: KMD
parent: rpg-j0v8
tags: [srd, testing, integration]
---
# srd-test-03: full pipeline integration test on tower_dungeon.asc

Write tests/srd_descent_loop_tests.cpp: load datasets/ascii_grids/tower_dungeon.asc, run srd_generate with a 10s budget, verify: (1) return value is 0; (2) output tile count > 0; (3) no two output boxes overlap; (4) all room-type boxes are reachable from the entrance box via adjacency graph BFS; (5) loss after optimization is lower than initial loss. Also run with 1s budget to verify it terminates on time.

## Design

Read tower_dungeon.asc with fopen. Call srd_generate. Unpack tile list. Overlap check: for all pairs (i,j) of room tiles, verify SDF overlap <= 0. Reachability BFS: find entrance box, BFS via adjacency, verify all non-corridor boxes visited.

## Acceptance Criteria

10s test completes in < 12s wall time; tile count >= number of ASCII regions; no overlap; all rooms reachable; final loss < initial loss; 1s test completes in < 2s wall time


## Notes

**2026-07-06T06:10:10Z**

DESIGN REVISION (2026-07-06): Pipeline is now ASCII → srd_seed_to_grid → srd_descent_optimize → srd_sdf_to_svo. Test verifies: (1) SVO has solid voxels around room boundaries, (2) all rooms reachable via flood-fill on the SDF grid, (3) loss decreases during optimization, (4) terminates within time budget.
