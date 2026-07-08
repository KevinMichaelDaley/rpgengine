---
id: rpg-i1sv
status: closed
deps: [rpg-cly7]
links: []
created: 2026-07-05T22:54:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-9pjm
tags: [srd, rules]
---
# srd-rules-01: srd_descent_rules.h and srd_descent_rules.c — table API

Public header and table management implementation: srd_rule_table_create, srd_rule_table_register, srd_rule_table_register_builtins, srd_rule_sample_selection, srd_rule_find_applicable. The register function asserts in debug builds that inverse_rule_id (if >= 0) already refers to a registered rule.

## Design

srd_rule_table_t: internal array of srd_descent_rule_t with capacity grown by doubling (arena-backed, no malloc in hot path). srd_rule_sample_selection: uniform random selection of n_select boxes from layout satisfying rule's cond predicate, using caller-supplied rng_state (xorshift32). srd_rule_find_applicable: linear scan, exclude is_repair rules.

## Acceptance Criteria

Table grows correctly up to at least 256 entries; register returns correct index; find_applicable excludes repair rules; sample_selection never returns invalid selection (tries up to 32 times before returning false); tests cover empty table, full table, repair exclusion

