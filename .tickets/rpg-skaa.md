---
id: rpg-skaa
status: closed
deps: [rpg-j0v8, rpg-t9ga]
links: []
created: 2026-07-05T22:57:34Z
type: task
priority: 1
assignee: KMD
parent: rpg-j0v8
tags: [srd, testing, libtorch]
---
# srd-test-02: critic gradient tests

Write tests/srd_critic_tests.cpp: for each loss term of AnalyticalCritic, verify gradient via finite difference (perturb cx/cz/hw/hd by 1e-4, compare analytic gradient to (f+ - f-) / 2e-4 within 1e-3 tolerance). Test TorchScriptCritic by scripting a trivial module (returns sum of params), saving to /tmp/test_critic.pt, loading, and verifying output.

## Design

Use torch::autograd::grad or loss.backward() for analytic gradients. Finite diff: for each parameter index, perturb +eps and -eps, compute (f(+eps)-f(-eps))/(2*eps). Test layout: 4 overlapping boxes (NonPenetration must be > 0), 4 undersized boxes (MinimumSize must be > 0), 1 isolated box (SoftReachability must be > 0).

## Acceptance Criteria

Gradient check passes for NonPenetration, MinimumSize, BoundsViolation (finite diff within 1e-3); TorchScriptCritic test passes end-to-end; gradient flows through TorchScriptCritic (backward does not throw)


## Notes

**2026-07-06T06:10:10Z**

DESIGN REVISION (2026-07-06): With the continuous L-BFGS phase dropped, the critic no longer needs differentiable gradients for optimization. Critic terms are now grid-based (room volume, flood-fill reachability, bounds violation). Gradient tests may be simplified or replaced with value-based tests verifying each critic term responds correctly to grid modifications.
