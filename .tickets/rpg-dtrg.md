---
id: rpg-dtrg
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
# procgen-3c: Runtime grammar selection via @grammar header

## Design

Extend tokenizer/rasterizer pipeline to use @grammar header for runtime grammar selection. Parse 'name vN' from header, lookup grammar in registry, call its tokenize() and rasterize(). If grammar not found, return clear error with available grammar names listed. Write RED test for grammar-switching behavior.

## Acceptance Criteria

- @grammar header parsed for grammar name and version\n- Grammar selected from registry by name\n- Error produced if grammar not found\n- Error message lists available grammars\n- Version mismatch warning produced

