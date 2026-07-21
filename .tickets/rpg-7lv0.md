---
id: rpg-7lv0
status: open
deps: []
links: []
created: 2026-07-21T01:38:36Z
type: bug
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, gi, bug]
---
# Bug: GI teardown leaks b_norm buffer

Section 8 #2. gi_probe_gpu.c:885-887 deletes 8 buffers but b_norm is missing from the array -- leaked on teardown. Add b_norm to the glDeleteBuffers list.

## Acceptance Criteria

GI teardown deletes b_norm; no GL buffer leak on gi_probe_gpu destroy.

