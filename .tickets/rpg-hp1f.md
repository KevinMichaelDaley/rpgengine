---
id: rpg-hp1f
status: closed
deps: [rpg-0d3z, rpg-dquq]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-fizd
tags: [procgen, nitrogen, python, bridge]
---
# procgen-6c: Python NitroGen bridge script

## Design

Create tools/nitrogen_bridge.py: a Python script that loads the NitroGen model from HuggingFace (nvidia/NitroGen), opens the shared memory ring buffer, reads frames in a loop, runs inference, and writes actions to the named pipe. Configurable: model path, frame resolution, inference device (cpu/cuda), frame skip. Mock mode for testing without a GPU. Includes a mock_agent mode that outputs random actions for P6-testing.

## Acceptance Criteria

- Script loads NitroGen model successfully\n- Reads frames from shared memory\n- Runs inference on each frame (or frame_skip)\n- Writes actions to named pipe\n- Configurable via command-line arguments\n- Mock mode works without GPU/model\n- Clean shutdown on SIGTERM\n- Error handling: model load failure, frame read timeout

