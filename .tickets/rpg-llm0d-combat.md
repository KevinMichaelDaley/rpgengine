---
id: rpg-llm0d-combat
status: deferred
deps: [combat-system-exists, rpg-llm02e]
links: []
created: 2026-04-26T23:30:00Z
type: task
priority: 3
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, npc, ai, combat, defend, attack, flee]
---
# Combat Tools: DEFEND, ATTACK, FLEE

Deferred until the combat subsystem (health, weapon range, combat BT, threat detection) exists.

## Tool IDs
| Tool | ID | Arguments |
|------|----|-----------|
| DEFEND | 5 | `{"target": "name"}` |
| ATTACK | 6 | `{"target": "name"}` |
| FLEE   | 7 | `{"direction": "optional"}` |

## Requirements (design-only)

### DEFEND
- Range check: target within 30m.
- State check: combat active or threatened.
- Engine action: set combat BT to "protect target" mode.

### ATTACK
- Range check: target within weapon range.
- State check: not already fleeing.
- Engine action: set combat BT to "engage target" mode.

### FLEE
- State check: health < 30% or outnumbered.
- Engine action: set combat BT to "escape" mode, pick nav destination away from threats.

## Blockers
- Combat state machine (health, weapon ranges, threat levels).
- Combat behavior tree system.
- Entity attribute keys for health, weapon, outnumbered status.
