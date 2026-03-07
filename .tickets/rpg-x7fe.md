---
id: rpg-x7fe
status: in_progress
deps: []
links: []
created: 2026-03-06T09:20:32Z
type: feature
priority: 3
assignee: KMD
---
# Joint physical properties (damping, elasticity, yield, break)

Add physical properties to joints for realistic ragdoll and destruction behavior. Each joint gets: damping (energy dissipation), elasticity (spring-like resistance), yield strength (plastic deformation threshold), and break strength (joint removal threshold).

## Properties per joint

- damping: float [0,∞) — viscous damping coefficient, dissipates angular/linear velocity
- elasticity: float [0,∞) — spring stiffness, pulls joint back to rest configuration  
- yield_strength: float [0,∞) — impulse threshold above which the joint permanently deforms
- break_strength: float [0,∞) — impulse threshold above which the joint is destroyed

0 means disabled for yield/break; 0 damping means no damping; 0 elasticity means rigid.

## Implementation

### Data (bone_joint_desc_t extension)
```c
float damping;         // viscous damping coefficient
float elasticity;      // spring stiffness (0 = rigid)
float yield_strength;  // plastic deformation threshold (0 = no yield)
float break_strength;  // joint destruction threshold (0 = unbreakable)
```

### Blender panel extension  
- Numeric inputs for each property per bone
- Visual preview: color-coding joints by strength
- Presets: Rigid, Elastic, Fragile, Ragdoll

### fskel export
- Extended JNTS chunk with additional float fields
- Backward-compatible: v2 JNTS without properties defaults to rigid/unbreakable

### Physics engine
- Damping: modify XPBD velocity derivation to include viscous term
- Elasticity: set XPBD compliance parameter per-constraint from elasticity
- Yield: track accumulated impulse per joint; when > yield_strength, shift rest configuration
- Break: when impulse > break_strength, remove constraint from solver

### Integration
- joint_accumulate_impulse() called after each XPBD iteration
- joint_check_yield_break() called after solve stage
- Broken joints trigger particle effect + sound event (separate system)

## Dependencies
- rpg-rcii: joint metadata in fskel (base joint support)
- rpg-mtqh: unified pipeline (joints participate in XPBD solver)

## Acceptance Criteria

- Damping coefficient reduces joint oscillation measurably
- Elasticity creates spring-like bounce (configurable stiffness)
- Yield threshold causes permanent deformation at correct impulse
- Break threshold removes joint constraint at correct impulse
- Blender panel shows all 4 properties per bone
- Properties export to fskel and load correctly
- Default values (rigid, no yield, no break) match current behavior
- Test: drop skeleton from height, joints with low break_strength snap
- Test: push skeleton, damped joints settle faster than undamped
- All existing tests pass

