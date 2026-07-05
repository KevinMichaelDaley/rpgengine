---
id: rpg-n8ay
status: closed
deps: [rpg-8sc6]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-oxnh
tags: [procgen, critic, runtime, config]
---
# procgen-7e: Critic configuration + CLI

## Design

Define critic_config_t struct: dungeon_layout_path, max_playthroughs, playthrough_timeout_s, nitroGen_model_path, nitroGen_device (cpu/cuda), frame_resolution, frame_skip, output_report_path, verbose flag. Implement critic CLI that loads config, runs critic, and writes report. Integrate with existing engine_settings for LLM-dependent critic features.

## Acceptance Criteria

- critic_config_t fully defined\n- CLI accepts all configurable parameters\n- Default values are reasonable\n- Config validation before runtime\n- Report written to specified path\n- Verbose mode shows per-playthrough details

