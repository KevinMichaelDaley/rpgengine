---
id: rpg-o9fl
status: closed
deps: []
links: []
created: 2026-07-04T20:37:58Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, grammar, tdd]
---
# procgen: Phase 0 - Types + Tokenizer

## Design

Foundational phase. Define all core token types (tok_type_t enum, procgen_token_t union), the dungeon layout struct (fr_dungeon_layout_t with room/corridor/opening/ramp/marker/nav sub-types), and the generic tokenizer/lexer that parses the string format into a validated token stream. Tokenizer handles outer syntax: @grammar header, BLOCK/EBLOCK nesting, comments (#...), keywords, and typed parameters (int, float, string, coordinate tuples). Line/col error reporting. No grammar-specific code yet — just shared infrastructure.

## Acceptance Criteria

- Tokenizer correctly parses valid @grammar v1 strings into flat token streams\n- Tokenizer rejects malformed strings with descriptive line:col error messages\n- All token types (ROOM_QUAD, CORRIDOR_H, RAMP_UP, etc.) defined in enum\n- procgen_token_t supports int/float/string values\n- fr_dungeon_layout_t and all geometry sub-types fully defined\n- All RED-phase tests compile and fail before implementation\n- Build integration: src/procgen/* compiles into libheadless.a\n- Zero warnings at -Wall -Wextra

