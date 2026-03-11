---
id: rpg-xg1v
status: open
deps: [rpg-rw0h, rpg-s4k4, rpg-fvio]
links: []
created: 2026-03-11T02:30:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-ovxp
tags: [physics, biomechanics]
---
# Antagonist muscle pairing and co-contraction

Support paired muscles on opposite sides of a joint:
- Flexor/extensor pairs (e.g., biceps/triceps)
- Net torque = flexor_torque - extensor_torque
- Joint stiffness from co-contraction: both muscles active simultaneously
- Stiffness ∝ sum of activations × moment arms
- API: phys_muscle_pair_t linking two muscle units to a joint DOF
- Controller interface: set activation for each muscle independently

