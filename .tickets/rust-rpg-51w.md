---
id: rust-rpg-51w
status: in_progress
deps: []
links: []
created: 2026-02-01T08:27:20.694690422-08:00
type: task
priority: 2
---
# p007 integration test: reliable+unreliable trajectory

Two-part integration test (separate server/client binaries on different hosts) exercising reliable + unreliable channels by sending quantized position/velocity updates for a simulated trajectory with intermittent random acceleration. Depends on core UDP comms module (rust-rpg-dhr).

## Notes

Server: bind/listen, echo or forward updates; Client: simulate truth, send quantized state over unreliable UDP, receive back, reconstruct, compare error thresholds.


