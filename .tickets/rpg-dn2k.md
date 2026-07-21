---
id: rpg-dn2k
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
# Bug: remove malloc/free from the per-dispatch GI path

Section 8 #6 / 4.6. gi_probe_gpu.c:692,702,707,723 malloc/free per dispatch -- violates the project no-malloc-per-frame rule. Sizes are bounded by max_lights/max_boxes known at init -> pre-allocate scratch in gi_probe_gpu_t. Also section 4.6: b_active counter is zeroed via glBufferSubData + full barrier on a just-used buffer (gi_probe_gpu.c:858-861) -- use a small dedicated buffer or glClearBufferSubData.

## Acceptance Criteria

No malloc/free in the GI dispatch path; scratch is pre-allocated in gi_probe_gpu_t; b_active zeroing no longer stalls on a just-used buffer.

