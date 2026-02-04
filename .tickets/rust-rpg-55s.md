---
id: rust-rpg-55s
status: open
deps: [rust-rpg-32w]
links: []
created: 2026-01-17T18:04:54.482795364-08:00
type: epic
priority: 2
---
# P_011 — Flow Field AI

## P_011 — Flow Field AI

### Design Intent
Scale navigation to hundreds of agents by computing a single integration+flow field per target and having agents read local vectors.

### Specification
- Cost field: `uint8_t` grid (1..254 cost, 255 wall)
- Integration field: Dijkstra flood-fill from target
- Flow field: each cell points to neighbor with lowest integration

### Implementation Steps
1. Define grid storage and indexing.
2. Implement queue-based Dijkstra (binary heap optional; start with bucketed/array queue).
3. Compute flow vectors.
4. Steering system applies flow as acceleration/desired velocity.

### Architectural Considerations
- One field per target; recompute on target move threshold.
- Agents read-only; avoids per-agent A*.

### Unit Tests (RED-first)
**Happy Path**
1. **Small open grid**
   - in an empty grid, flow vectors point directly toward the target.
2. **Obstacle avoidance**
   - U-shaped wall: vectors route around opening, not through walls.

**Edge Cases**
3. **Unreachable cells**
   - cells enclosed by walls remain at “infinite/unreachable” integration and have zero/none flow.
4. **Tie-breaking determinism**
   - when multiple neighbors have equal integration, choose a deterministic priority order and test it.
5. **Cost extremes**
   - high costs (254) are avoided versus low costs; walls (255) are never entered.
6. **Boundary cells**
   - flow computation at edges does not read out of bounds.

**Failure Modes**
7. **Invalid grid sizes**
   - zero width/height returns explicit failure.
8. **Invalid target**
   - target on a wall cell returns explicit failure.

### Regression Tests (RED-first)
1. **Gradient correctness**
   - integration values strictly decrease along flow path to target (within constraints).
2. **Integration overflow guard**
   - long paths with high costs must not overflow the integration accumulator type.

### Cumulative Integration Tests (RED-first, cumulative through P_011)
1. **Compute + apply flow to agents (P_000..P_011)**
   - compute flow field in scheduled jobs, then run a steering update that moves agents along flow.
   - verify agents monotonically approach the target (or reduce integration) and results are deterministic.

---



