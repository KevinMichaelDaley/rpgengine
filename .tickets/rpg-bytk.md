---
id: rpg-bytk
status: in_progress
deps: [rpg-btcr]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 0
assignee: KMD
parent: rpg-mvn6
tags: [procgen]
---
# srd-014: Architect prompt rewrite -- emit ASCII grids

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Rewrite architect system prompt to emit ASCII floor plans. Update reprompting loop for ASCII validation. Remove old token prompt. RED-phase: send simple prompt, verify parseable output.

## Acceptance Criteria

Prompt returns valid ASCII grid; reprompt catches malformed output; tokens fully removed

