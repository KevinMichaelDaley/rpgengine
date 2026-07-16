---
id: rpg-n1kq
status: closed
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-imlo]
created: 2026-07-09T02:42:01Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: vaults and arches

Ceiling vault and doorway arch rules using SDF interpolation. These two features define ~90% of the visual character of the dwarven reference dataset.

## Design

### Core technique: SDF interpolation

For curved shapes, generate the target shape as a local SDF, then lerp:

    sdf_new(v) = (1 - t) * sdf_current(v) + t * sdf_target(v)

Differentiable w.r.t. t, local, reversible.

### Rules (6 rules in 2 files)

**srd_rules_vault.c** (4 functions, face=CEIL):

1. **barrel_vault**: Half-cylinder ceiling along room long axis. Detect long axis from bbox. Cylinder center at (mid_x, ceil_y), radius = half-width of short axis. Lerp ceiling region. param ∈ [0.3, 1.0]: vault depth.
2. **barrel_vault_remove**: Inverse — lerp back toward flat ceiling.
3. **groin_vault**: Two perpendicular barrel vaults, take max (intersection). Creates cross-ribbed ceiling. param ∈ [0.3, 1.0].
4. **dome**: Hemisphere centered on room, radius = min(half_w, half_d). param ∈ [0.3, 1.0].

**srd_rules_arch.c** (2 functions, face=N/S/E/W):

5. **arch_doorway**: Find doorway on selected face (where room voxels are face-adjacent to another room). Shape top into semicircular arch via SDF interpolation. param ∈ [0.3, 1.0]: 0.3 = segmental, 1.0 = full semicircle.
6. **arch_doorway_remove**: Inverse — restore square top.

### Files

- include/ferrum/procgen/srd/srd_rules_vault.h
- include/ferrum/procgen/srd/srd_rules_arch.h
- src/procgen/srd/srd_rules_vault.c
- src/procgen/srd/srd_rules_arch.c
- srd_voxel_rule_table.c: +6 entries
- tests/srd_rules_vault_tests.c
- tests/srd_rules_arch_tests.c

## Acceptance Criteria

- 6 new rules compile, link, pass tests
- Barrel vault produces visually correct half-cylinder ceiling
- Groin vault produces cross-ribbed intersection of two barrel vaults
- Arch doorway shapes doorway tops into semicircles
- All rules reversible: apply then remove restores grid within float tolerance
- make test passes with FAISS_STUB=1


## Notes

**2026-07-16T03:19:32Z**

OBSOLETE: SDF-rewrite-rules approach abandoned; replaced by Blender procgen (bpy/bmesh arch-mesh generators).
