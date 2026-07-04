---
id: rpg-d8l8
status: open
deps: []
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-npzr
tags: [procgen, grammar, registry]
---
# procgen-3a: Grammar registry struct + register/lookup

## Design

Define procgen_grammar_t struct in procgen_grammar.h: name, version, fn pointers for tokenize/rasterize, vlm_system_prompt_fragment, known_markers array. Implement registry hash table in procgen_grammar_registry.c: init, register (insert by name, reject duplicates), find (lookup by name, return NULL if not found). Write RED tests.

## Acceptance Criteria

- procgen_grammar_t fully defined\n- Registry supports at least 16 grammars\n- Register rejects duplicates\n- Find returns correct grammar by name\n- Find returns NULL for unknown name\n- Thread-safe for read after registration

