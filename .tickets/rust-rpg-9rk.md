---
id: rust-rpg-9rk
status: closed
deps: [rust-rpg-1eu]
links: []
created: 2026-01-17T18:04:55.670731689-08:00
type: epic
priority: 2
---
# P_019 — UI, Text & Localization (Immediate Mode)

## P_019 — UI, Text & Localization (Immediate Mode)

### Design Intent
Immediate-mode UI with font atlas, text shaping support (basic), and localization scaffolding.

### Specification
- Font atlas; glyph metrics; basic shaping.
- Localization tables; RTL flagging (basic).

### Implementation Steps
1. Font atlas builder; text rendering in forward pass.
2. Localization loader; string lookup.
3. Input navigation integration.

### Architectural Considerations
- Deterministic layout; avoid retained-state bugs.

### Unit/Regression/Cumulative Tests
- Glyph placement; atlas lookups; localized strings resolved.
- Cumulative: dialogue UI integrates with networking and quests.

---




## Notes

**2026-03-13T04:08:43Z**

Closed as exact duplicate (double import). Keeping the copy with downstream blockers wired.
