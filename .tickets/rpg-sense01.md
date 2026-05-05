---
id: rpg-sense01
status: closed
deps: [rpg-llm02a]
links: [rpg-llm02a]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [sense, bug, memory-corruption, auto-sense]
---
# Hardcoded Stride 15 in npc_sense_auto Ignores Flexible name[] Member

`npc_sense_auto_update` in `src/npc/sense/npc_sense_auto.c:149` iterates sense result entities with a hardcoded stride:
```c
ent_ptr += 15; /* stride over aegis_sense_entity_t */
```
But `aegis_sense_entity_t` has a flexible array member `char name[1]` and its actual size is `15 + strlen(name)`. Entities with non-empty names cause misalignment — reading subsequent entities yields garbage data (wrong entity_id, distance, salience, flags).

## Root Cause
The stride assumes every `aegis_sense_entity_t` is exactly 15 bytes (entity_id=4 + distance=4 + salience=4 + flags=2 + name[1]=1). But `aegis_sense_entity_size()` computes the correct size as `15 + strlen(name)`.

## Fix
Replace `ent_ptr += 15` with `ent_ptr += aegis_sense_entity_size(ent->name)`.

Also audit the sense executor (`aegis_async_execute.c`) to ensure entities are written with correct stride.

## Acceptance
- [ ] Auto-sense correctly parses entities when names are non-empty
- [ ] Stride matches `aegis_sense_entity_size()` output
- [ ] Test with entities that have names of varying lengths
