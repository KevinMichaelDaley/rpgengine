---
id: rpg-ma6m
status: open
deps: [rpg-o9fl, rpg-cpqa]
links: []
created: 2026-07-04T20:37:59Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout]
---
# procgen-1g: BLOCK/EBLOCK nesting support

## Design

Add BLOCK/EBLOCK nesting support to the rasterizer. BLOCK opens a new scope (e.g., a multi-floor section or sub-dungeon). EBLOCK closes it. Rasterizer maintains a stack of scopes. Parameters set in outer scope apply as defaults to inner scope (e.g., corridor width, ceiling height).

## Acceptance Criteria

- BLOCK/EBLOCK nesting depth tracked\n- Scope stack maintained correctly\n- Default parameter inheritance from outer to inner block\n- Unbalanced BLOCK/EBLOCK rejected

