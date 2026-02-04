---
id: rust-rpg-4d1
status: closed
deps: []
links: []
created: 2026-02-02T23:51:45.136837581-08:00
type: task
priority: 2
---
# Reliable UDP buffers: fragmentation/reassembly opaque

Goal: reliable UDP layer handles fragmentation, reassembly, and retransmission transparently so jobs/fibers only push/pop per-connection buffers.

Acceptance:
- Runtime/client fibers do not build protocol packets or manage resend slots directly; they call RUDP APIs and only touch per-client topics/buffers.
- RUDP supports payloads > single-packet max via fragmentation + reassembly (enabled by default), bounded by per-peer reassembly buffer.
- Retransmission remains internal to RUDP.
- Unit/regression coverage exists for fragmented send+reassembly (in-order and out-of-order).

Notes:
- Default reassembly buffer per peer should be caller-independent and safe for fiber stacks (prefer internal storage or explicit buffers per peer).
- Keep public API minimal; callback-based send is acceptable for runtime abstraction.


