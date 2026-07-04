---
id: rpg-q3dv
status: closed
deps: [rpg-xlnl]
links: []
created: 2026-07-04T20:37:58Z
type: task
priority: 0
assignee: KMD
parent: rpg-o9fl
tags: [procgen, grammar, tdd, tokenizer]
---
# procgen-0c: Tokenizer validation + error handling

## Design

Extend tokenizer with comprehensive validation: enum for error codes (TOK_ERR_UNEXPECTED_TOKEN, TOK_ERR_UNBALANCED_BLOCK, TOK_ERR_MISSING_PARAM, etc.), bounds checking (token buffer capacity, max string length), validate coordinate tuple completeness, validate parameter name spelling against known set, ensure @grammar header is present and parseable. Write RED tests for all error conditions.

## Acceptance Criteria

- Every error condition produces a unique, descriptive error code\n- Token buffer overflow detected and reported\n- Coordinate tuples validated for completeness\n- Missing @grammar header produces clear error\n- All error path tests pass

