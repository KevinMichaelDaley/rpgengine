---
id: rpg-p1ma
status: closed
deps: []
links: []
created: 2026-07-04T23:00:39Z
type: bug
priority: 1
assignee: KMD
tags: [procgen, grammar, bug]
---
# procgen-fix: suppressed error handling in rasterizers

## Design

rasterize_corridor, rasterize_opening, rasterize_ramp, and rasterize_marker use (void)err_buf; (void)err_cap; and never write error messages. If allocation fails or validation fails, the user gets no diagnostic.

## Acceptance Criteria

- All rasterize_* functions write errors to err_buf\n- Missing required params produce error messages\n- Allocation failures produce error messages\n- Existing tests pass

