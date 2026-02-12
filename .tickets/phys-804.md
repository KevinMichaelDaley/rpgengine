---
id: phys-804
status: in_progress
deps: [phys-801, phys-802, phys-803]
links: [phys-800]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 8.4: Phase 8 Integration Test


**Parent Epic:** phys-800 (Phase 8: Joints)

## Test Cases

```c
// test_pendulum_swings          (ball joint + gravity = pendulum)
// test_chain_of_bodies          (distance joints linking 5 bodies)
// test_door_hinge               (hinge joint with limits)
// test_ragdoll_basic            (compound → ragdoll with ball joints)
```

## Acceptance Criteria

- [ ] Pendulum converges to rest
- [ ] Chain doesn't stretch beyond joint limits
- [ ] Ragdoll transition from compound collider works

