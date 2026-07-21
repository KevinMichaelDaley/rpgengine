---
id: rpg-rbon
status: open
deps: [rpg-oda7, rpg-s720]
links: []
created: 2026-07-19T21:27:19Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [gi, streaming, net]
---
# Wire STREAM_PRIORITY + player-distance interest into the light-data streamer

Feed server-assigned priorities (net_repl_stream_priority_apply, rpg-3ldk) + player-distance chunk interest (fr_chunk_table_set_interest) + visibility into the light-data streamer's chunk/zone priorities, so residency order == server-suggested + on-screen order.

## Design

See ref/gi_streaming_design.md 'fr_asset_stream integration' + build-order step 6. server_stream_priority_build already exists.

## Acceptance Criteria

Server priority hints + player movement reorder chunk/zone admission; visible+nearby+server-prioritized chunks become resident first.

