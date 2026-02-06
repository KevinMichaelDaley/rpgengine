---
id: phys-405
status: closed
deps: [phys-402]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.5: Amortized Ticking for T4


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

T4 bodies tick every 3rd frame (10 Hz instead of 30 Hz). Interpolate
visually. T4 amortized cost: ~0.05 µs/body/tick (2 iterations, sphere
collider, amortized 10 Hz). 500 background props = 25 µs.

## Acceptance Criteria

- [ ] T4 bodies ticked at 10 Hz
- [ ] Visual interpolation between physics ticks
- [ ] 500 T4 props < 25 µs contribution

