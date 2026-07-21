---
id: rpg-2p4m
status: open
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# forward_plus: stop re-speccing cluster TBOs per frame; pack offset+count

Section 1.5. forward_plus.c:45-69 fully re-specifies four TBOs per frame with glBufferData(GL_STATIC_DRAW) -- the worst usage hint for a stream. Fix: allocate once at init at capacity with GL_STREAM_DRAW, then orphan + glBufferSubData the used prefix; skip the upload when neither camera nor lights changed. Also u_cluster_offset/u_cluster_count are two R32I buffer textures (:64-68) where one RG32I halves the per-pixel fetch count (pbr_shader.c:92 already suggests RG32I).

## Acceptance Criteria

Cluster TBOs are allocated once with a stream hint and updated via orphan+subdata; offset+count share one RG32I buffer; the upload is skipped on unchanged frames.

