---
id: rpg-c7fk
status: open
deps: [rpg-oda7]
links: []
created: 2026-07-19T21:27:19Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [gi, streaming]
---
# gi_runtime accepts external SDF residency (retire self-load)

gi_runtime currently owns gi_sdf_stream and loads+RAM-caches every SDF chunk at init. Refactor so gi_runtime consumes an EXTERNALLY-managed resident SDF chunk set (owned by the light-data streamer) instead of self-loading, so SDF obeys the streamer's RAM/VRAM budget + visibility gating.

## Design

See ref/gi_streaming_design.md 'Renderer consumption' + build-order step 5.

## Acceptance Criteria

gi_runtime binds only streamer-resident SDF chunks; no all-chunks-in-RAM cache; GI unchanged visually for great_hall.

