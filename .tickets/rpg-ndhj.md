---
id: rpg-ndhj
status: closed
deps: [rpg-a3dm, rpg-btcr]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 0
assignee: KMD
parent: rpg-nlev
tags: [procgen]
---
# srd-009: procgen_srd_grammar.c -- context-sensitive rewrite proposals

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Context checks and proposal generation for all 10+ rewrite rule types. RED-phase: tests/procgen/srd/srd_grammar_tests.c

## Acceptance Criteria

Correct proposals for all rule types; context checks reject invalid; 2+ variants per rule

