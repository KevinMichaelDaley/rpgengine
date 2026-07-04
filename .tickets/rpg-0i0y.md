---
id: rpg-0i0y
status: open
deps: [rpg-bux3]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-8sc6
tags: [procgen, critic, hooks, death]
---
# procgen-5c: Death detection hook

## Design

Wire death detection into the player entity. When player health reaches 0 or player calls a die() function: fire FR_CRITIC_EVENT_DEATH with player position, velocity, damage_source (0=fall, 1=combat, 2=environment), and death position. Register this hook in the critic runtime. Write RED test: create test player, trigger death, verify hook fires with correct event data.

## Acceptance Criteria

- Death hook fires on player death\n- Event includes correct position and velocity\n- damage_source classified correctly\n- Hook fires before entity destruction\n- Multiple death events per playthrough tracked

