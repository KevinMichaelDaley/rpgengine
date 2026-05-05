---
id: rpg-npc04
status: open
deps: [rpg-npc01]
links: [rpg-npc01]
created: 2026-05-03T20:00:00Z
type: bug
priority: 3
assignee: KMD
parent: rpg-npc01
tags: [npc, bug, prompt, awareness, sorting]
---
# Awareness Summary Takes First 5 Entries Unsorted

`npc_state_prompt_assemble` in `src/npc/state/npc_state_prompt.c:22` picks the first 5 awareness entries regardless of salience:
```c
uint32_t show = aw->count < 5 ? aw->count : 5;
```
The 5 most important entities (highest salience) may be missed if they appear later in the array.

## Fix
Sort the awareness entries by descending salience before picking the top N, or maintain the awareness list in sorted order. Use a simple insertion sort (N ≤ number of entities in range, typically < 50).

## Acceptance
- [ ] Top-5 awareness entries are the highest-salience entities
- [ ] Entries with salience 0 are excluded
- [ ] Performance: sorting overhead < 1 µs for typical awareness list sizes
