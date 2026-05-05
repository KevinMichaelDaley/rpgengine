---
id: rpg-scent01
status: open
deps: [rpg-llm05]
links: [rpg-llm05]
created: 2026-05-03T20:00:00Z
type: bug
priority: 3
assignee: KMD
parent: rpg-llm05
tags: [scent, bug, diffusion, simulation]
---
# Scent Simulation Has No Diffusion Step

`npc_scent_advect` in `src/npc/sense/npc_sense_scent.c` only shifts scent concentrations by wind — there is no Laplacian/Gaussian diffusion. Scent stays in a tight concentration even after many ticks without wind, which is physically unrealistic.

## Fix
Add a diffusion pass after advection:
```c
for each cell:
    new[cell] = grid[cell] * (1 - 6*k) + k * sum(neighbors)
```
Where k is the diffusion coefficient (e.g., 0.1 per tick). This can be implemented as a simple 6-neighbor box blur.

## Acceptance
- [ ] Scent spreads to adjacent cells over time even with zero wind
- [ ] Concentration is conserved (no mass loss from diffusion)
- [ ] Performance: diffusion pass < 100 µs at 32³ resolution
