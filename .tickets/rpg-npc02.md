---
id: rpg-npc02
status: closed
deps: [rpg-npc01]
links: [rpg-npc01]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent: rpg-npc01
tags: [npc, bug, prompt, context, truncation]
---
# Token Budget Truncation Destroys System Prompt

`npc_state_prompt_assemble` in `src/npc/state/npc_state_prompt.c:89-96` handles budget overflow by discarding the FRONT of the assembled prompt — which is the system prompt, statblock, and status line — and moving the context suffix to the start.

The resulting prompt sent to the LLM has no NPC personality, capabilities, or current state — just a truncated chat history prefix.

## Root Cause
```c
if (off > npc->context_max_tokens * 4) {
    uint32_t start = off - (uint32_t)(npc->context_max_tokens * 4);
    memmove(prompt, prompt + start, keep_chars + 1);
```
The `memmove` shifts everything after `start` bytes to position 0, discarding the crucial prefix.

## Fix
Keep the system prompt + statblock + status line intact. Truncate the MIDDLE by summarizing or dropping oldest context entries. The system prefix must survive all budget constraints.

## Acceptance
- [ ] System prompt survives when context exceeds token budget
- [ ] Statblock and status line are always present in the assembled prompt
- [ ] Only context history is truncated when over budget
