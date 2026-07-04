---
id: rpg-5t38
status: closed
deps: [rpg-d8l8]
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-npzr
tags: [procgen, grammar, registry, blockout]
---
# procgen-3b: Refactor blockout grammar into registry

## Design

Refactor grammar_blockout.c to conform to procgen_grammar_t interface. Implement tokenize() and rasterize() function pointers. Define vlm_system_prompt_fragment describing the blockout grammar for VLM consumption. Define known_markers array (entrance, mid_encounter, boss_arena, treasure, exit). Register at init time.

## Acceptance Criteria

- grammar_blockout conforms to procgen_grammar_t interface\n- vlm_system_prompt_fragment is complete and accurate\n- known_markers list is comprehensive\n- Registration succeeds without conflicts

