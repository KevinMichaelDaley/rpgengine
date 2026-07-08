---
id: rpg-odqb
status: closed
deps: [rpg-j0v8, rpg-9pjm, rpg-rtxv]
links: []
created: 2026-07-05T22:57:33Z
type: task
priority: 1
assignee: KMD
parent: rpg-j0v8
tags: [srd, testing]
---
# srd-test-01: grammar property test suite (reversibility, jump-continuity)

Write tests/srd_descent_rules_tests.c: for each of the 46 built-in rules, test: (1) cond returns true for a valid selection; (2) apply succeeds; (3) for non-repair rules, apply inverse restores layout to within SRD_EPSILON (round-trip); (4) for Add* rules, L2 change in rasteriser output is < 0.01 (jump-continuity). Use the minimal test harness pattern from the codebase.

## Design

Build a canonical 6-room test layout: 3x2 grid of boxes, all adjacent, various types. For each rule: construct the minimal valid selection, apply, verify postconditions, apply inverse, verify round-trip. For rasteriser L2: call srd_layout_rasterize before and after, compute sum of squared differences.

## Acceptance Criteria

All 46 rules have at least one test; all round-trip tests pass; all jump-continuity tests pass; test file compiles against libheadless.a; no test uses malloc/free directly


## Notes

**2026-07-06T06:10:10Z**

DESIGN REVISION (2026-07-06): Rules are now voxel-based (15 rules: wall push/pull/bevel/niche, corner chamfer/round, ceiling raise/lower, floor step, corridor widen/narrow/curve, pillar, arch, convert type). Round-trip tests apply rule then inverse on the SDF grid and verify grid values return to within epsilon. Jump-continuity measured by L2 change in voxel values, not rasterizer output.
