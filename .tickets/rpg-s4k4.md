---
id: rpg-s4k4
status: closed
deps: []
links: []
created: 2026-03-11T02:30:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-ovxp
tags: [physics, biomechanics]
---
# Activation dynamics

First-order ODE for muscle activation modeling neural-to-mechanical delay:
- da/dt = (u - a) / tau, where tau = tau_rise when u > a, tau_fall when u < a
- Typical values: tau_rise = 10-20ms, tau_fall = 40-60ms
- Input: excitation signal u ∈ [0,1] from animation or AI controller
- Output: activation a ∈ [0,1] used by force model
- Integrate with semi-implicit Euler at physics timestep

