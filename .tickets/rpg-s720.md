---
id: rpg-s720
status: open
deps: [rpg-oda7]
links: []
created: 2026-07-19T21:27:19Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [gi, streaming, zones]
---
# Zone-level residency (zones over chunks)

Two-level streaming for large open worlds: zones group chunks; a zone is admitted/evicted by proximity + coarse visibility before its chunks page in. Add a zone table above the chunk tables (parent priority bucket or separate table). Zone admission bundles its chunk manifest + boxes.

## Design

See ref/gi_streaming_design.md 'Two-level residency: zones -> chunks' + build-order step 4.

## Acceptance Criteria

Chunks only stream for admitted zones; distant zones fully evicted (RAM/VRAM bounded); great_hall = one zone.

