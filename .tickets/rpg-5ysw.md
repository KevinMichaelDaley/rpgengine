---
id: rpg-5ysw
status: open
deps: []
links: []
created: 2026-03-06T07:54:37Z
type: feature
priority: 2
assignee: KMD
---
# Per-bone collision geometry in Blender and fskel format

Add support for assigning collision geometry (capsule, box, sphere, or per-vertex-group convex hull) to individual bones in Blender, previewing the shapes with wireframe overlays, enabling CCD per bone, and exporting/importing this data in the .fskel binary format.

## Blender-side (addon extension to export_fskel.py)

### Custom bone properties
- Per-bone custom property panel: "Talarium Physics"
- Collision shape selector: None / Capsule / Box / Sphere / Convex Hull
- Capsule params: radius, height, axis (X/Y/Z)
- Box params: half-extents (x, y, z)
- Sphere params: radius
- Convex Hull params: vertex group name (auto-generates from mesh vertices in that group)
- CCD toggle: enable continuous collision detection per bone
- Mass override: optional per-bone mass value (default: auto from volume)

### Wireframe preview
- Custom draw handler that generates wireframe meshes from collision parameters
- Capsule: cylinder + hemispheres wireframe
- Box: wireframe cube
- Sphere: 3-circle wireframe (XY, XZ, YZ planes)
- Convex Hull: wireframe of computed hull
- Preview updates live as parameters change
- Toggle visibility via bone property or viewport overlay

### Export
- Extend fskel format: new COLL chunk after IBM chunk
- Per-bone collision descriptor: shape type, params, CCD flag, mass
- Convex hull: vertex data embedded inline (count + float3 array)
- Bones without collision export as shape_type=NONE

## Engine-side (fskel format extension)

### Format extension (fskel_format.h)
- FSKEL_VERSION bumps to 2 (backward-compatible: v1 files have no COLL chunk)
- New chunk after IBMs:
  - [joint_count x bone_collider_desc_t] collision descriptors
  - [variable] convex hull vertex data (referenced by offset+count in descriptors)

### bone_collider_desc_t struct
```c
typedef struct bone_collider_desc {
    uint32_t shape_type;     // 0=none, 1=capsule, 2=box, 3=sphere, 4=convex_hull
    float    params[6];      // capsule: radius,height,axis / box: hx,hy,hz / sphere: radius,0,0
    uint32_t ccd_enabled;    // 1=CCD on
    float    mass;           // 0=auto from volume
    uint32_t hull_offset;    // byte offset into hull vertex data (shape_type=4 only)
    uint32_t hull_count;     // vertex count for convex hull
} bone_collider_desc_t;
```

### Loader extension (fskel_load)
- Detect version 2, read COLL chunk
- Version 1 files: synthesize empty colliders (shape_type=NONE for all)
- skeleton_def_t gains: bone_collider_desc_t *colliders, float *hull_vertices, uint32_t hull_vertex_count

### Ragdoll builder integration
- ragdoll_create() reads bone collider descriptors
- Creates phys_collider_t per bone from the descriptor
- Capsule/box/sphere map directly to existing collider types
- Convex hull uses existing convex collider builder
- CCD flag sets body->flags |= PHYS_BODY_CCD
- Mass from descriptor or auto-computed from shape volume * density

## Acceptance criteria
- Blender custom properties panel appears on bones in Pose mode
- Wireframe previews render correctly for all 4 shape types
- Shape parameters round-trip through export/import without data loss
- Version 1 .fskel files still load (backward compatibility)
- CCD flag propagates to physics bodies
- Visual test: humanoid with collision capsules on limbs, wireframe overlay in Blender, ragdoll simulation in engine

## Acceptance Criteria

- Per-bone collision shape assignment UI in Blender
- Live wireframe preview of collision shapes on bones
- CCD toggle per bone
- fskel v2 format with COLL chunk, backward compatible with v1
- bone_collider_desc_t in skeleton_def_t
- fskel_load handles v1 and v2 files
- ragdoll_create uses bone collider descriptors
- Convex hull from vertex group exports and loads correctly
- All existing tests pass (v1 files unchanged)

