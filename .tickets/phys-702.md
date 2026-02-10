---
id: phys-702
status: in_progress
deps: [phys-408]
links: [phys-700]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 7.2: Speculative Contacts


**Parent Epic:** phys-700 (Phase 7: Advanced Stability)

Generate contacts for close-but-not-touching pairs to prevent tunneling
of fast-moving objects. Predict contact based on velocity and generate
constraint before penetration occurs.

## Acceptance Criteria

- [ ] Fast-moving small objects don't tunnel through surfaces
- [ ] Speculative bias prevents false resting contacts

