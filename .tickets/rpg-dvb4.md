---
id: rpg-dvb4
status: open
deps: [rpg-uzd4]
links: []
created: 2026-07-04T23:19:26Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, architect, review, dataset, integration]
---
# procgen-review: end-to-end architect demo + dataset generation

## Design

Thoroughly test the architect VLM by generating level token strings from natural language prompts, converting them to JSON, loading into demo_server/demo_client for visual inspection, and producing a small dataset of connected subgraphs (2-5 rooms with corridors) for future critic finetuning. Uses the OpenRouter API with cheap models (gemini-2.0-flash for low cost).

## Acceptance Criteria

- architect CLI builds and runs successfully\n- Generate 10+ valid level token strings from natural language\n- Each token string passes tokenizer validation\n- Each token string rasterizes to a valid layout\n- Each layout serializes to valid JSON\n- Load generated JSON into demo_server/demo_client and visually verify\n- Dataset directory contains .txt (token strings) and .json (levels)\n- All levels are connected subgraphs with 2-5 rooms\n- Document any failure patterns or model quirks\n- Cost tracking verifies </bin/bash.01 per level for cheap models

