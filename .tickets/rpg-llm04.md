---
id: rpg-llm04
status: open
deps: [rpg-llm03]
links: [rpg-llm02a]
created: 2026-04-25T22:52:00Z
type: task
priority: 2
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, audio, propagation, sense, acoustics, beamtracing]
---
# Audio Propagation Graph for SENSE_QUERY

Replace the SENSE_QUERY auditory placeholder with a dynamic audio propagation graph built from precomputed beam tracing over static level geometry. Works in any open-world or enclosed environment without requiring explicit room definitions.

## Current State

SENSE_QUERY audio detection uses a simplified placeholder: at medium range it only checks whether doors are open or closed. Near range claims "full audio propagation graph" but no graph exists.

## Requirements

### 1. Acoustic Material Properties

Every physics body and mesh surface carries acoustic material data:

```c
typedef struct npc_acoustic_material {
    float absorption_low;    /* 0.0-1.0 @ 125 Hz  */
    float absorption_mid;    /* 0.0-1.0 @ 1 kHz   */
    float absorption_high;   /* 0.0-1.0 @ 8 kHz   */
    float scattering;        /* 0.0-1.0, diffuse reflection fraction */
    float transmission;      /* 0.0-1.0, energy passing through thin material */
} npc_acoustic_material_t;
```

- **Dynamic**: material properties are entity attributes, not static baked data.
- Physics events (wall destroyed, door opened/closed, window shattered) can mutate the material in real time.
- Default presets: wood, stone, snow, ice, metal, glass, flesh.

### 2. Precomputed Beam Tracing Graph

Instead of rooms, the graph is built by tracing acoustic beams through the static level geometry:

1. **Seed points**: uniformly sample candidate listener positions across the walkable navmesh surface.
2. **Beam emission**: from each seed, cast rays in a Fibonacci sphere pattern.
3. **Path construction**: when a ray hits geometry, record the hit point + material. Reflected beams continue with energy reduced by `(1 - absorption) * scattering`. Transmitted beams continue with energy reduced by `transmission`.
4. **Connectivity**: two seed points are connected by an edge if a beam path exists between them with total attenuation above a threshold.
5. **Edge weights**: accumulated attenuation (dB) along the best path, plus approximate path length.

```c
typedef struct npc_audio_graph_node {
    uint32_t node_id;
    vec3_t   position;
    uint32_t edge_count;
    uint32_t edge_cap;
    struct npc_audio_graph_edge *edges;
} npc_audio_graph_node_t;

typedef struct npc_audio_graph_edge {
    uint32_t to_node_id;
    float    attenuation_db;   /* total loss along best path */
    float    path_length;      /* approximate meters */
    uint32_t material_hits;    /* number of surface interactions */
} npc_audio_graph_edge_t;

typedef struct npc_audio_graph {
    npc_audio_graph_node_t *nodes;
    uint32_t                node_count;
    uint32_t                node_cap;
    /* Spatial index for fast nearest-node lookup */
    void                   *spatial_index;
} npc_audio_graph_t;
```

- The graph is **read-only during queries**; updates are batched.
- Precomputation runs offline at level-build time or on first server start.

### 3. Dynamic Local Updates

When two previously disconnected regions become adjoined (e.g., a wall is destroyed, a door is blown open, ice bridge forms):

1. Physics event flags the affected AABB region as "acoustically dirty."
2. A background job re-traces beams within the dirty region plus a 5m margin.
3. New edges are added; invalidated edges are marked stale and skipped.
4. Update is atomic: swap in the new edge list, then free the old one.

When a door opens/closes (no geometry destroyed, just material transmission changes):
1. Update the acoustic material on the door entity (`transmission` goes from 0.01 to 0.8).
2. Re-trace only beams that intersect the door's AABB.
3. This is fast enough to run synchronously (< 1 ms).

### 4. SENSE_QUERY Audio Executor

Input: sound source position + intensity (dB SPL), listener (NPC) position.
Output: audible boolean + attenuated loudness (dB) + approximate direction.

Execution:
1. Find nearest graph node to source (`ns`) and listener (`nl`).
2. Query the graph for the shortest-attenuation path `ns → nl` (Dijkstra on `attenuation_db`).
3. If no path exists, fall back to direct raycast:
   - Hit nothing → full propagation (minus inverse-square loss).
   - Hit geometry → apply hit material transmission + absorption.
4. Apply inverse-square law: `L = L_source - 20*log10(distance) - path_attenuation_db`.
5. Audible if `L > hearing_threshold` (species-dependent, e.g., human ~0 dB, wolf ~-10 dB).
6. Direction = normalized vector from listener to source (or last path node if occluded).

Multiple simultaneous sources: sum their intensities in the linear domain, then convert back to dB.

### 5. Species Hearing Profiles

Stored as entity attributes:
- `hearing_threshold` (dB SPL)
- `frequency_range_low` / `frequency_range_high` (Hz)
- `directionality` (0.0 = omnidirectional, 1.0 = highly directional like a cat)

The executor filters source intensity by frequency overlap before applying attenuation.

## Files to Create

- `include/ferrum/npc/npc_sense_audio.h` — acoustic material, graph node/edge types, API
- `src/npc/sense/npc_audio_material.c` — material presets + dynamic mutation
- `src/npc/sense/npc_audio_graph_build.c` — beam tracing precomputation (≤4 non-static functions)
- `src/npc/sense/npc_audio_graph_update.c` — dirty-region re-trace + edge swap
- `src/npc/sense/npc_audio_graph_query.c` — shortest-attenuation path + Dijkstra
- `src/npc/sense/npc_audio_hearing.c` — species profile application, loudness summation
- `tests/npc/npc_audio_graph_tests.c` — build, update, query tests

## Files to Modify

- `src/aegis/aegis_async_execute.c` — add audio query path to SENSE_TASK executor
- `include/ferrum/npc/npc_sense.h` — extend `aegis_sense_entity_t.flags` with `AUDIBLE` bit

## Acceptance

- [ ] A solid stone wall between source and listener blocks sound (attenuation > 60 dB).
- [ ] An open doorway allows sound through with minor attenuation (< 3 dB).
- [ ] Destroying a wall triggers a local graph update and creates a new low-attenuation edge.
- [ ] A closed wooden door transmits some sound (attenuation ~15 dB); opening it drops to ~2 dB.
- [ ] Wolf NPC hears a sound 10 dB below human threshold.
- [ ] Multiple simultaneous sources mix correctly (2 × identical source = +3 dB).
- [ ] Graph query performance: < 0.1 ms for 10k-node graph.
- [ ] Local update performance: < 1 ms for single-door material change.
