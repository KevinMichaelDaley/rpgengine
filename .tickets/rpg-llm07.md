---
id: rpg-llm07
status: open
deps: [rpg-zkz2, rpg-floe, rpg-lvgz]
links: [rpg-llm02a]
created: 2026-04-26T23:10:00Z
type: task
priority: 2
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, sense, light, visibility]
---
# Light Level Query API for SENSE_QUERY

Provide a runtime API to sample ambient luminance and shadow coverage at any world position, so SENSE_QUERY can compute light-aware visibility scores.

## Prerequisites

- Light system exists (rpg-zkz2): directional, point, spot lights with entity attributes.
- Tiled/clustered light culling exists (rpg-floe): per-tile light lists and intensities.
- PSSM shadow maps exist (rpg-lvgz): shadow coverage data per cascade.

## Requirements

1. **Ambient luminance sampler**:
   - Input: world position (vec3).
   - Output: scalar luminance (cd/m² or arbitrary units).
   - Sources: directional light sky contribution, point/spot lights within range, baked SH probes (rpg-sukf).
   - Accounts for light intensity, attenuation, and cone angles.

2. **Shadow coverage query**:
   - Input: world position + entity bounding sphere.
   - Output: shadow fraction (0.0 = fully lit, 1.0 = fully shadowed).
   - Uses PSSM cascades for directional shadows, cube/2D maps for point/spot.

3. **Combined visibility score**:
   - `visibility = base_visibility * f(luminance, shadow_frac)`.
   - Example: pitch darkness (luminance < 0.01) reduces visibility to 0; bright daylight (luminance > 5.0) gives full visibility.

## Integration

- `include/ferrum/npc/npc_sense_light.h` — API declarations.
- `src/npc/sense/npc_sense_light.c` — sampler implementation.
- Extend `aegis_sense_entity_t` with `luminance` and `shadow_frac` fields.

## Acceptance

- [ ] Sampled luminance at a torch-lit position is > 1.0.
- [ ] Sampled luminance inside a dark cave is < 0.01.
- [ ] Entity standing in a directional shadow has shadow_frac > 0.5.
- [ ] Performance: single query < 0.01 ms.
