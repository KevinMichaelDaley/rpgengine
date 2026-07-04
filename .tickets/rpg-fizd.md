---
id: rpg-fizd
status: open
deps: []
links: []
created: 2026-07-04T20:40:15Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, nitrogen, ipc, shm, tdd]
---
# procgen: Phase 6 - NitroGen IPC

rpg-o9fl

## Design

Build the IPC bridge between the C engine and the NitroGen Python inference process. The C engine renders frames into a shared memory ring buffer; NitroGen reads frames and outputs gamepad actions via a Unix pipe. Protocol: fixed-size RGB frames at configured resolution, JSON-formatted actions with joystick axes and button states. Python side: nitrogen_bridge.py loads the NitroGen model from HuggingFace and runs inference in a loop. This phase is infrastructure only — no agent gameplay yet (that's P7).

## Acceptance Criteria

- Shared memory ring buffer: producer (C) writes frames, consumer (Python) reads\n- Frame protocol: fixed resolution (e.g., 224x224 RGB), no compression\n- Action pipe protocol: JSON with lx,ly (joystick), rx,ry (camera), buttons\n- C side: frame write function writeable from renderer thread\n- C side: action read function callable from input/player thread\n- Python bridge: loads NitroGen model, reads frames, runs inference, writes actions\n- Ring buffer handles underrun (no new frame) and overrun (consumer slow)\n- Clean shutdown: stop flag via pipe/signal\n- Works with mock Python agent for testing (no model needed)

