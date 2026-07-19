---
id: rpg-n3cc
status: closed
deps: []
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [job, infra]
---
# Honor job-system priority on the streaming path

job_dispatch takes an int priority but the normal work-stealing path (src/job/queue.c job_system_enqueue_preferred) ignores it ((void)priority); only the deterministic scheduler honors it. Streaming-by-priority (the whole point) needs priority to actually affect scheduling order.

## Design

Add a priority-aware submission path usable by the asset streamer without regressing the hot work-stealing path: e.g. a small number of priority buckets / a priority steal-order, or a dedicated streaming queue drained highest-first. Keep per-frame physics/render jobs on the existing fast path. Measure no throughput regression on perf_job.

## Acceptance Criteria

A higher-priority streaming job is observably scheduled before a lower one under contention (deterministic test); perf_job shows no regression on the existing dispatch path.


## Notes

**2026-07-19T07:47:50Z**

Obviated: job-scheduler priority is NOT what streaming needs. Priority is the asset streamer's own admission/eviction policy -- a priority-ordered request queue drained within RAM/VRAM residency budgets (server suggests order; client admits top-priority within budget, evicts lowest under pressure). With a bounded number of in-flight loads refilled from the queue head, effective load order == priority order regardless of the work-stealing pool's internal scheduling. Folded into rpg-nbp2.
