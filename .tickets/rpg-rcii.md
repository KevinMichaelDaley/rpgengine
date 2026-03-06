---
id: rpg-rcii
status: open
deps: []
links: []
created: 2026-03-06T09:19:20Z
type: feature
priority: 2
assignee: KMD
---
# Joint metadata export in fskel format

Export standard joint constraints (ball, hinge, distance) as metadata in Blender and store them in the .fskel binary format. These joints define structural integrity between bones so that the skeleton doesn't fall apart when external forces are applied during physics simulation.

## Blender side (export_fskel.py extension)

### Per-bone joint type panel
- Joint type selector: Auto (default, infers ball from hierarchy) / Ball / Hinge / Distance / None
- Hinge axis selector: X / Y / Z (local bone axis)
- Distance rest length: float (auto from bone length if 0)
- Joint limits: min/max angle for hinge, min/max distance for distance

### Export
- New JNTS chunk in fskel v2 format (after COLL chunk from rpg-5ysw)
- Per parent-child bone pair: joint_type, axis, limits, rest_length
- Coordinate conversion applied to axes (Blender Z-up → engine Y-up)

## Engine side (fskel format extension)

### Format
```c
typedef struct bone_joint_desc {
    uint32_t joint_type;     // 0=none, 1=ball, 2=hinge, 3=distance
    float    axis[3];        // hinge axis (local to parent bone)
    float    rest_length;    // distance joint rest length
    float    limit_min;      // min angle (hinge) or min distance
    float    limit_max;      // max angle (hinge) or max distance
} bone_joint_desc_t;
```

### Loader
- fskel_load detects v2 and reads JNTS chunk after COLL chunk
- v1 files: auto-generate ball joints for every parent-child pair
- skeleton_def_t gains: bone_joint_desc_t *joints

### Integration with physics
- ragdoll_create() reads bone joint descriptors
- Creates phys_joint_t per parent-child pair from descriptor
- Maps to existing ball/hinge/distance joint builders
- Joints converted to phys_constraint_t rows for XPBD solver

## Acceptance Criteria

- Blender panel allows per-bone joint type configuration
- Joint metadata round-trips through fskel export/import
- v1 files still load with auto-generated ball joints
- ragdoll_create uses joint descriptors from fskel
- Hinge joints constrain rotation to single axis
- Distance joints maintain bone-length separation
- All existing tests pass

