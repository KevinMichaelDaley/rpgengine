---
id: rpg-llm01
status: closed
deps: []
links: []
created: 2026-04-25T19:15:00Z
type: task
priority: 1
assignee: KMD
parent: 
tags: [aegis, llm, npc, ai, async]
---
# AEGIS LLM Prompt Opcode and Async Client

Implement first-class LLM prompting support in the AEGIS VM for NPC narrative AI. Scripts must be able to submit prompts to an OpenAI-compatible API (Ollama, OpenAI, etc.) via an async opcode that integrates with the existing `wait`/`poll` fiber yielding mechanism.

## Design Reference

See `design/aegis_llm_integration.md` (in parent repo) for full architecture.

## Requirements

### Engine Settings
- Extend `fr_engine_settings_t` with LLM config fields:
  - `llm_base_url[256]` — provider endpoint
  - `llm_api_key[128]` — auth key (empty = no auth)
  - `llm_model[64]` — model name
  - `llm_timeout_ms` — per-prompt timeout
  - `llm_max_tokens` — global cap per prompt
  - `llm_input_cost_per_1k` / `llm_output_cost_per_1k` — cost tracking
  - `llm_budget_usd` — cumulative budget (0 = unlimited)

### Async Infrastructure
- Add `AEGIS_TASK_LLM_PROMPT = 2` to `aegis_async_task` enum.
- Add `AEGIS_OP_LLM_PROMPT = 0x4A` to opcode enum.
- Opcode format: `llm_prompt r_handle, r_prompt_off, r_max_tokens`
- Fuel cost: 500 per invocation.

### LLM Client
- HTTP POST to `/v1/chat/completions` with OpenAI-compatible JSON.
- Parse response: content, usage tokens, tool_calls.
- Support timeout and connection errors.
- No external deps beyond POSIX sockets / libcurl (discuss with team).

### Cost Tracking
- Thread-safe atomic `llm_total_cost_usd`.
- Check budget before each prompt; return `BUDGET_EXCEEDED` if over.
- Per-result metadata: input_tokens, output_tokens, cost_usd, total_cost_usd.

### Result Format
- Variable-length `aegis_llm_result_t` written to script heap:
  - status, input_tokens, output_tokens, cost_usd, total_cost_usd
  - response_len, response[]
  - tool_call_count + tool_call structs

### Tool Calling (Base Feature)
- Parse OpenAI `tool_calls` array from response.
- Return as `aegis_llm_tool_call_t` structs (id, name, JSON args).
- AEGIS scripts iterate and dispatch via existing `SIGNAL` opcode.

### Multi-Turn Conversation (Base Feature)
- Optional `r_conv_id` register on `llm_prompt` opcode.
- Executor maintains per-conversation message history.
- Conversation buffer stored in script static array.

## Files to Create

### Headers (≤2 types each)
- `include/ferrum/aegis/aegis_llm.h` — result + tool call types
- `include/ferrum/aegis/aegis_ops_llm.h` — opcode handler declaration
- `include/ferrum/llm/llm_client.h` — client config + state
- `include/ferrum/llm/llm_executor.h` — executor lifecycle
- `include/ferrum/llm/llm_cost_tracker.h` — cost tracker type
- `include/ferrum/llm/llm_conversation.h` — conversation buffer type

### Source (≤4 non-static functions per file)
- `src/aegis/ops/aegis_ops_llm.c`
- `src/llm/client/llm_client_init.c`
- `src/llm/client/llm_client_send.c`
- `src/llm/client/llm_client_parse.c`
- `src/llm/executor/llm_executor_init.c`
- `src/llm/executor/llm_executor_drain.c`
- `src/llm/executor/llm_executor_task.c`
- `src/llm/cost/llm_cost_tracker.c`
- `src/llm/cost/llm_cost_compute.c`
- `src/llm/conversation/llm_conv_init.c`
- `src/llm/conversation/llm_conv_append.c`
- `src/llm/conversation/llm_conv_build.c`

### Tests
- `tests/aegis/aegis_llm_prompt_tests.c`
- `tests/llm/llm_client_parse_tests.c`
- `tests/llm/llm_cost_tracker_tests.c`
- `tests/llm/llm_integration_tests.c` (with mock server)

## Integration Points
1. `include/ferrum/engine_settings.h` — add LLM fields
2. `include/ferrum/aegis/aegis_types.h` — add opcode
3. `include/ferrum/aegis/aegis_async.h` — add task type
4. `src/aegis/aegis_vm_run.c` — add case to switch
5. `src/server/tick/tick_loop.c` — call llm_executor_drain

## TDD Requirements
- Write all tests before implementation.
- Cover: happy path, timeout, budget exceeded, malformed response, tool call parsing, conversation append/build.
- Tests must compile with `libheadless.a` (no GL dependency).
