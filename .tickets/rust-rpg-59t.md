---
id: rust-rpg-59t
status: closed
deps: []
links: []
created: 2026-02-01T21:41:53.466232311-08:00
type: task
priority: 2
---
# P_007: RUDP peer supports caller-sized send slots

Remove the fixed 64-slot reliable send cap in RUDP by supporting caller-provided send-slot storage per peer. Needed for P_008 multi-client spawn bursts (e.g., 100 clients / 100 spawns) to avoid dropping reliable SPAWN messages.


