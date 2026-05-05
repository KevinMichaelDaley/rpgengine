---
id: rpg-llm09
status: open
deps: [rpg-llm07]
links: [rpg-llm03, rpg-llm02a, rpg-npc01]
created: 2026-04-27T06:53:48Z
type: task
priority: 2
assignee: KMD
parent: rpg-llm03
tags: [lighting, indirect, spherical-harmonics, probes, precomputation, sh, caching]
---
# Indirect Lighting Precomputation: Spherical Harmonic Probes with Multi-Source Channels

Precompute and cache indirect lighting using spherical harmonic (SH) probes placed throughout the level.  Multiple SH channels per probe track illumination by source type (sun, sky, emissive surfaces, dynamic lights, NPC-held torches, etc.) so that the SENSE_QUERY shadow/lighting channel and NPC visibility heuristics can query directional radiance at any world-space point cheaply (< 1 µs).

## Requirements

### 1. Spherical Harmonic Probe Grid

```c
typedef struct sh_probe {
    phys_vec3_t position;         /**< World-space position. */
    float       sh_coeffs[16][9]; /**< 16 channels × 9 L2 coefficients. */
    uint32_t    flags;            /**< Valid, dirty, boundary, etc. */
} sh_probe_t;

typedef struct sh_probe_grid {
    sh_probe_t *probes;
    uint32_t    grid_res[3];      /**< Resolution in x, y, z. */
    phys_aabb_t bounds;           /**< World-space bounding box. */
    vec3_t      spacing;          /**< Spacing between probes (meters). */
    uint32_t    channel_count;    /**< Number of source channels. */
    uint32_t    total_probes;
} sh_probe_grid_t;
```

- **Order**: L2 spherical harmonics (9 coefficients per channel).
- **Placement**: Uniform 3D grid with adaptive spacing (finer indoors, coarser outdoors).
- **Channel assignment by source type**:
  - Channel 0: Sun / directional sky light.
  - Channel 1: Sky dome / ambient.
  - Channel 2: Emissive static surfaces (lava, neon signs).
  - Channel 3: Player / NPC dynamic lights (accumulated per frame).
  - Channels 4–15: Reserved for additional source categories.
- **Interpolation**: trilinear interpolation between 8 nearest probes; gradient-free, just SH dot with direction vector to evaluate radiance.

### 2. Precomputation Pipeline

Run offline (or at level-load time on a background job):

1. **Voxelize scene**: Inject static mesh surfaces into a 3D radiance volume.
2. **Direct lighting pass**: For each probe, shoot rays toward light sources; accumulate direct radiance into SH.
3. **First-bounce indirect**: Diffuse bounce from direct-lit surfaces → SH probes.
4. **Optional second bounce**: One additional inter-probe propagation step.
5. **Cache to disk**: Serialize `sh_probe_grid_t` to `.shcache` binary file alongside the level.

### 3. Runtime Query

```c
bool sh_probe_evaluate(const sh_probe_grid_t *grid,
                       phys_vec3_t world_pos,
                       phys_vec3_t normal,
                       uint32_t channel_mask,
                       float out_radiance[3]);
```

- **Cost target**: < 1 µs per query (trilinear interp + 9-dot SH evaluation).
- **Channel mask**: Bitmask of which source channels to accumulate.  SENSE_QUERY uses this to answer "how visible is this entity to the NPC under current lighting?"
- **Normal hemisphere clamp**: Optionally clamp SH evaluation to positive hemisphere (Lambertian diffuse); can be pre-tabled.

### 4. Integration with SENSE_QUERY

- SH probes provide the **visibility signal** for the `AEGIS_SENSE_SHADOW` flag.
- An entity standing in a bright spot → higher salience for NPCs looking its way.
- An entity in deep shadow → salience reduced or `AEGIS_SENSE_SHADOW` flag set.
- The `max_range` of SENSE_QUERY is modulated by the directional radiance at the querier's position: darker → shorter effective sight range.

### 5. Dynamic Light Injection

- Dynamic lights (torches, muzzle flashes, spells) are NOT baked into the SH grid.
- Instead, each frame the engine accumulates dynamic light contributions into a **separate SH channel** (channel 3) by rasterizing point/spot lights into the probe grid.
- This is done on a background job; stale data for 1–2 frames is acceptable.

## Files to Create

- `include/ferrum/lighting/sh_probe_grid.h` — probe and grid types
- `src/lighting/sh_probe_grid_init.c` — grid allocation, placement
- `src/lighting/sh_probe_bake.c` — direct + first-bounce indirect baking
- `src/lighting/sh_probe_evaluate.c` — trilinear interp + SH evaluation
- `src/lighting/sh_probe_dynamic.c` — dynamic light injection per frame
- `src/lighting/sh_probe_serialize.c` — binary cache I/O (.shcache)
- `tests/lighting/sh_probe_tests.c` — bake, evaluate, dynamic inject, serialize

## Files to Modify

- `src/npc/sense/npc_sense_auto.c` — wire SH probe shadow/salience into auto-sense
- `Makefile` — add `src/lighting/` wildcard to SRC_HEADLESS

## Acceptance

- [ ] SH probe grid builds from a test scene with known light sources.
- [ ] `sh_probe_evaluate()` returns correct radiance at grid-aligned and interpolated positions.
- [ ] Multi-channel: querying only channel 0 (sun) returns zero for emissive-only surfaces.
- [ ] Dynamic light injection updates channel 3 within 1 frame.
- [ ] Serialization round-trips without data loss.
- [ ] SENSE_QUERY salience differs for entities in bright vs shadow areas.
- [ ] Query cost < 1 µs per call.

## Blockers

- rpg-llm07 [open] Directional Light Cascade: Sun/Moon Celestial Lighting
