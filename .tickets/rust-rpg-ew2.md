---
id: rust-rpg-ew2
status: open
deps: []
links: []
created: 2026-02-02T15:41:35.040219897-08:00
type: task
priority: 2
---
# NUMA benchmarking on multi-node host

Run performance benchmarking on a host with >=4 NUMA nodes. Goals: compare baseline vs affinity vs NUMA-enabled scheduling across worker counts up to nproc; collect wall/cpu time, utilization, throughput; note hardware topology and kernel version. Deliverables: raw logs, summarized tables, and a brief analysis doc in ref/perf/numa_multi_node.md. Prereqs: disable ASan/instrumentation for perf; ensure auto-detect picks correct node count or override via env.


