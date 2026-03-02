# Renderer Specification — Tiled Forward Rendering Pipeline

## 1. Overview

Ferrum's renderer is a **tiled forward rendering pipeline** targeting OpenGL 4.6
with AZDO (Approaching Zero Driver Overhead) patterns. It supports:

- **Point, directional, and spot lights** with both static and dynamic lighting
- **Parallel-Split Shadow Maps (PSSM)** precomputed at the caster stage for
  immobile lights, regenerated per-frame for mobile lights
- **SH probes** for global illumination approximation
- **Dynamic and static batching** for draw call reduction
- **Constraint-driven skeletal animation** extending the physics XPBD solver

The pipeline is organized into six implementation phases:

| Phase | Name | Scope |
|-------|------|-------|
| 1 | Type System | `static_mesh_t`, `skeletal_mesh_t`, VAO loading, material binding |
| 2 | Pipeline Configuration | Render passes, batched state updates, draw lists |
| 3 | Scene Graph | Left-child right-sibling tree over entity set |
| 4 | Material System | Shader permutations, uniform blocks, texture binding |
| 5 | Animation System | Constraint-driven skeletons, ragdoll, dynamic CCD |
| 6 | Lighting & Shadows | Tiled forward, PSSM, SH probes, lightmaps |

---

## 2. Phase 1 — Type System

### 2.1 Static Mesh (`static_mesh_t`)

A static mesh is an immutable piece of renderable geometry. It owns a VAO
and one or more submeshes, each referencing a material slot.

```c
/** A contiguous range of indices sharing one material. */
typedef struct render_submesh {
    uint32_t index_offset;   /**< Byte offset into the index buffer. */
    uint32_t index_count;    /**< Number of indices in this submesh. */
    uint16_t material_slot;  /**< Index into the entity's material table. */
} render_submesh_t;

/** Immutable renderable geometry (owned VAO + VBOs). */
typedef struct static_mesh {
    vao_t    vao;
    vbo_t    vbo_position;    /**< vec3 positions. */
    vbo_t    vbo_normal;      /**< vec3 normals. */
    vbo_t    vbo_tangent;     /**< vec4 tangents (xyz + handedness). */
    vbo_t    vbo_uv0;         /**< vec2 primary UVs. */
    vbo_t    vbo_uv1;         /**< vec2 lightmap UVs. */
    vbo_t    vbo_color;       /**< vec4 vertex colors. */
    vbo_t    ibo;             /**< uint32 index buffer (GL_ELEMENT_ARRAY_BUFFER). */
    uint32_t vertex_count;
    uint32_t index_count;
    render_submesh_t *submeshes;
    uint32_t          submesh_count;
    float    bsphere_radius;  /**< Bounding sphere for frustum culling. */
    float    aabb_min[3];
    float    aabb_max[3];
} static_mesh_t;
```

**Ownership:** The `static_mesh_t` owns all GPU resources (VAO, VBOs, IBO).
Destroy via `static_mesh_destroy()`. Submesh array is heap-allocated;
freed on destroy.

**Loading:**
- `static_mesh_create_from_fvma(const uint8_t *blob, size_t len, const gl_loader_t *loader, static_mesh_t *out)` — Deserializes the existing FVMA binary format into separate per-attribute VBOs with proper interleaving.
- `static_mesh_create_from_primitives(...)` — Box, sphere, capsule, plane generation (replaces `gen_capsule_mesh` etc. in demo_client).

**VAO Attribute Layout (all static meshes):**

| Location | Name | Type | Notes |
|----------|------|------|-------|
| 0 | `in_position` | vec3 | Always present |
| 1 | `in_normal` | vec3 | Always present |
| 2 | `in_tangent` | vec4 | Optional (flag in FVMA header) |
| 3 | `in_uv0` | vec2 | Optional |
| 4 | `in_uv1` | vec2 | Optional (lightmap) |
| 5 | `in_color` | vec4 | Optional |

Missing optional attributes bind a 1-element VBO with defaults
(tangent=(1,0,0,1), uv=(0,0), color=(1,1,1,1)) so shaders don't need
permutations for attribute presence.

### 2.2 Skeletal Mesh (`skeletal_mesh_t`)

Extends static mesh with bone weight and index attributes.

```c
/** Skeletal mesh: static mesh + bone weight/index buffers. */
typedef struct skeletal_mesh {
    static_mesh_t base;        /**< Inherits all static mesh fields. */
    vbo_t    vbo_bone_weights; /**< vec4 bone weights (4 influences). */
    vbo_t    vbo_bone_indices; /**< ivec4 bone indices. */
    uint32_t bone_count;       /**< Bind-pose bone count. */
    float   *inv_bind_matrices;/**< bone_count × mat4, row-major. */
} skeletal_mesh_t;
```

**Additional VAO attributes (extend static mesh VAO):**

| Location | Name | Type |
|----------|------|------|
| 6 | `in_bone_weights` | vec4 |
| 7 | `in_bone_indices` | ivec4 |

**Loading:**
- `skeletal_mesh_create_from_fvma(...)` — same FVMA format with bone data
  flag; requires `FVMA_FLAG_BONES` to be set.
- Future: `skeletal_mesh_create_from_gltf(...)` for full glTF 2.0 import.

### 2.3 Mesh Registry

A central registry maps mesh handles to loaded mesh data, enabling
shared geometry across entities.

```c
/** Opaque mesh handle (index + generation). */
typedef struct mesh_handle {
    uint32_t index;
    uint16_t generation;
} mesh_handle_t;

#define MESH_REGISTRY_CAPACITY 4096

/** Mesh type discriminator. */
typedef enum mesh_type {
    MESH_TYPE_STATIC   = 0,
    MESH_TYPE_SKELETAL = 1
} mesh_type_t;

/** Central mesh store. */
typedef struct mesh_registry {
    mesh_type_t       types[MESH_REGISTRY_CAPACITY];
    union {
        static_mesh_t   stat;
        skeletal_mesh_t skel;
    }                 meshes[MESH_REGISTRY_CAPACITY];
    uint16_t          generations[MESH_REGISTRY_CAPACITY];
    uint32_t          freelist[MESH_REGISTRY_CAPACITY];
    uint32_t          freelist_count;
    uint32_t          count;
    const gl_loader_t *loader;
} mesh_registry_t;
```

---

## 3. Phase 2 — Rendering Pipeline Configuration

### 3.1 Render Pass Architecture

The frame is organized into ordered passes. Each pass binds a framebuffer
(or default FBO), sets global state, and processes a draw list.

```
┌─────────────┐
│ Shadow Pass  │  ← Per-light shadow map generation (PSSM for directional)
├─────────────┤
│ Depth Pre   │  ← Optional depth pre-pass for early-Z
├─────────────┤
│ Caster Pass │  ← Precomputed shadow maps for immobile lights
├─────────────┤
│ Light Cull  │  ← Tiled light assignment (compute or CPU fallback)
├─────────────┤
│ Forward Pass│  ← Main shading: geometry + lighting + materials
├─────────────┤
│ Skybox Pass │  ← Drawn at max depth after forward
├─────────────┤
│ Debug Pass  │  ← Debug lines, gizmos, wireframes
├─────────────┤
│ Post Pass   │  ← Tone mapping, gamma, FXAA
├─────────────┤
│ UI Pass     │  ← 2D overlay
└─────────────┘
```

### 3.2 Draw Lists and Sorting

Each pass operates on a **draw list**: a flat array of draw commands
sorted to minimize state changes.

```c
/** Key for sort-by-state. */
typedef struct draw_sort_key {
    uint64_t key;  /**< Packed: [depth(16) | material(16) | mesh(16) | flags(16)] */
} draw_sort_key_t;

/** Single draw command in a draw list. */
typedef struct draw_command {
    draw_sort_key_t  sort_key;
    mesh_handle_t    mesh;
    uint32_t         submesh_index;
    uint32_t         instance_offset; /**< Offset into per-instance UBO. */
    uint32_t         instance_count;  /**< 1 for non-batched, N for instanced. */
} draw_command_t;

/** Draw list for a single render pass. */
typedef struct draw_list {
    draw_command_t *commands;
    uint32_t        count;
    uint32_t        capacity;
} draw_list_t;
```

**Sort order (front-to-back for opaque, back-to-front for transparent):**
1. Shader program (most expensive state change)
2. Material (texture binds)
3. Mesh (VAO bind)
4. Depth (front-to-back for early-Z benefit)

### 3.3 Batching

**Static batching:** At load time, merge meshes sharing the same material
and transform-invariant (world-space) into a single VBO+IBO. Produces one
draw call per material.

**Dynamic batching:** At runtime, adjacent draw commands with the same
material and mesh can be promoted to instanced draws. Per-instance data
(model matrix, entity ID) is uploaded to a persistent-mapped UBO/SSBO.

**Instance data layout (std140):**
```glsl
struct InstanceData {
    mat4 model;
    mat4 model_inv_transpose;  // for normal transformation
    uint entity_id;            // for picking / debug
};
```

Maximum batch size: 256 instances per draw call (fits a 64KB UBO at
256 bytes per instance).

---

## 4. Phase 3 — Scene Graph

### 4.1 Left-Child Right-Sibling Tree

The scene graph is a flat-array tree using LCRS (left-child, right-sibling)
representation over the existing entity set. This avoids pointer chasing
and keeps the hierarchy cache-friendly.

```c
/** Per-entity scene node. Stored parallel to entity arrays. */
typedef struct scene_node {
    uint32_t parent;        /**< Entity index of parent (UINT32_MAX = root). */
    uint32_t first_child;   /**< Entity index of first child (UINT32_MAX = leaf). */
    uint32_t next_sibling;  /**< Entity index of next sibling (UINT32_MAX = last). */
    uint32_t flags;         /**< SCENE_NODE_DIRTY_LOCAL, _DIRTY_WORLD, _STATIC. */
    mat4_t   local_transform;
    mat4_t   world_transform;
} scene_node_t;

#define SCENE_NODE_DIRTY_LOCAL  (1u << 0)
#define SCENE_NODE_DIRTY_WORLD  (1u << 1)
#define SCENE_NODE_STATIC       (1u << 2)  /**< World transform is baked. */

/** Scene graph over entity pool. */
typedef struct scene_graph {
    scene_node_t *nodes;
    uint32_t      capacity;   /**< Matches entity pool capacity. */
    uint32_t     *dirty_list; /**< Indices of dirty nodes for BFS update. */
    uint32_t      dirty_count;
} scene_graph_t;
```

**Update strategy:**
1. Mark modified entities `DIRTY_LOCAL`.
2. BFS from dirty roots, propagating `world = parent.world × local`.
3. Children inherit dirty flag (cascade).
4. Static nodes skip update unless explicitly invalidated.

**Integration with ECS:** Scene graph indices are entity indices.
The `scene_node_t` array is allocated parallel to the entity pool.
Entities without scene presence have `parent = first_child = next_sibling = UINT32_MAX`.

### 4.2 Skeleton Hierarchy

Skeletons are a special case of the scene graph. Each bone is an entity
with a `scene_node_t` in the LCRS tree, parented under a root skeleton
entity. This unifies:

- Artist-authored joint hierarchy
- Physics-driven bones (ragdoll)
- Attachment points (weapons, effects)

Bone entities carry a `bone_component_t` with bind-pose inverse and
weight metadata.

---

## 5. Phase 4 — Material System

### 5.1 Material Definition

A material binds a shader program to a set of parameters (uniforms + textures).

```c
/** Texture slot identifiers. */
typedef enum material_texture_slot {
    MATERIAL_TEX_ALBEDO    = 0,
    MATERIAL_TEX_NORMAL    = 1,
    MATERIAL_TEX_ROUGHNESS = 2,
    MATERIAL_TEX_METALLIC  = 3,
    MATERIAL_TEX_EMISSIVE  = 4,
    MATERIAL_TEX_LIGHTMAP  = 5,
    MATERIAL_TEX_COUNT     = 6
} material_texture_slot_t;

/** Render state flags. */
typedef enum material_flags {
    MATERIAL_FLAG_NONE        = 0,
    MATERIAL_FLAG_DOUBLE_SIDED = (1 << 0),
    MATERIAL_FLAG_ALPHA_CLIP   = (1 << 1),
    MATERIAL_FLAG_ALPHA_BLEND  = (1 << 2),
    MATERIAL_FLAG_UNLIT        = (1 << 3),
    MATERIAL_FLAG_WIREFRAME    = (1 << 4)
} material_flags_t;

/** Render queue assignment. */
typedef enum render_queue {
    RENDER_QUEUE_OPAQUE      = 0,
    RENDER_QUEUE_ALPHA_CLIP  = 1,
    RENDER_QUEUE_TRANSPARENT = 2,
    RENDER_QUEUE_OVERLAY     = 3
} render_queue_t;

/** Material instance. */
typedef struct render_material {
    uint32_t         shader_handle;   /**< GL program handle. */
    uint32_t         textures[MATERIAL_TEX_COUNT]; /**< GL texture handles. */
    float            base_color[4];   /**< RGBA base color multiplier. */
    float            roughness;       /**< 0..1 roughness parameter. */
    float            metallic;        /**< 0..1 metallic parameter. */
    float            emissive_strength;
    float            alpha_cutoff;    /**< Threshold for ALPHA_CLIP. */
    material_flags_t flags;
    render_queue_t   queue;
} render_material_t;
```

### 5.2 Shader Permutations

Rather than uber-shaders with dynamic branching, use **compile-time
permutations** via preprocessor defines:

| Define | Purpose |
|--------|---------|
| `HAS_NORMAL_MAP` | Enable tangent-space normal mapping |
| `HAS_LIGHTMAP` | Sample lightmap from UV1 |
| `HAS_VERTEX_COLOR` | Multiply base_color by vertex color |
| `HAS_SKINNING` | Include bone palette transform |
| `ALPHA_CLIP` | Discard fragments below alpha_cutoff |
| `NUM_DIR_LIGHTS` | Number of directional lights (0–4) |
| `NUM_POINT_LIGHTS_TILE` | Max point lights per tile |

**Permutation cache:** At startup, compile commonly-needed variants.
On-demand compilation for rare combinations with async compilation
(shader_program_create on a shared GL context).

### 5.3 Material Registry

```c
#define MATERIAL_REGISTRY_CAPACITY 1024

typedef struct material_registry {
    render_material_t materials[MATERIAL_REGISTRY_CAPACITY];
    uint16_t          generations[MATERIAL_REGISTRY_CAPACITY];
    uint32_t          freelist[MATERIAL_REGISTRY_CAPACITY];
    uint32_t          freelist_count;
    uint32_t          count;
} material_registry_t;
```

### 5.4 State Binding Order

When rendering, material state is applied in this order to minimize
redundant GL calls:

1. `glUseProgram` (only if shader changed)
2. Bind textures to units 0–5 (only changed slots)
3. Upload material UBO (base_color, roughness, metallic, emissive)
4. Set render state (cull face, blend mode, depth write)

The draw list sort key ensures adjacent draws share shaders/materials,
making most bind calls no-ops.

---

## 6. Phase 5 — Animation System

### 6.1 Architecture

The animation system extends the existing skinning pipeline into a
full constraint-driven system:

```
┌──────────────────────────────────────────────────────┐
│                  Animation Pipeline                   │
│                                                      │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │  Clip Eval   │→│ Blend Tree   │→│ IK / XPBD   │ │
│  │  (sampling)  │  │  (layered)   │  │ (constraint)│ │
│  └─────────────┘  └──────────────┘  └─────────────┘ │
│         │                                    │       │
│         ▼                                    ▼       │
│  ┌─────────────┐                    ┌─────────────┐  │
│  │ Bind Pose   │                    │ Bone Palette│  │
│  │ Fallback    │                    │ Upload      │  │
│  └─────────────┘                    └─────────────┘  │
└──────────────────────────────────────────────────────┘
```

### 6.2 Clip Evaluation

Animation clips are arrays of `(time, value)` keyframes per bone per channel
(translation, rotation, scale). Evaluation samples at the current time with
linear interpolation (lerp for vec3, slerp for quat).

```c
/** Animation channel target. */
typedef enum anim_channel {
    ANIM_CHANNEL_TRANSLATION = 0,
    ANIM_CHANNEL_ROTATION    = 1,
    ANIM_CHANNEL_SCALE       = 2
} anim_channel_t;

/** Single keyframe. */
typedef struct anim_keyframe {
    float time;
    union {
        float    vec3[3];
        float    quat[4];
    } value;
} anim_keyframe_t;

/** Animation track: one channel for one bone. */
typedef struct anim_track {
    uint32_t       bone_index;
    anim_channel_t channel;
    anim_keyframe_t *keyframes;
    uint32_t        keyframe_count;
} anim_track_t;

/** Animation clip. */
typedef struct anim_clip {
    const char  *name;
    float        duration;
    anim_track_t *tracks;
    uint32_t      track_count;
    uint8_t       looping;
} anim_clip_t;
```

### 6.3 Blend Tree

Layered blending with per-bone masks:

- **Additive layers:** Walk + lean (additive on upper body)
- **Override layers:** Aim override on spine chain
- **Cross-fade:** Smooth transition between clips with configurable duration

### 6.4 Constraint Solver (XPBD Extension)

After blend tree evaluation, bone transforms are post-processed by
constraints using the existing physics XPBD solver:

- **Look-at / aim constraints:** Orient bone toward target
- **Hinge / ball-socket limits:** Joint angle limits (reuse `phys_joint_t`)
- **IK chains:** Iterative FABRIK or CCD IK, finalized by XPBD
- **Spring / damper:** Secondary motion (hair, cloth, tails)

### 6.5 Ragdoll Fallback

Per-bone ragdoll activation:
- Each bone maps to a physics body with convex collider (from convex
  decomposition of the mesh segment weighted to that bone)
- Transition: blend from animation pose to physics-driven pose over
  configurable duration
- Individual bones can ragdoll independently (e.g., limp arm while
  rest of body is animated)

### 6.6 Dynamic CCD for Fast Bones

Weapon sweeps and fast-moving extremities use the existing dynamic CCD
system (§auto-CCD from `ccd_dynamic.c`):
- Bone velocity computed from frame-to-frame delta
- When displacement > 0.5 × bone_collider_radius, CCD sweep test is applied
- Contacts generate hit events for gameplay (damage, particles)

### 6.7 Bone Palette Upload

Final bone transforms (world-space × inverse-bind-pose) are packed into
a `bone_palette_buffer_t` (existing infrastructure) and uploaded as a
std140 UBO. Maximum 128 bones per skeleton.

---

## 7. Phase 6 — Lighting and Shadows

### 7.1 Light Types

```c
/** Light type discriminator. */
typedef enum light_type {
    LIGHT_TYPE_DIRECTIONAL = 0,
    LIGHT_TYPE_POINT       = 1,
    LIGHT_TYPE_SPOT        = 2
} light_type_t;

/** Light component. */
typedef struct light_component {
    light_type_t type;
    float        color[3];       /**< Linear-space RGB. */
    float        intensity;      /**< Luminous intensity (cd for point/spot, lux for dir). */
    float        range;          /**< Attenuation cutoff (point/spot). */
    float        inner_cone;     /**< Spot inner cone angle (radians). */
    float        outer_cone;     /**< Spot outer cone angle (radians). */
    uint8_t      cast_shadows;   /**< Non-zero to enable shadow map. */
    uint8_t      is_static;      /**< Non-zero: shadow map precomputed once. */
} light_component_t;
```

### 7.2 Tiled Light Culling

Screen-space tiles (16×16 pixels) are assigned visible lights via a
CPU-side culling pass (with optional compute shader acceleration):

1. Extract tile frustum from projection matrix + tile bounds
2. Test each light's bounding sphere against tile frustum
3. Pack per-tile light index lists into an SSBO
4. Forward shader reads tile index from `gl_FragCoord`, iterates light list

**SSBO layout:**
```glsl
struct TileData {
    uint light_count;
    uint light_indices[MAX_LIGHTS_PER_TILE];  // 64 max
};
layout(std430) buffer LightTiles {
    TileData tiles[];  // (screen_w/16) × (screen_h/16)
};
```

### 7.3 Parallel-Split Shadow Maps (PSSM)

Directional lights use PSSM with 4 cascade splits:

| Cascade | Near | Far | Resolution |
|---------|------|-----|------------|
| 0 | znear | 10m | 2048×2048 |
| 1 | 10m | 30m | 2048×2048 |
| 2 | 30m | 100m | 1024×1024 |
| 3 | 100m | 500m | 1024×1024 |

**Static light optimization:** For immobile directional lights (e.g., sun),
shadow maps are rendered once at the **caster stage** and cached.
Only re-rendered when:
- A dynamic object enters/exits the cascade frustum
- The light direction changes

Point lights use a single cube map (6-face, 512×512 per face).
Spot lights use a single 2D shadow map (1024×1024).

### 7.4 SH Probes (Spherical Harmonics)

Baked irradiance probes placed on a 3D grid provide ambient lighting:

- **L2 spherical harmonics** (9 coefficients × 3 channels = 27 floats per probe)
- Probe grid spacing: configurable, typically 4–8 meters
- Per-fragment: trilinear interpolation of 8 nearest probes
- Uploaded as a 3D texture (4 × RGBA32F texels per probe, 36 floats)

**Probe baking** is offline (ray-traced or rasterized to cubemap → SH
projection). Runtime update for dynamic GI is out of scope for Phase 6.

### 7.5 Shader Lighting Interface

```glsl
// Material UBO (binding = 0)
layout(std140) uniform MaterialParams {
    vec4  base_color;
    float roughness;
    float metallic;
    float emissive_strength;
    float alpha_cutoff;
};

// Per-frame UBO (binding = 1)
layout(std140) uniform FrameParams {
    mat4  view;
    mat4  proj;
    mat4  view_proj;
    vec3  camera_pos;
    float time;
    vec4  cascade_splits;        // PSSM split distances
    mat4  light_space[4];        // PSSM cascade matrices
};

// Light SSBO (binding = 2)
struct Light {
    vec4  position_type;   // xyz = position, w = type (0/1/2)
    vec4  direction_range; // xyz = direction, w = range
    vec4  color_intensity; // xyz = color, w = intensity
    vec4  cone_angles;     // x = inner, y = outer, zw = reserved
};
layout(std430) buffer LightBuffer {
    uint  light_count;
    Light lights[];
};

// Tile index SSBO (binding = 3)
layout(std430) buffer TileLightIndices {
    TileData tiles[];
};

// Shadow maps
uniform sampler2DArray u_shadow_cascades;  // PSSM cascades
uniform samplerCube    u_shadow_point[4];  // Point light cubemaps
uniform sampler2D      u_shadow_spot[4];   // Spot light maps
```

---

## 8. Vertex Attribute Canonical Locations

All shaders in the engine use consistent attribute locations:

| Location | Name | Type | Used By |
|----------|------|------|---------|
| 0 | `in_position` | vec3 | All |
| 1 | `in_normal` | vec3 | All |
| 2 | `in_tangent` | vec4 | Normal-mapped |
| 3 | `in_uv0` | vec2 | Textured |
| 4 | `in_uv1` | vec2 | Lightmapped |
| 5 | `in_color` | vec4 | Vertex-colored |
| 6 | `in_bone_weights` | vec4 | Skinned |
| 7 | `in_bone_indices` | ivec4 | Skinned |

---

## 9. UBO Binding Points

| Binding | Name | Contents |
|---------|------|----------|
| 0 | MaterialParams | base_color, roughness, metallic, emissive, alpha_cutoff |
| 1 | FrameParams | view, proj, VP, camera_pos, time, cascade data |
| 2 | InstanceData | Per-instance model matrices (up to 256) |
| 3 | BonePalette | Bone matrices (up to 128 × mat4) |

---

## 10. SSBO Binding Points

| Binding | Name | Contents |
|---------|------|----------|
| 0 | LightBuffer | Light array + count |
| 1 | TileLightIndices | Per-tile light index lists |

---

## 11. File Organization

```
include/ferrum/renderer/
├── mesh/
│   ├── static_mesh.h        # static_mesh_t, create/destroy/draw
│   ├── skeletal_mesh.h      # skeletal_mesh_t, extends static_mesh
│   └── mesh_registry.h      # mesh_handle_t, registry
├── material/
│   ├── render_material.h    # render_material_t, texture slots
│   └── material_registry.h  # material registry
├── scene/
│   ├── scene_graph.h        # LCRS scene graph
│   └── scene_node.h         # scene_node_t
├── draw/
│   ├── draw_list.h          # draw_command_t, draw_list_t
│   └── draw_sort.h          # sort key construction
├── light/
│   ├── light_component.h    # light_component_t
│   ├── light_cull.h         # tiled light culling
│   └── shadow_map.h         # PSSM, point/spot shadow maps
├── anim/
│   ├── anim_clip.h          # anim_clip_t, keyframes
│   ├── anim_blend.h         # blend tree, layers
│   └── anim_constraint.h   # IK, ragdoll, XPBD bone constraints
├── shader_program.h         # (existing)
├── shader_uniforms.h        # (existing)
├── vao.h                    # (existing)
├── vbo.h                    # (existing)
├── render_pipeline.h        # (existing, extended)
└── skinning_shader.h        # (existing, absorbed into anim/)

src/renderer/
├── mesh/
│   ├── static_mesh_create.c
│   ├── static_mesh_destroy.c
│   ├── static_mesh_draw.c
│   ├── static_mesh_fvma.c
│   ├── static_mesh_primitives.c
│   ├── skeletal_mesh_create.c
│   ├── skeletal_mesh_destroy.c
│   └── mesh_registry.c
├── material/
│   ├── render_material.c
│   └── material_registry.c
├── scene/
│   ├── scene_graph_init.c
│   ├── scene_graph_update.c
│   ├── scene_graph_attach.c
│   └── scene_graph_detach.c
├── draw/
│   ├── draw_list.c
│   └── draw_sort.c
├── light/
│   ├── light_cull.c
│   ├── shadow_pssm.c
│   ├── shadow_point.c
│   └── shadow_spot.c
├── anim/
│   ├── anim_clip_eval.c
│   ├── anim_blend.c
│   ├── anim_constraint.c
│   └── anim_ragdoll.c
└── (existing files remain)
```

---

## 12. Integration Points

### 12.1 Physics ↔ Animation
- Ragdoll bones are physics bodies with joint constraints
- XPBD solver shared between physics joints and bone constraints
- Dynamic CCD for fast bone sweeps reuses `ccd_dynamic.c` auto-activation

### 12.2 Network ↔ Renderer
- Server sends mesh data via `NET_REPL_SCHEMA_MESH_DATA` (existing FVMA chunks)
- Static meshes deserialized into `static_mesh_t` on client receive
- Skeletal mesh data + bind pose sent as extended FVMA with bone flag

### 12.3 Editor ↔ Renderer
- `edit_entity_t.materials[5]` maps to `render_material_t` texture slots
- Entity spawn messages include shape_type → mesh_type mapping
- Scene graph mirrors editor entity hierarchy

### 12.4 ECS ↔ Scene Graph
- Entity pool indices are scene node indices (parallel arrays)
- Entity creation → `scene_graph_attach(graph, entity_idx, parent_idx)`
- Entity deletion → `scene_graph_detach(graph, entity_idx)` (reparents children)

---

## 13. Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Draw calls per frame | < 2000 | After batching |
| State changes per frame | < 500 | Shader + material + VAO |
| Shadow map renders | ≤ 8 | 4 PSSM cascades + 4 dynamic lights |
| Bone palette uploads | ≤ 64 | 64 skinned characters on screen |
| Light count | ≤ 1024 | Tiled culling handles density |
| Tile size | 16×16 px | Balance between culling and overhead |
| Frame budget (render) | < 4ms | At 1080p, 60Hz |

---

## 14. Migration Path

### 14.1 Demo Client Migration

The `demo_client.c` ad-hoc rendering will be incrementally replaced:

1. **Phase 1:** Replace `gen_*_mesh()` functions with `static_mesh_create_from_primitives()`.
   Replace per-body VBO/VAO with `mesh_registry` lookups. Keep existing
   single-shader path but use `render_material_t` for uniform management.

2. **Phase 2:** Replace manual draw loop with `draw_list` + sort + submit.
   Add depth pre-pass.

3. **Phase 3:** Wire entity hierarchy to `scene_graph_t`. Use world
   transforms from scene graph instead of per-body matrices.

4. **Phase 4:** Replace inline shader strings with material registry.
   Add capsule shader as a material variant (no more special-case code).

5. **Phase 5:** Integrate `skeletal_mesh_t` for character rendering.
   Wire existing skinning pipeline through animation system.

6. **Phase 6:** Add lights, shadow maps, tiled culling. Replace flat
   `u_color` with full surface shader evaluation.

### 14.2 Backward Compatibility

During migration, both old and new paths coexist:
- `body_render_info_t` continues to work for physics debug visualization
- New material system is opt-in per entity type
- Existing FVMA pipeline unchanged; `static_mesh_create_from_fvma` wraps it

---

## 15. Dependencies

### Allowed
- OpenGL 4.6 (core profile)
- SDL2 (windowing, input, GL context)
- GLAD (GL function loading)
- Standard C library, POSIX

### Forbidden
- Third-party rendering libraries (no bgfx, no sokol)
- Third-party scene graph libraries
- Third-party animation libraries

### Future (whitelisted when needed)
- stb_image for texture loading
- cgltf or custom parser for glTF 2.0 import
