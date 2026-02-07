---
id: rpg-xhl7
status: closed
deps: [rpg-pjrp]
links: []
created: 2026-02-07T19:44:41Z
type: task
priority: 1
assignee: KMD
parent: rpg-m9nw
---
# Sparse stabilization: Position correction and velocity sync

Apply the solved lambda to compute position corrections: delta_q = M^-1 * J^T * lambda. Update body positions and synchronize velocities: v_new = delta_q / dt. Integrate this as a new stage or replacement for the existing Baumgarte bias in the TGS pipeline. Only apply to TGS-tier bodies (not XPBD tiers).

## Acceptance Criteria

Positions corrected to remove penetration; velocities consistent with corrected positions; no energy injection; existing integrate stage unaffected for non-TGS tiers


## Notes

**2026-02-07T21:06:07Z**

IMPLEMENTATION NOTE: The velocity sync formula in the original tex doc (v_new = delta_q/dt) was naive — it replaces the entire velocity including tangential components from friction solving. Updated tex Section 5 to specify normal-component replacement: project out the old constraint-normal velocity and replace with the correction's normal component. This preserves TGS friction work and avoids energy injection. Also: position corrections must be applied AFTER the integrate stage, not before — applying before causes double-integration and exponential energy growth (discovered during implementation).
