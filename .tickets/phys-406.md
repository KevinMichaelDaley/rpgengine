---
id: phys-406
status: closed
deps: [phys-401, phys-403]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.6: Occlusion-Based Tier Demotion


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

## Description

The renderer provides a `visibility_set` bitfield each frame indicating which
bodies are currently visible to the player camera (after occlusion culling).
Bodies that would be T1 by distance but are NOT visible are demoted to T3+.

Cost savings: T1 full TGS = ~14.0 µs/body → T3 XPBD = ~0.33 µs/body = 42× cheaper.
20 occluded nearby bodies saves ~270 µs per tick.

## Files

- `src/physics/stages/tier_classify.c` (extend)
- `tests/physics/tier_classify_occlusion_tests.c`

## Algorithm

```
for each body in T1:
    if !visibility_set[body_id]:
        demote to T3
```

On re-promotion: position nudge < 5mm lerped over 3–5 frames.

## Test Cases

```c
// test_occluded_body_demotes_to_t3
// test_visible_body_stays_t1
// test_re_promotion_position_nudge
// test_solver_transition_on_visibility_change
```

## Acceptance Criteria

- [ ] Occluded T1 bodies demoted to T3
- [ ] Re-promotion triggers position nudge (< 5mm correction over 3–5 frames)
- [ ] Solver warm-start conversion on tier change
- [ ] No visual pop on re-promotion

