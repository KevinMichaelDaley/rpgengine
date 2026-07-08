---
id: rpg-3sff
status: closed
deps: [rpg-ct3l]
links: []
created: 2026-07-05T22:56:50Z
type: epic
priority: 1
assignee: KMD
tags: [srd, integration]
---
# SRD-E6: Full SRD Descent Loop and Bridge Rewire

The complete SRD outer loop (srd_descent_loop.cpp) combining continuous and discrete phases, and the rewired srd_bridge.cpp that replaces the old grammar-tree pipeline. The outer loop runs until time_budget_s is exhausted, alternating continuous (L-BFGS) and discrete (K-candidate + max-cover) phases with temperature annealing. srd_bridge.cpp parses ASCII, builds SDF layout via srd_sdf_layout_from_grid, runs srd_descent_optimize, and converts the final layout to tiles for output. All SymX code is removed.

## Design

srd_descent_optimize(layout, config): outer loop. Each iteration: srd_continuous_phase, srd_discrete_phase, T *= decay. Returns final loss. srd_bridge.cpp srd_generate: parse ASCII lines (keep existing parser), call srd_grid_parse, call srd_sdf_layout_from_grid, build rule table + critic from config, call srd_descent_optimize, convert layout boxes to srd_tile_list_t via srd_layout_to_tiles.

## Acceptance Criteria

Full pipeline: srd_generate on a valid ASCII floor plan completes without crash within budget; final layout has no overlapping boxes (repairs ran); loss is lower after optimization than before; srd_bridge.cpp contains no #include symx or SymX references; srd_loop.cpp, srd_energy.cpp, srd_optimizer.cpp, srd_eikonal.cpp, srd_transport.cpp, srd_loss_primitives.cpp, srd_loss_compiler.cpp, srd_loss_gradient.cpp are removed from the build


## Notes

**2026-07-06T06:09:51Z**

DESIGN REVISION (2026-07-06): The SRD optimizer now uses a voxel SDF grid (srd_sdf_grid_t) instead of box arrays. The continuous L-BFGS phase is dropped — optimization is purely discrete (temperature-annealed rewrite rules on the grid). The bridge outputs an SVO (npc_svo_grid_t) via SDF booleanization, not a tile list. Depends on E8 (voxel grid foundation) and E9 (voxel rewrite rules).
