---
id: rpg-xhl7
status: open
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

