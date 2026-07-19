---
id: rpg-3ldk
status: open
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

