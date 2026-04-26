---
id: rpg-llm09
status: open
deps: [rpg-llm07]
links: [rpg-llm08]
created: 2026-04-26T23:10:00Z
type: task
priority: 3
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, sense, species]
---
# NPC Light Sensitivity Attributes (Night Vision, Photophobia)

Add per-species and per-NPC light sensitivity modifiers so that e.g. wolves see better in low light, while cave-dwelling monsters are blinded by bright light.

## Requirements

1. **Species baseline**:
   - `light_sensitivity` float: multiplier on effective luminance.
   - `night_vision` bool: if true, minimum visibility_mod is 0.3 instead of 0.0.
   - `photophobia` bool: if true, bright light (luminance > 10.0) reduces visibility.

2. **Entity attribute integration**:
   - Store sensitivity as entity attributes (SCRIPT_KEY_LIGHT_SENSITIVITY, etc.).
   - Default values per species defined in spawn tables.

3. **SENSE_QUERY application**:
   - `effective_luminance = sampled_luminance * light_sensitivity`.
   - Apply night_vision floor and photophobia ceiling before computing visibility_mod.

## Integration

- `include/ferrum/npc/npc_sense_light.h` — add sensitivity types.
- `src/npc/sense/npc_sense_light_apply.c` — apply per-NPC modifiers.

## Acceptance

- [ ] Wolf NPC detects prey at 0.1 luminance (human cannot).
- [ ] Cave monster is blinded by torchlight (visibility drops).
- [ ] Sensitivity values are loaded from entity spawn config.
