---
id: rpg-llm05
status: open
deps: [rpg-llm03]
links: []
created: 2026-04-25T22:52:00Z
type: task
priority: 2
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, scent, wind, simulation, sense]
---
# Wind and Scent Simulation for SENSE_QUERY

Replace the SENSE_QUERY smell placeholder with a particle-based scent diffusion model driven by wind.

## Current State
SENSE_QUERY smell detection uses a downwind cone approximation at medium range and "exact wind + scent simulation" at near range, but no simulation exists.

## Requirements

1. **Wind field**: A coarse 3D grid or vector field over the game world.
   - Updated periodically (every few seconds) based on weather state.
   - Obstacles (buildings, terrain) cause turbulence and dead zones.

2. **Scent emitters**: Entities tagged with scent type + intensity.
   - Types: blood, food, smoke, sweat, monster musk, etc.
   - Intensity decays over time and distance.

3. **Particle-based diffusion**:
   - Emit scent particles from sources each tick.
   - Advect particles by wind field.
   - Diffuse with a simple Laplacian or random walk.
   - Particles deposit scent concentration on a coarse spatial grid.

4. **SENSE_QUERY smell executor** samples the scent grid:
   - Input: NPC position + olfaction radius (species-dependent).
   - Output: list of detectable scents with type, intensity, approximate direction.
   - Animals/monsters get larger radii and finer resolution than humans.

## Integration
- `src/npc/sense/npc_sense_scent_field.c` — wind field + particle advection + scent grid
- `include/ferrum/npc/npc_sense_scent.h` — scent types and field API
- Extend `aegis_sense_entity_t.flags` with `SMELLED` bit.

## Acceptance
- [ ] Scent from a campfire spreads downwind and is detectable by a wolf NPC.
- [ ] A deer upwind of a wolf is not detected by smell.
- [ ] Scent concentration decays to zero after the source is removed.
- [ ] Performance: 100 scent sources × 60 Hz stays under 1 ms per tick.
