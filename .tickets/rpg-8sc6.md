---
id: rpg-8sc6
status: open
deps: []
links: []
created: 2026-07-04T20:39:26Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, critic, hooks, tdd]
---
# procgen: Phase 5 - Critic Hook System

rpg-o9fl

## Design

Implement the engine hook system for the critic playtester. Hooks register callbacks that fire on specific events during gameplay: player death, marker reached, player fell out of bounds, playthrough timeout, player stuck (no progress). Each event captures position, velocity, elapsed time, playthrough ID, and event-specific data (marker name, damage source, etc.). Hooks are grammar-agnostic — the critic only needs spawn position + markers list + nav graph.

## Acceptance Criteria

- Death hook fires when player health reaches 0\n- Marker hook fires when player enters marker proximity radius\n- Fell-OOB hook fires when player falls below world floor\n- Timeout hook fires when playthrough exceeds time limit\n- Stuck hook fires when no progress for N seconds\n- Each hook receives full event context (position, time, ID)\n- Multiple hooks can be registered simultaneously\n- Hooks can be registered/unregistered at runtime\n- Thread-safe for physics fiber context

