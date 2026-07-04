---
id: rpg-xlnl
status: closed
deps: [rpg-py13]
links: []
created: 2026-07-04T20:37:58Z
type: task
priority: 0
assignee: KMD
parent: rpg-o9fl
tags: [procgen, grammar, tdd, tokenizer]
---
# procgen-0b: Tokenizer lexer

## Design

Create src/procgen/procgen_tokenize.c/h. Implement procgen_tokenize() that takes a null-terminated string and outputs a validated token stream. State machine: lex @grammar header, skip whitespace+comments, recognize keywords (ROOM_QUAD, CORRIDOR_H/V/DIAG, RAMP_UP/DOWN, DOOR, WINDOW, SPAWN, MARKER, BLOCK, EBLOCK), parse typed parameters (NAME=value for int/float/string/coords), track line/col for error reporting, validate BLOCK/EBLOCK nesting balance. Write RED test: tests/procgen/procgen_tokenize_tests.c with test cases for valid strings, invalid strings, nesting errors, all keyword recognition.

## Acceptance Criteria

- procgen_tokenize() correctly lexes valid grammar string\n- All keyword tokens recognized case-sensitively\n- Integer, float, string, coord-tuple parameters parsed correctly\n- BLOCK/EBLOCK nesting validated (balanced)\n- Comments (#...) ignored\n- Error messages include line:col\n- Tests compile and pass

