---
id: rpg-00d1
status: closed
deps: []
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Internal JSON parser (json_parse.c)

Implement a minimal internal JSON parser since cJSON is not in the codebase and external dependencies are forbidden (only C stdlib, POSIX, OpenGL, SDL2 allowed).

READ FIRST: ref/editor_design.md §2.4 for wire format, ref/editor_spec.md §5 for protocol.

Requirements:
- Parse JSON objects, arrays, strings, numbers, booleans, null
- json_value_t union type (see design §2.4 for the struct definition)
- json_parse(const char *input, size_t len, json_value_t *out) → bool
- json_write(const json_value_t *val, char *buf, size_t cap) → size_t
- Zero dynamic allocation: parse into caller-provided arena or fixed-capacity pools
- No VLAs (project rule)
- Must handle the edit protocol message format (nested objects with arrays of numbers)
- Must be clean under -Wall -Wextra -Wpedantic

Files to create:
- include/ferrum/editor/json_parse.h (public API, ≤2 types)
- src/editor/protocol/json_parse.c (parser implementation)
- src/editor/protocol/json_write.c (serializer)
- tests/editor/json_parse_tests.c

TDD: write tests first covering happy path, malformed input, nested objects, arrays of numbers, string escapes, unicode, buffer overflow edge cases.

