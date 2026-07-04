---
id: rpg-zx6t
status: closed
deps: [rpg-tljj]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-uzd4
tags: [procgen, architect, vlm, config]
---
# procgen-4d: Architect configuration + run function

## Design

Define architect_config_t struct (model override, grammar name, max_retries, retry_timeout_ms, user_prompt). Implement procgen_architect_run() that orchestrates the full pipeline: grammar lookup → prompt build → VLM call → reprompt loop → return fr_dungeon_layout_t. Output: token_string, success flag, error_message, attempt_count, token/cost stats.

## Acceptance Criteria

- architect_config_t fully defined\n- procgen_architect_run() orchestrates full pipeline\n- Success path returns layout with correct token string\n- Failure path returns error message\n- Stats (attempts, tokens, cost) populated\n- Works with any registered grammar

