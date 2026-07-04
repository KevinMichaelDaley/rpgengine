---
id: rpg-a46u
status: open
deps: [rpg-nhjk, rpg-q6x7, rpg-1tgj, rpg-81mp]
links: []
created: 2026-07-04T20:41:07Z
type: task
priority: 0
assignee: KMD
parent: rpg-aqm2
tags: [procgen, critic, vlm, tdd, integration]
---
# procgen-8e: P8 integration test

rpg-aqm2

## Design

Integration test: run a 2-playthrough critic session with mock VLM (returns predictable scores). Verify: screenshots captured at expected events, VLM called with correct data, scores returned and stored, aggregate stats computed correctly. Test edge cases: VLM timeout, VLM returns malformed response, empty screenshot. Test that P8 does not break P7 critic functionality when VLM is disabled.

## Acceptance Criteria

- Screenshots captured and sent to VLM\n- Scores returned and stored\n- Aggregate stats computed correctly\n- VLM errors handled gracefully\n- Critic works without VLM (disabled mode)\n- RED-GREEN-REFACTOR complete

