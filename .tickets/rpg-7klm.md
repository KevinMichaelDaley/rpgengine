---
id: rpg-7klm
status: open
deps: []
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-9wui
tags: [aegis, vm, validation]
---
# Aegis validation rule JSON parser

Parse validation rules from JSON per ref/aegis_bytecode_spec.md §7.1, §7.2.

Implement:
- aegis_rule_t: single validation rule (name, condition key, check type, params, action)
- aegis_rule_set_t: collection of rules + flagged attribute set
- aegis_rules_load(rules, json, json_len): parse JSON rule definitions
- Rule actions: ALLOW, REJECT, REJECT_AND_FLAG, CLAMP, LOG
- Check types: distance_check, range_check, rate_limit (extensible enum)
- Flagged attributes: set of SCRIPT_KEY_* values that trigger rule evaluation

Uses the engine's existing JSON parser (json_value_t / json_arena_t).

Files:
- include/ferrum/aegis/aegis_rules.h
- src/aegis/aegis_rules_parse.c
- tests/aegis/aegis_rules_parse_tests.c

Acceptance criteria:
- [ ] Parses valid JSON rule definitions correctly
- [ ] Extracts rule name, condition key, check type, params, action
- [ ] Handles multiple rules in array
- [ ] Rejects malformed JSON gracefully (returns error, no crash)
- [ ] Flagged attributes parsed from JSON
- [ ] Tests: parse single rule, multiple rules, all action types, all check types, malformed JSON, empty rules

## Acceptance Criteria

JSON rule parsing produces correct aegis_rule_set_t, handles malformed input gracefully

