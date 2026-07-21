---
id: rpg-oenp
status: open
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, gi]
---
# GI: make gi_n_probe_groups gate pass 0 (classify/trace)

Section 4.1 -- sharpest finding of the review. gi_probe_gpu.c:485-488: main() calls pass_classify(gid) for EVERY active probe when u_pass==0; only pass_gather (:362) checks in_group(gid). So the staggering knob amortizes the cheap gather while the depth/inject trace (>90% of GI compute, up to ~9,200 scene_sdf evals/probe) runs monolithically -- a guaranteed multi-ms spike every 8th frame on Iris/Xe.
Fix: gate compute_depth/normal/ray-inject by in_group too; persist per-probe classification (e.g. surf_min in pnrm[gid].w) so out-of-group probes re-append to alist from stored data instead of re-tracing. Also section 4.4: split pass 0 / pass 1 across consecutive frames (the barrier already permits it).

## Acceptance Criteria

gi_n_probe_groups divides the classify/trace cost, not just the gather; the per-tick GI spike drops by ~(n_probe_groups)x with no steady-state quality change.

