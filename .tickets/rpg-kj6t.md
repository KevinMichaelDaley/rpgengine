---
id: rpg-kj6t
status: closed
deps: [rpg-bux3]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-8sc6
tags: [procgen, critic, hooks]
---
# procgen-5e: Stuck/fall/OOB detection hooks

## Design

Implement stuck detection (player position delta < threshold for N consecutive seconds → FR_CRITIC_EVENT_STUCK), fall-OOB detection (player Z < world_min_z → FR_CRITIC_EVENT_FELL_OOB), and playthrough timeout (elapsed > playthrough_timeout → FR_CRITIC_EVENT_TIMEOUT). Write RED tests for each detection path.

## Acceptance Criteria

- Stuck: no movement for N seconds → STUCK event\n- Fall: Z < min → FELL_OOB event\n- Timeout: elapsed > limit → TIMEOUT event\n- Stuck configurable: threshold distance + duration\n- Events fire exactly once per condition per playthrough\n- Reset between playthroughs

