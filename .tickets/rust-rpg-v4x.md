---
id: rust-rpg-v4x
status: closed
deps: [rust-rpg-71n]
links: []
created: 2026-01-17T18:04:55.817360269-08:00
type: epic
priority: 2
---
# P_020 — Online Services (Matchmaking, Auth, Security)

## P_020 — Online Services (Matchmaking, Auth, Security)

### Design Intent
Self-hosted dedicated server model with minimal authentication and simple matchmaking; no client-side anti-cheat.

### Specification
- Auth: server-issued session tokens; straightforward handshake.
- Matchmaking: lobby directory or direct server join; player presence lists.
- Security: server-side validation and rate limiting only; no client-side anti-cheat.

### Implementation Steps
1. Simple auth handshake; issue/validate session tokens on the server.
2. Lobby directory or direct-connect endpoints; manage player presence.
3. Server-side rate limiting counters and basic validation.

### Architectural Considerations
- Dedicated server is authoritative; clients never trust unvalidated messages.
- Deterministic test harness with injected clocks; focus on simplicity and operability.

### Unit/Regression/Cumulative Tests
- Auth roundtrip; lobby join/leave; rate limit enforcement (server-side only).
- Cumulative: integrate with P_007 replication and interest management; verify dedicated server flows.

---

## End of Document




## Notes

**2026-03-13T04:08:43Z**

Closed as exact duplicate (double import). Keeping the copy with downstream blockers wired.
