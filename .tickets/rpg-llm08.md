---
id: rpg-llm08
status: open
deps: [rpg-llm07]
links: [rpg-llm02a]
created: 2026-04-26T23:10:00Z
type: task
priority: 2
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, sense, visibility]
---
# Extend SENSE_QUERY with Light-Aware Visibility Scoring

Replace the placeholder shadow-coverage row in SENSE_QUERY with real light-level data, so NPCs cannot see entities standing in deep shadow (or see better in bright light).

## Current State

SENSE_QUERY has a "Shadow coverage" row that is currently a placeholder:
| Near | Medium | Far |
|------|--------|-----|
| Full shadow map query | Approximate cell coverage | None |

No actual shadow or light data is consulted.

## Requirements

1. **Execute light query per sensed entity**:
   - For each candidate entity within range, call `npc_sense_light_query(position)`.
   - Retrieve `luminance` and `shadow_frac`.

2. **Visibility modifier**:
   - `effective_range = base_range * visibility_mod`.
   - `visibility_mod = clamp(luminance / daylight_threshold, 0.1, 1.0) * (1.0 - shadow_frac * 0.8)`.
   - Example: an entity at 30m in bright daylight is visible; the same entity in pitch darkness is not.

3. **Entity light emission**:
   - Entities carrying torches, lanterns, or glowing items emit light.
   - These entities are always visible regardless of ambient light (they are their own light source).
   - Add `EMITS_LIGHT` flag to `aegis_sense_entity_t.flags`.

## Integration

- Modify `src/npc/sense/npc_sense_execute.c` (or create `npc_sense_light_apply.c`).
- Extend `aegis_sense_entity_t` with `float luminance` and `float shadow_frac`.

## Acceptance

- [ ] Entity in bright daylight is detected at normal range.
- [ ] Same entity in deep shadow is not detected.
- [ ] Torch-carrying entity is visible in darkness.
- [ ] Tests use mock light data (no renderer required).
