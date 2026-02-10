---
id: rust-rpg-coq
status: closed
deps: []
links: []
created: 2026-02-01T22:57:07.635987877-08:00
type: feature
priority: 2
---
# Refactor RUDP layering: retransmit+reassembly above protocol frames

Refactor the networking stack layering so retransmission and message reconstruction occur above protocol framing.

Problem:
- Current RUDP peer mixes protocol frame concerns with how subsystems consume messages.

Requirements:
- Keep a minimal wire-level framing layer (protocol id, schema/topic id, ack header).
- Implement a distinct reliability/reassembly layer that outputs an abstract per-channel stream of decoded messages.
- Ensure subsystems consume only stream/channel messages, not protocol frames.

Deliverables:
- New module boundaries and includes reflected in aggregator headers.
- Migration plan: adapt existing p007/p008 tests and clients/servers.
- Tests for ack/retransmit behavior at the reliability layer boundary.



## Notes

**2026-02-05T04:11:41Z**

Starting work: audit current RUDP peer vs stream usage, add boundary tests, then split wire framing from reliability/reassembly and migrate p007/p008 call sites.

**2026-02-05T06:28:53Z**

Moved server runtime JOIN nonce extraction off wire-frame decode: runtime_pump now uses net_rudp_peer_receive() to treat JOIN as an inbound message. make test passes.

**2026-02-05T06:44:27Z**

Split reliability/reassembly receive path from wire decode: added net_rudp_reliability_receive() (decoded header+frame in) and refactored net_rudp_peer_receive() into wire-decode wrapper. Added p013 reliability-layer boundary tests (dup window, ack retirement, fragmentation reassembly) and wired them into make test/test_timeout. Commit: 514d261.

**2026-02-05T07:06:00Z**

Split send-side reliability above wire framing: added net_rudp_reliability_send_*() + net_rudp_reliability_tick_resend_via(); peer send wrappers now delegate; added p014 boundary tests (seq advance rules, ack header state, resend bytes stable). Commit: b3ade61.

**2026-02-05T07:30:38Z**

Standardized server runtime inbound-topic payload bytes (client_id/schema_id/reliable+payload) via fr_server_net_inbound_message_{encode,decode}(). Refactored runtime_client_fiber publish + entity net pump to use it; added p015 tests. Commit: f3ffd16.
