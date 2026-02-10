---
id: rust-rpg-wpe
status: closed
deps: []
links: []
created: 2026-02-01T22:57:06.458224093-08:00
type: task
priority: 2
---
# Client network runtime: RX reassembly thread

Implement the client-side network RX/reassembly thread.

Requirements:
- Owns the UDP socket recv path (recv/recvfrom).
- Parses protocol frames, updates ACK/reliability state.
- Reorders and reconstructs reliable streams per channel (duplicate suppression, in-order delivery, message reassembly if applicable).
- Before any subsystem reads messages, pumps decoded per-channel messages into topic/channel ring buffers.
- Dispatches or wakes jobs subscribed to those topics (job system safe).

Notes:
- `malloc` is allowed in this networking thread.
- Gameplay subsystems must not parse protocol frames or touch the socket.

Deliverables:
- New client net runtime module with an explicit context object.
- Clean shutdown + join semantics.
- Minimal tests for ordering/duplicate suppression at the stream boundary.


