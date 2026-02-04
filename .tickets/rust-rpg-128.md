---
id: rust-rpg-128
status: closed
deps: [rust-rpg-ml2]
links: []
created: 2026-02-01T15:03:39.506987911-08:00
type: task
priority: 2
---
# P_008: Renderer client for cube replication

Implement a renderer-backed client that visualizes the multi-client cube replication scenario.

Scope
- Client controls its own 'player cube': each frame interpolate toward an intermittently-randomized rotation (deterministic RNG seedable).
- Uses reliable JOIN/SPAWN to establish its cube on the server.
- Receives unreliable STATE updates for all other clients' cubes and renders them.
- Interpolate remote cubes between received snapshots.

Deliverables
- Client executable that links against existing renderer modules.
- Optional headless flag is OK if it does not add extra UX.

Acceptance
- Visual: own cube responds smoothly; remote cubes appear and update.
- Robust under packet loss; no crashes.



