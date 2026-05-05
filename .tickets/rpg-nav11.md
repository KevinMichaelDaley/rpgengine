---
id: rpg-nav11
status: closed
deps: [rpg-llm0d-nav]
links: []
created: 2026-05-03T20:00:00Z
type: bug
priority: 2
assignee: KMD
parent: rpg-llm02
tags: [navigation, bug, target-resolution, goto]
---
# npc_nav_action_resolve_target Always Returns False

`npc_nav_action_resolve_target` in `src/npc/nav/npc_nav_action.c:18-27` discards all parameters and unconditionally returns `false`:
```c
(void)nw; (void)actor_id;
return false;
```
The comment admits: *"Named entity lookup and landmark table are engine-level features. For now, always return false."*

Any code path that relies on resolving a target string to a world position gets a hard failure.

## Fix
Implement entity name lookup via ECS query (iterate entities, match name attribute) and a static landmark table (key-value store of name→position). Both require engine integration.

## Acceptance
- [ ] Named entity "player_42" resolves to its position
- [ ] Landmark "iron_forge" resolves to its position
- [ ] Unknown target returns false with clear distinction from "not implemented"
