---
id: rpg-npc03
status: open
deps: [rpg-npc01]
links: [rpg-npc01]
created: 2026-05-03T20:00:00Z
type: bug
priority: 3
assignee: KMD
parent: rpg-npc01
tags: [npc, bug, tokenizer, prompt, estimate]
---
# Crude Token Estimate len/4 in State Manager

Both `npc_state_compact.c` and `npc_state_prompt.c` use a crude token count estimator:
```c
return (len / 4) + 1;
```
Real LLM tokenizers (BPE, WordPiece) produce dramatically different counts. For English text this may be off by 20-30%; for code or non-English it could be off by 50% or more.

Impact: LLM API calls may be silently rejected for exceeding the context window, or tokens are wasted by sending less context than the budget allows.

## Fix
Integrate the existing `llm_cost_tracker` (from rpg-llm01) which provides `llm_token_count()` using the same tokenizer as the LLM provider. If no provider is available, the `/4` heuristic is an acceptable fallback but should be documented as approximate.

## Acceptance
- [ ] Token estimate uses llm_cost_tracker when available
- [ ] Falls back to len/4 when no tracker
- [ ] Compact and prompt functions use the same estimator
