---
id: rpg-w8bs
status: in_progress
deps: []
links: []
created: 2026-03-08T04:38:45Z
type: feature
priority: 0
assignee: KMD
tags: [physics, solver, ragdoll, stability]
---
# Coupled implicit Gauss-Seidel solver for TIER_ANIM ragdoll joints

Replace the decoupled TGS velocity-solve + split-impulse position-correction path for TIER_ANIM joint constraints with a fully coupled implicit Gauss-Seidel solver using XPBD-style compliance/damping regularization.

## Background

The current TGS solver for TIER_ANIM ragdoll joints uses a two-pass approach:
1. Velocity-level impulse solve (with optional Baumgarte bias)
2. Separate split-impulse pseudo-velocity position correction

This architecture injects energy into long articulated chains because the two passes fight each other: velocity corrections move bodies away from constraint satisfaction, then position correction pushes them back, generating oscillating kinetic energy that overwhelms damping. The ragdoll tumbles indefinitely after ground contact.

Prior attempts to fix this within the existing architecture (adjusting Baumgarte, damping formulas, iteration counts, XPBD routing) have all failed to produce stable settling behavior.

## Solution: Coupled Implicit GS with Regularization

Replace the joint-solving path in solve_one_constraint() (for is_joint constraints only) with a single coupled pass. The per-row update equation:

    Δλ_i = -(J_i·v + C_i/h + (α/h)·λ_total) / (J_i·M_eff⁻¹·J_i^T + α/h² + γ/h)

Then coupled velocity + position update:

    v ← v + M_eff⁻¹·J_i^T·Δλ_i
    p ← p + h·(M_eff⁻¹·J_i^T·Δλ_i)
    λ_total ← λ_total + Δλ_i

Where:
- α = compliance (inverse stiffness), from phys_constraint_t.compliance
- γ = damping coefficient, from phys_joint_t.damping
- λ_total = accumulated impulse for this row across all iterations this substep
- M_eff = M - h²K with geometric stiffness K (start with K=0)
- C_i = position-level constraint error (already in rows[r].bias from joint builders)
- h = substep dt

Key properties:
- Position and velocity coupled in a single pass (no split-impulse, no Baumgarte)
- α/h² regularization prevents infinite stiffness, allows soft joints
- γ/h regularization adds viscous dissipation (guaranteed energy removal)
- (α/h)·λ_total feedback drives λ toward zero for elastic constraints
- When α=0, γ=0: reduces to rigid coupled implicit GS
- Unconditionally stable for any h, α, γ ≥ 0

Contact constraints (non-joint) in the same island keep the existing TGS split-impulse path unchanged.

## Implementation Plan

1. Add phys_body_t *bodies_mut to phys_tgs_solve_args_t; pass bodies_next for TIER_ANIM
2. New coupled implicit path in solve_one_constraint() for is_joint constraints
3. After each row solve, update both velocity AND body position/orientation on bodies_mut
4. Track per-row λ_total (rows[r].lambda as accumulator, reset at substep start)
5. Rebuild joint Jacobians between GS iterations (positions change during solve)
6. Geometric stiffness K deferred (start with K=0, add if convergence needs it)

## Files to Modify

- src/physics/solver/tgs_solve.c (main change: coupled joint path)
- include/ferrum/physics/tgs_solve.h (add bodies_mut to args)
- src/physics/world/tick_parallel.c (pass mutable bodies for TIER_ANIM)
- src/physics/stages/par/tgs_solve_par.c (parallel coupled path)

## Acceptance Criteria

1. Joint constraints for TIER_ANIM islands use the coupled implicit update equation with compliance α and damping γ regularization
2. Each row solve updates BOTH velocity and body position/orientation (single coupled pass)
3. No Baumgarte bias or split-impulse pseudo-velocity correction for joint constraints
4. Contact constraints in the same island still use existing TGS split-impulse path
5. p005 ragdoll drop test: body velocities monotonically decay after ground contact (no energy injection)
6. p005 ragdoll drop test: joint anchor errors remain below 0.02m throughout simulation
7. p005 ragdoll drop test: ragdoll comes to rest (all body speeds < 0.1 m/s) within 3 seconds of ground contact
8. No joints break during normal ragdoll settling (with existing break_strength values)
9. Geometric stiffness K=0 is acceptable for initial implementation; convergence regression is not required
10. Existing physics tests (make test) pass without regression
11. Builds clean under -Wall -Wextra -Wpedantic

