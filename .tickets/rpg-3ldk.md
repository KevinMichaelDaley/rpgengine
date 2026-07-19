---
id: rpg-3ldk
status: closed
deps: [rpg-nbp2, rpg-q1cp]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [net, streaming]
---
# Server-assigned streaming priority protocol

A protocol so the server assigns per-client, per-asset/chunk streaming priority (level chunks nearest the player stream first). Today only per-body speed/tier gating exists (priority_body_sender + phys_game_state tiers); there is no streaming-priority channel to the client.

## Design

Add a net schema for chunk/asset priority hints keyed by the descriptor's asset/chunk ids; server computes priorities from player position/interest (reuse phys_game_state tier logic); client feeds them into the streamer (T3) request priorities. Reliable-stream or piggyback on the entity net pump.

## Acceptance Criteria

Server sends prioritized stream hints on join + as the player moves; client reorders its streaming queue accordingly; nearest chunks demonstrably load first in a walkthrough.


## Notes

**2026-07-19T19:23:24Z**

Completed (protocol + both endpoints' logic). NET_REPL_SCHEMA_STREAM_PRIORITY (0x2011): a batch of (asset/chunk id u64, priority i32) hints; encode/decode in src/net/replication/stream_priority.c. Server side: server_stream_priority_build (src/server/level/, libheadless) ranks chunk ids by distance from the player (-(dist^2*scale), nearest=highest). Client side: net_repl_stream_priority_apply feeds the hints into the fr_asset_stream priority queue (rpg-nbp2). 3/3 tests: wire round-trip + failure modes, server build-by-distance, and end-to-end that applying hints reorders the streamer's admission (top-priority chunk becomes resident under budget). The demo SEND-on-join / APPLY-on-recv wiring lands with rpg-8302 (the client only instantiates its asset streamer there); noted on 8302.
