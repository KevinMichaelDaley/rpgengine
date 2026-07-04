---
id: rpg-npzr
status: open
deps: []
links: []
created: 2026-07-04T20:39:25Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, grammar, registry, tdd]
---
# procgen: Phase 3 - Grammar Registry

rpg-bdrv

## Design

Build the multi-grammar registry system. procgen_grammar_t struct defines each grammar's interface (tokenize, rasterize, vlm_system_prompt_fragment, known_markers). The registry supports register/lookup by name. The blockout grammar is refactored to use the registry pattern. Runtime grammar selection via @grammar header in the token string. This enables future grammars (castle, cave, city) to plug in without changing the pipeline.

## Acceptance Criteria

- procgen_grammar_registry_init() initializes empty registry\n- procgen_grammar_register() adds grammars\n- procgen_grammar_find() looks up by name\n- Blockout grammar registered as 'blockout'\n- @grammar header used for runtime grammar selection\n- Duplicate registration rejected\n- Unknown grammar lookup returns NULL with error\n- Multiple grammars can coexist\n- Grammar struct includes version for compatibility checks

