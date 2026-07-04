---
id: rpg-piy6
status: open
deps: [rpg-0d3z, rpg-dquq, rpg-hp1f]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-fizd
tags: [procgen, nitrogen, ipc, tdd, integration]
---
# procgen-6d: P6 integration test

rpg-fizd

## Design

Integration test: C side creates shared memory + pipe, writes test frames, Python mock agent reads frames and sends actions, C side reads actions and verifies roundtrip. Test buffer underrun/overrun. Test pipe timeout. Test clean shutdown sequence. Test with actual NitroGen model if available (optional, manual test).

## Acceptance Criteria

- Frame roundtrip: C → shm → Python → verified\n- Action roundtrip: Python → pipe → C → verified\n- Buffer underrun/overrun handled\n- Pipe timeout works\n- Clean shutdown: no orphaned processes or shared memory\n- RED-GREEN-REFACTOR complete

