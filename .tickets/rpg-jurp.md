---
id: rpg-jurp
status: in_progress
deps: [rpg-r82r, rpg-mfdj, rpg-rnno, rpg-icvq]
links: []
created: 2026-07-05T06:51:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-022: Milestone 3 smoke — loss composition integration

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Integration test after eikonal (srd-018), transport (srd-019), loss primitives (srd-016), and loss compiler (srd-017) are complete. Parse a LOSS: expression alongside a full multi-floor dungeon ASCII grid (12+ rooms, 2 floors), build the energy tree, verify it produces correct values and gradients for complex multi-term losses involving PDE field solvers.

Test the loss on a real dungeon scale: PathDistance between entrance and the farthest room on each floor, LineOfSight between guard rooms and treasure room across floor boundaries, Separation between noisy (B) and quiet (P) room types, plus NonPenetration and MinimumSize on all rooms. Verify each term contributes to total energy, verify eikonal solver propagates T field across the full grid, verify transport solver produces R field through occlusion, verify gradient composition is correct.

RED-phase: tests/procgen/srd/srd_m3_loss_smoke.cpp — full 2-floor 12-room ASCII grid + multi-term LOSS expression; verify energy values are sum of components, verify PathDistance includes eikonal-determined shortest path length, verify LineOfSight includes transport-determined visibility, verify composite gradient equals sum of individual gradients.

## Acceptance Criteria

Loss compiler produces correct energy tree with all 10+ terms; PathDistance energy includes full eikonal solve across multi-floor grid; LineOfSight energy includes transport solve through occlusion; composite energy equals weighted sum of components; composite gradient matches sum of individual gradients; eikonal T field propagates through corridors and stairs between floors; no NaN in any field value or gradient

