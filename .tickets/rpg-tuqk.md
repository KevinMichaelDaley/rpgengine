---
id: rpg-tuqk
status: closed
deps: []
links: []
created: 2026-07-04T23:00:39Z
type: bug
priority: 0
assignee: KMD
tags: [procgen, tokenizer, bug, critical]
---
# procgen-fix: coordinate tuple parsing — syntax parsed but values discarded

## Design

The tokenizer lexes (x,y) and (x,y,z) coordinate tuples but only skips them without emitting tokens. Parameters like from=(20,0), to=(40,0), at=(20,0), polygon=((x1,y1),...) produce NO output tokens. This blocks corridor positioning, detailed room shapes, opening placements, and ramp endpoints.

## Acceptance Criteria

- Coordinate tuples emit proper tokens (x/y/z as floats)\n- from=(20,0) produces x=20,y=0 params\n- polygon=((x1,y1),(x2,y2),...) produces vertex count + coords\n- Nested parens handled correctly\n- Existing tests pass (no regressions)

