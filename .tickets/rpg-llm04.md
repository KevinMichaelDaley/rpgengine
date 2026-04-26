---
id: rpg-llm04
status: open
deps: [rpg-llm03]
links: []
created: 2026-04-25T22:52:00Z
type: task
priority: 2
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, audio, propagation, sense]
---
# Audio Propagation Graph for SENSE_QUERY

Replace the SENSE_QUERY auditory placeholder with a real door/wall occlusion graph that models sound propagation through the game world.

## Current State
SENSE_QUERY audio detection uses a simplified placeholder: at medium range it only checks whether doors are open or closed. Near range claims "full audio propagation graph" but no graph exists.

## Requirements

1. **Build audio propagation graph** from level geometry:
   - Nodes: rooms, open areas, doorways, windows.
   - Edges: direct acoustic coupling between connected spaces.
   - Edge weights: estimated attenuation (dB) based on distance, material absorption, and occlusion.

2. **Material properties** on physics bodies / mesh surfaces:
   - Absorption coefficients per material (wood, stone, snow, etc.).
   - Stored as entity attributes or mesh metadata.

3. **SENSE_QUERY audio executor** queries the graph:
   - Input: sound source position + intensity, listener (NPC) position.
   - Output: audible boolean + attenuated loudness + direction.
   - Supports multiple simultaneous sources (mixed loudness).

4. **Dynamic updates**: Doors opening/closing, walls being destroyed update the graph in real time.

## Integration
- `src/npc/sense/npc_sense_audio_graph.c` — graph build/update/query
- `include/ferrum/npc/npc_sense_audio.h` — graph types
- Extend `aegis_sense_entity_t.flags` with `AUDIBLE` bit.

## Acceptance
- [ ] A closed door between source and listener blocks or heavily attenuates sound.
- [ ] An open doorway allows sound through with minor attenuation.
- [ ] Destroying a wall updates the graph and removes occlusion.
- [ ] Multiple NPCs can query the same shared graph safely (read-only during queries).
