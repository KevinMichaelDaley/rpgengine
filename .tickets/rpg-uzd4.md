---
id: rpg-uzd4
status: open
deps: []
links: []
created: 2026-07-04T20:39:25Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, architect, vlm, llm, tdd]
---
# procgen: Phase 4 - Architect VLM

rpg-o9fl

## Design

Build the Architect VLM pipeline that accepts a natural language description of a dungeon level and generates a valid grammar token string via a reprompting loop. The system prompt describes the grammar's token alphabet, constraints, and output format. If the VLM output fails to parse, the error is fed back into the prompt for correction (up to max_retries). Uses existing engine_settings LLM infrastructure (llm_base_url, llm_model, llm_api_key). Tracks token usage and cost via existing llm_cost_tracker.

## Acceptance Criteria

- Architect accepts natural language prompt describing a dungeon\n- System prompt includes full grammar reference and constraints\n- VLM generates valid token string\n- On parse failure: error fed back to VLM, retried up to max_retries\n- Max retries configurable (default 3)\n- Token usage and cost tracked via llm_cost_tracker\n- Budget exceeded → AEGIS_LLM_BUDGET_EXCEEDED returned\n- Timeout → AEGIS_LLM_TIMEOUT returned\n- Integration with existing Aegis VM LLM opcode path\n- Works with local LLMs via Ollama (llm_base_url configured)

