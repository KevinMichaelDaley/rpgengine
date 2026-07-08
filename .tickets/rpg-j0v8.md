---
id: rpg-j0v8
status: closed
deps: [rpg-3sff]
links: []
created: 2026-07-05T22:57:33Z
type: epic
priority: 1
assignee: KMD
tags: [srd, testing, validation]
---
# SRD-E7: Integration Tests and Grammar Property Validation

End-to-end integration tests validating that the redesigned SRD satisfies the four Kodnongbua et al. properties in practice, and that dungeon layouts produced are well-formed. Includes property tests (reversibility round-trips, jump-continuity bounds, local control reachability, repairability coverage), regression tests on the tower_dungeon.asc dataset, and a simple benchmark confirming loss decreases faster than the old grammar-tree implementation.

## Design

tests/srd_descent_rules_tests.c: for each of the 46 rules, apply then apply-inverse and verify layout returns to within EPSILON. tests/srd_critic_tests.cpp: gradient check for each loss term. tests/srd_descent_loop_tests.cpp: full pipeline from ASCII to tiles, verify loss decreases, verify no overlapping boxes in output, verify all rooms reachable from entrance.

## Acceptance Criteria

Reversibility: for all 41 non-repair rules, round-trip test passes (layout identical to within SRD_EPSILON after apply+inverse); Jump-continuity: all Add* rules change rasteriser L2 by < 0.01; Local control: BridgeComponents can always connect any two boxes regardless of grid origin; Repairability: after 4 repair passes, no constraint violations remain; Integration: loss decreases on tower_dungeon.asc; all rooms reachable from entrance box in final layout


## Notes

**2026-07-06T06:10:10Z**

DESIGN REVISION (2026-07-06): Tests now validate the voxel SDF pipeline. Property tests check voxel rewrite rules (apply+inverse=identity on the grid). Critic tests use grid-based metrics. Pipeline test: ASCII → seed → SDF grid → optimize → SVO. Reachability verified via flood-fill on the grid, not BFS on box adjacency.
