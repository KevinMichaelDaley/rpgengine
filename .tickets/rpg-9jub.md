---
id: rpg-9jub
status: closed
deps: []
links: []
created: 2026-07-04T20:36:26Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, grammar, tdd]
---
# procgen: Phase 0 - Types + Tokenizer

Define the core types for the procedural dungeon grammar system: token enum, procgen_token_t union, fr_dungeon_layout_t and all sub-types (fr_room_def_t, fr_corridor_def_t, fr_opening_def_t, fr_ramp_def_t, fr_marker_def_t, fr_nav_node_t, fr_nav_edge_t). Implement the generic tokenizer/lexer that parses the token string format into a validated token stream. The tokenizer handles the outer syntax: @grammar header, BLOCK/EBLOCK nesting, comments (#...), keyword tokens, and typed parameters (int, float, string, coordinate tuples).

## Design

Define all core token types, layout structs, and the generic tokenizer/lexer. This is the foundation: no grammar-specific code, just the shared types and parsing infrastructure. Tokenizer must handle the outer syntax (BLOCK/EBLOCK, comments, @grammar header) and emit a flat token stream with line/col error reporting.

## Acceptance Criteria

Tokenizer parses valid grammar strings; Tokenizer rejects invalid strings with error messages; All token types defined and documented; Layout types (fr_dungeon_layout_t etc.) fully defined


## Notes

**2026-07-04T20:38:07Z**

Superseded by rpg-o9fl (created with full subtask structure)
