---
id: rpg-8ijf
status: closed
deps: []
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-uzd4
tags: [procgen, architect, vlm, prompt]
---
# procgen-4a: System prompt builder

## Design

Implement architect_build_system_prompt() in architect/architect_prompt.c. Given a grammar (procgen_grammar_t*) and user request string, construct the full system prompt: role description, full token reference (BNF-style), constraint list (exactly one SPAWN, ≥3 MARKERs with distinct names, no overlapping rooms, positive clearance), output format instructions (tokens only, no markdown, no explanation). Write RED test verifying prompt structure and content.

## Acceptance Criteria

- Prompt includes grammar name and version\n- Prompt includes full token alphabet with descriptions\n- Prompt includes ALL constraints\n- Prompt explicitly forbids markdown/code blocks\n- Prompt includes user request\n- Prompt length < max context window\n- System prompt renders correctly for blockout grammar

