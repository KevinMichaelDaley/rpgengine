---
id: rust-rpg-6he
status: open
deps: [rust-rpg-c2z]
links: []
created: 2026-01-17T18:04:55.372520954-08:00
type: epic
priority: 2
---
# P_017 — ECS Serialization, Save-Load & Replay

## P_017 — ECS Serialization, Save-Load & Replay

### Design Intent
Enable deterministic save/load of ECS state and input/replay functionality for debugging and regression testing.

### Specification
- Snapshot serialization of ECS components with versioned schemas.
- Input log recording; deterministic playback.

### Implementation Steps
1. Schema registry for save/load (reuse networking schemas where applicable).
2. Snapshot writer/reader; diff tools.
3. Input recorder; replay runner.

### Architectural Considerations
- Versioning; backward-compat.
- No hidden global state.

### Unit/Regression/Cumulative Tests
- Save/load roundtrip; diff correctness.
- Replay reproduces deterministic positions/events across runs.

---



