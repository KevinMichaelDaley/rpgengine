---
id: rpg-vgqt
status: open
deps: []
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-uzd4
tags: [procgen, architect, vlm, llm]
---
# procgen-4b: VLM call integration

## Design

Implement architect_call_vlm() that sends the system+user prompt to the configured LLM via the existing infrastructure. Read llm_base_url, llm_model, llm_api_key from engine_settings. Use the same HTTP client path as aegis_ops_llm.c. Handle response: extract text content, handle tool calls (unused but parsed), track tokens and cost. Write RED test with mock HTTP endpoint.

## Acceptance Criteria

- VLM called with correct system+user prompt\n- llm_model from engine_settings used\n- llm_timeout_ms enforced\n- Response text extracted correctly\n- Token counts returned\n- Cost computed via llm_cost_compute\n- Error on HTTP failure\n- Budget check passes before sending

