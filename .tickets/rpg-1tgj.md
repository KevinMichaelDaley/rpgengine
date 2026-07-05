---
id: rpg-1tgj
status: closed
deps: [rpg-oxnh]
links: []
created: 2026-07-04T20:41:07Z
type: task
priority: 0
assignee: KMD
parent: rpg-aqm2
tags: [procgen, critic, vlm, python]
---
# procgen-8c: Python VLM bridge for visual critique

## Design

Create tools/critic_visual_vlm.py: a Python script that accepts screenshot data (raw RGB bytes via pipe or temp file) + text prompt, loads a small VLM (Qwen2.5-VL-3B via transformers or llama.cpp), runs inference, and returns: coherence_score (float) and issues (list of strings). Configurable: model name, device, max_tokens. Mock mode returns dummy scores for testing without a model. Use the existing LLM infrastructure (engine_settings) for API-based VLMs.

## Acceptance Criteria

- Script accepts screenshot + prompt\n- Loads small VLM model\n- Returns coherence score (float)\n- Returns issue list\n- Mock mode works without model\n- Configurable model and device\n- JSON output format for easy parsing\n- Clean shutdown handling

