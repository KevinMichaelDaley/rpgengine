# Renderer Specification — Tiled Forward Rendering Pipeline

## 1. Overview

Ferrum's renderer is a **tiled forward rendering pipeline** targeting OpenGL 3.3+
(core profile) with a custom GL function loader pattern (no GLAD/GLEW). It supports:

- **Point, directional, and spot lights** with both static and dynamic lighting
- **Parallel-Split Shadow Maps (PSSM)** precomputed at the caster stage for
  immobile lights, regenerated per-frame for mobile lights
- **SH probes** for global illumination approximation
- **Dynamic and static batching** for draw call reduction
- **Constraint-driven skeletal animation** extending the physics XPBD solver
- **glTF 2.0 / GLB import** via cgltf for meshes and skeletons
- **GPU-buffered video capture** with async PBO readback and encoder thread

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

## 2. GL Loader Pattern

All renderer modules avoid linking directly against GL symbols. Instead, GL
function pointers are resolved at runtime via a `gl_loader_t` table.

### 2.1 GL Loader (`gl_loader_t`)

```c
typedef struct gl_loader {
    void *(*get_proc_address)(const char *name, void *user_data);
    void *user_data;
} gl_loader_t;
```

Each renderer wrapper (VAO, VBO, shader program, bone palette, UBO, etc.)
resolves only the GL functions it needs at creation time and stores the
function pointers directly in its struct. This avoids global state and
enables headless/mock testing.

### 2.2 LOAD_GL_PROC Macro

Both the renderer and editor modules use a common pattern to resolve function
pointers while avoiding the ISO C pedantic warning about casting `void*` to
function pointers:

```c
#define LOAD_GL_PROC(field, loader_ptr, name)                 \
    do {                                                       \
        void *raw_ = (loader_ptr)->get_proc_address(           \
            (name), (loader_ptr)->user_data);                  \
        memcpy(&(field), &raw_, sizeof(field));                \
    } while (0)
```

### 2.3 GL Loader Validation

`gl_loader_validate()` checks that all 27 required GL function pointers
(shader compilation, buffer management, VAO operations) are resolvable.
Returns `GL_LOADER_OK` or `GL_LOADER_ERR_MISSING` with the first missing
symbol name.

### 2.4 GL Constants Header

`gl_constants.h` provides `#ifndef`-guarded defines for all GL constants
used by renderer wrappers (buffer targets, shader types, texture formats,
blending, framebuffer enums, etc.), allowing compilation without pulling in
the full GL header. Constants include:

- Buffer targets: `GL_ARRAY_BUFFER`, `GL_ELEMENT_ARRAY_BUFFER`, `GL_UNIFORM_BUFFER`, `GL_SHADER_STORAGE_BUFFER`, `GL_TEXTURE_BUFFER`
- Texture: `GL_TEXTURE_2D`, `GL_RGBA8`, `GL_RGBA32F`, `GL_R8`, `GL_RED`, `GL_RGBA`, `GL_LINEAR`, `GL_NEAREST`, `GL_CLAMP_TO_EDGE`
- Framebuffer: `GL_FRAMEBUFFER`, `GL_DRAW_FRAMEBUFFER`, `GL_RENDERBUFFER`, `GL_COLOR_ATTACHMENT0`, `GL_DEPTH_STENCIL_ATTACHMENT`, `GL_FRAMEBUFFER_COMPLETE`, `GL_DEPTH24_STENCIL8`
- Draw modes: `GL_LINES`, `GL_TRIANGLES`
- Blending: `GL_BLEND`, `GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`
- State: `GL_DEPTH_TEST`, `GL_SCISSOR_TEST`
- Clear: `GL_COLOR_BUFFER_BIT`, `GL_DEPTH_BUFFER_BIT`

---

## 3. VAO / VBO Renderer Wrappers

### 3.1 VBO (`vbo_t`)

Wraps a GL buffer object. Each instance stores its own GL function pointers
(resolved from `gl_loader_t` at creation). Supports create, destroy, upload,
and handle query.

```c
typedef struct vbo {
    uint32_t handle;
    void (*glGenBuffers)(int32_t count, uint32_t *buffers);
    void (*glDeleteBuffers)(int32_t count, const uint32_t *buffers);
    void (*glBindBuffer)(uint32_t target, uint32_t buffer);
    void (*glBufferData)(uint32_t target, size_t size, const void *data, uint32_t usage);
} vbo_t;
```

Status codes: `VBO_OK`, `VBO_ERR_INVALID`, `VBO_ERR_MISSING_GL`, `VBO_ERR_ZERO_SIZE`.

### 3.2 VAO (`vao_t`)

Wraps a GL vertex array object. Stores function pointers for VAO operations
and attribute configuration (both float and integer attributes via
`glVertexAttribPointer` and `glVertexAttribIPointer`).

```c
typedef struct vao {
    uint32_t handle;
    void (*glGenVertexArrays)(int32_t count, uint32_t *arrays);
    void (*glDeleteVertexArrays)(int32_t count, const uint32_t *arrays);
    void (*glBindVertexArray)(uint32_t array);
    void (*glEnableVertexAttribArray)(uint32_t index);
    void (*glVertexAttribPointer)(...);
    void (*glVertexAttribIPointer)(...);
    void (*glBindBuffer)(uint32_t target, uint32_t buffer);
} vao_t;
```

### 3.3 VAO Attribute Binding (`vao_attribute_t`)

Vertex attribute descriptors used by `vao_bind_attributes()`:

```c
typedef struct vao_attribute {
    uint32_t index;       /**< Attribute location. */
    int      components;  /**< Number of components (1-4). */
    uint32_t type;        /**< GL type (GL_FLOAT, GL_INT, etc.). */
    uint8_t  normalized;  /**< GL_TRUE for normalized integers. */
    uint32_t offset;      /**< Byte offset within vertex stride. */
    uint8_t  integer;     /**< Non-zero to use glVertexAttribIPointer. */
} vao_attribute_t;
```

`vao_bind_attributes(vao, vbo, attributes, count, stride)` binds a VBO to a
VAO with the specified attribute layout. The `integer` flag selects between
float and integer attribute pointer functions.

---

## 4. Shader System

### 4.1 Shader Program (`shader_program_t`)

Compiles and links vertex + fragment shaders. Stores all required GL function
pointers inline (no globals):

```c
typedef struct shader_program {
    uint32_t handle;
    uint32_t (*glCreateShader)(uint32_t type);
    void (*glShaderSource)(...);
    void (*glCompileShader)(uint32_t shader);
    /* ... 14 more GL function pointers ... */
    void (*glUniform1f)(int32_t location, float v0);
} shader_program_t;
```

**API:**
- `shader_program_create(program, loader, vert_src, frag_src, log_buf, log_cap)` -- compile + link
- `shader_program_bind(program)` -- `glUseProgram`
- `shader_program_destroy(program)` -- `glDeleteProgram`
- `shader_program_handle(program)` -- get GL program name

Status codes: `SHADER_PROGRAM_OK`, `ERR_INVALID`, `ERR_MISSING_GL`, `ERR_COMPILE`, `ERR_LINK`.

### 4.2 Shader Uniforms (`shader_uniform_cache_t`)

Caches uniform locations with type checking. Avoids repeated `glGetUniformLocation`
calls and detects type mismatches at runtime.

```c
#define SHADER_UNIFORM_CACHE_CAPACITY 64u

typedef struct shader_uniform_cache {
    struct {
        const char *name;
        int32_t     location;
        uint8_t     type;      /**< SHADER_UNIFORM_TYPE_MAT4/VEC3/INT/FLOAT */
    } entries[SHADER_UNIFORM_CACHE_CAPACITY];
    uint32_t count;
    /* GL function pointers for uniform upload */
} shader_uniform_cache_t;
```

**API:**
- `shader_uniform_cache_init(cache, program)` -- initialize from shader program
- `shader_uniform_set_mat4(cache, program, name, value, transpose)`
- `shader_uniform_set_vec3(cache, program, name, value)`
- `shader_uniform_set_int(cache, program, name, value)`
- `shader_uniform_set_float(cache, program, name, value)`

Returns `SHADER_UNIFORM_ERR_TYPE_MISMATCH` if a name was previously cached
with a different type. Returns `SHADER_UNIFORM_ERR_CACHE_FULL` if all 64
slots are used.

---

## 5. Phase 1 — Mesh Type System

### 5.1 Static Mesh (`static_mesh_t`)

Immutable renderable geometry owning a VAO, per-attribute VBOs, an index buffer,
and one or more submeshes. Each submesh references a material slot and a
contiguous index range.

```c
typedef struct render_submesh {
    uint32_t index_offset;   /**< Start index (element count, not bytes). */
    uint32_t index_count;    /**< Number of indices. */
    uint16_t material_slot;  /**< Material table index. */
} render_submesh_t;

typedef struct static_mesh {
    vao_t    vao;
    vbo_t    vbo_position;    /**< vec3 positions   (location 0). */
    vbo_t    vbo_normal;      /**< vec3 normals     (location 1). */
    vbo_t    vbo_tangent;     /**< vec4 tangents    (location 2). */
    vbo_t    vbo_uv0;         /**< vec2 primary UVs (location 3). */
    vbo_t    vbo_uv1;         /**< vec2 lightmap UVs(location 4). */
    vbo_t    vbo_color;       /**< vec4 vertex color(location 5). */
    vbo_t    ibo;             /**< uint32 index buffer. */
    uint32_t vertex_count;
    uint32_t index_count;
    render_submesh_t *submeshes;    /**< Heap-allocated submesh array. */
    uint32_t          submesh_count;
    float    bsphere_radius;
    float    aabb_min[3];
    float    aabb_max[3];
    void (*glDrawElements)(...);    /**< Resolved at creation time. */
} static_mesh_t;
```

**Creation descriptor** (`static_mesh_create_info_t`):
- `positions` (required), `normals`, `tangents`, `uv0`, `uv1`, `colors` (all optional)
- `indices` (required)
- `vertex_count`, `index_count`
- Optional `submeshes`/`submesh_count` (defaults to single submesh if NULL)

**API:**
- `static_mesh_create(loader, info, out)` -- from raw arrays
- `static_mesh_create_from_fvma(loader, fvma_data, fvma_size, out)` -- from FVMA binary
- `static_mesh_create_box(loader, w, h, d, out)` -- box primitive
- `static_mesh_create_sphere(loader, radius, slices, rings, out)` -- UV sphere
- `static_mesh_create_capsule(loader, radius, half_height, slices, cap_rings, out)` -- capsule
- `static_mesh_create_plane(loader, half_w, half_d, out)` -- XZ plane at Y=0
- `static_mesh_destroy(mesh)` -- releases all GPU resources + submesh array
- `static_mesh_bind(mesh)` / `static_mesh_unbind()` -- VAO bind/unbind
- `static_mesh_draw_submesh(mesh, submesh_index)` -- `glDrawElements`

Missing optional attributes bind a 1-element VBO with defaults
(tangent=(1,0,0,1), uv=(0,0), color=(1,1,1,1)) so shaders don't need
permutations for attribute presence.

**VAO Attribute Layout (all static meshes):**

| Location | Name | Type | Notes |
|----------|------|------|-------|
| 0 | `in_position` | vec3 | Always present |
| 1 | `in_normal` | vec3 | Always present |
| 2 | `in_tangent` | vec4 | Optional (flag in FVMA header) |
| 3 | `in_uv0` | vec2 | Optional |
| 4 | `in_uv1` | vec2 | Optional (lightmap) |
| 5 | `in_color` | vec4 | Optional |

### 5.2 Skeletal Mesh (`skeletal_mesh_t`)

Extends `static_mesh_t` with bone weight and index VBOs plus inverse-bind matrices.

```c
typedef struct skeletal_mesh {
    static_mesh_t base;              /**< Inherited static mesh fields. */
    vbo_t    vbo_bone_weights;       /**< vec4 bone weights (location 6). */
    vbo_t    vbo_bone_indices;       /**< uvec4 bone indices (location 7). */
    uint32_t bone_count;             /**< Number of bones in bind pose. */
    float   *inv_bind_matrices;      /**< bone_count x mat4, row-major (heap). */
} skeletal_mesh_t;
```

**Additional VAO attributes:**

| Location | Name | Type |
|----------|------|------|
| 6 | `in_bone_weights` | vec4 |
| 7 | `in_bone_indices` | ivec4 |

Maximum bone count defaults to 2048 (SSBO path), configurable at compile time
via `SKELETAL_MESH_DEFAULT_MAX_BONES`. No hard cap enforced at runtime.

**API:**
- `skeletal_mesh_create(loader, info, out)` -- from raw arrays (includes bone data)
- `skeletal_mesh_create_from_fvma(loader, fvma_data, fvma_size, out)` -- requires `MESH_VAO_FLAG_BONES`
- `skeletal_mesh_destroy(mesh)` -- frees bone VBOs, inv_bind_matrices, then base mesh
- `skeletal_mesh_bind(mesh)` / `skeletal_mesh_unbind()` / `skeletal_mesh_draw_submesh(mesh, idx)`

### 5.3 Mesh Handle (`mesh_handle_t`)

Opaque handle for referencing meshes in a registry. Uses index + generation
counter to detect stale references.

```c
typedef struct mesh_handle {
    uint32_t index;
    uint16_t generation;
} mesh_handle_t;

typedef enum mesh_type {
    MESH_TYPE_NONE     = 0,
    MESH_TYPE_STATIC   = 1,
    MESH_TYPE_SKELETAL = 2
} mesh_type_t;
```

### 5.4 Mesh Registry (`mesh_registry_t`)

Central mesh store mapping opaque handles to loaded mesh data. Holds both
static and skeletal meshes in a single flat array of union slots. Capacity is
configurable at init time. Internal arrays are allocated with a single malloc.

```c
typedef struct mesh_registry {
    mesh_type_t *types;
    union mesh_registry_slot {
        static_mesh_t   stat;
        skeletal_mesh_t skel;
    } *meshes;
    uint16_t *generations;
    uint32_t *freelist;
    uint32_t freelist_count;
    uint32_t count;
    uint32_t capacity;
    const gl_loader_t *loader;
} mesh_registry_t;
```

**API:**
- `mesh_registry_init(reg, capacity, loader)` -- allocate slots
- `mesh_registry_destroy(reg)` -- destroy all live meshes and free arrays
- `mesh_registry_insert_static(reg, info, out_handle)` -- create + store static mesh
- `mesh_registry_insert_skeletal(reg, info, out_handle)` -- create + store skeletal mesh
- `mesh_registry_remove(reg, handle)` -- destroy mesh, bump generation, return to freelist
- `mesh_registry_is_valid(reg, handle)` -- check handle validity
- `mesh_registry_type(reg, handle)` -- get mesh type
- `mesh_registry_get_static(reg, handle)` / `mesh_registry_get_skeletal(reg, handle)` -- lookup
- `mesh_registry_count(reg)` / `mesh_registry_capacity(reg)` -- stats

---

## 6. FVMA Binary Format

The FVMA (Ferrum Vertex Mesh Archive) format is a compact binary representation
for mesh serialization. Defined in `mesh_vao_format.h`.

### 6.1 Wire Format (little-endian)

```
Offset  Size  Field
  0       4   magic 'FVMA' (0x414D5646)
  4       4   version (1)
  8       4   vertex_count
 12       4   index_count
 16       4   flags (bitmask)
 20       4   polygroup_count
 24      ...  attribute data (conditional on flags)
```

### 6.2 Attribute Flags

| Flag | Bit | Attribute |
|------|-----|-----------|
| `MESH_VAO_FLAG_NORMALS` | 0 | vec3 normals |
| `MESH_VAO_FLAG_TANGENTS` | 1 | vec4 tangents |
| `MESH_VAO_FLAG_UV0` | 2 | vec2 UV set 0 |
| `MESH_VAO_FLAG_UV1` | 3 | vec2 UV set 1 |
| `MESH_VAO_FLAG_COLORS` | 4 | vec4 vertex colors |
| `MESH_VAO_FLAG_BONES` | 5 | Bone weights + indices + inv-bind matrices |

### 6.3 API

- `mesh_vao_serialized_size(slot, flags)` -- compute serialized byte count
- `mesh_vao_serialize(slot, flags, buf, buf_size)` -- serialize to buffer
- `mesh_vao_deserialize(buf, buf_size, out)` -- deserialize into `mesh_slot_t`

Serialization is stateless and thread-safe for distinct buffers.

---

## 7. Phase 2 — Rendering Pipeline Configuration

### 7.1 Render Pass Architecture (9-pass)

The frame is organized into 9 ordered passes. Each pass has a name, begin/submit/end
callbacks, user data, framebuffer ID, draw list, and pass type.

```
+--------------+
| Shadow Pass  |  <- Per-light shadow map generation (PSSM for directional)
+--------------+
| Depth Pre    |  <- Optional depth pre-pass for early-Z
+--------------+
| Caster Pass  |  <- Precomputed shadow maps for immobile lights
+--------------+
| Light Cull   |  <- Tiled light assignment (compute or CPU fallback)
+--------------+
| Forward Pass |  <- Main shading: geometry + lighting + materials
+--------------+
| Skybox Pass  |  <- Drawn at max depth after forward
+--------------+
| Debug Pass   |  <- Debug lines, gizmos, wireframes
+--------------+
| Post Pass    |  <- Tone mapping, gamma, FXAA
+--------------+
| UI Pass      |  <- 2D overlay
+--------------+
```

Pass types are defined in `render_pass_type.h` as an enum where the value
doubles as the array index:

```c
typedef enum render_pass_type {
    RENDER_PASS_SHADOW     = 0,
    RENDER_PASS_DEPTH_PRE  = 1,
    RENDER_PASS_CASTER     = 2,
    RENDER_PASS_LIGHT_CULL = 3,
    RENDER_PASS_FORWARD    = 4,
    RENDER_PASS_SKYBOX     = 5,
    RENDER_PASS_DEBUG      = 6,
    RENDER_PASS_POST       = 7,
    RENDER_PASS_UI         = 8,
    RENDER_PASS_TYPE_COUNT = 9
} render_pass_type_t;
```

`render_pass_type_name(type)` returns a static human-readable string.

### 7.2 Render Pass Descriptor (`render_pass_t`)

```c
typedef struct render_pass {
    const char *name;
    void (*begin)(void *user_data);
    void (*submit)(void *user_data);
    void (*end)(void *user_data);
    void *user_data;
    render_pass_type_t pass_type;
    draw_list_t *draw_list;        /**< Per-pass draw list (may be NULL). */
    uint32_t framebuffer;          /**< FBO id (0 = default). */
} render_pass_t;
```

### 7.3 Render Pipeline (`render_pipeline_t`)

Two usage modes:
1. **Full** -- `render_pipeline_init(pipeline, draw_list_capacity)` allocates
   9 passes with per-pass draw lists in a single malloc.
2. **Legacy** -- `render_pipeline_default(pipeline, storage, skybox, forward, post)`
   with external storage for 3 passes.

```c
typedef struct render_pipeline {
    render_pass_t *passes;
    size_t pass_count;
    void (*glBindFramebuffer)(uint32_t target, uint32_t framebuffer);
    int owns_storage;
} render_pipeline_t;
```

**API:**
- `render_pipeline_init(pipeline, draw_list_capacity)` -- allocate 9-pass pipeline
- `render_pipeline_destroy(pipeline)` -- free owned resources
- `render_pipeline_get_pass(pipeline, type)` -- get mutable pass by type
- `render_pipeline_clear_draw_lists(pipeline)` -- reset all draw lists per frame
- `render_pipeline_execute(pipeline)` -- iterate passes calling begin/submit/end
- `render_pipeline_bind_resources(pipeline, pass_index, view_set)` -- bind resource views
- `render_pipeline_unbind_resources(pipeline, pass_index)` -- unbind

### 7.4 Render Pipeline Graph (`render_pipeline_graph_t`)

Dependency-graph representation for render passes with flags (e.g.,
`RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS`). Supports conditional execution
of depth pre-pass nodes.

### 7.5 Resource Views (`render_resource_view_set_t`)

Render passes declare their resource dependencies via view sets describing
attachments (color/depth with format and dimensions) and transient resources
(vertex/uniform buffers).

### 7.6 Draw Lists and Sorting

Each pass operates on a **draw list**: a flat array of draw commands
sorted by 64-bit sort keys to minimize GPU state changes.

```c
typedef struct draw_sort_key {
    uint64_t key;  /**< [shader(16) | material(16) | mesh(16) | depth(16)] */
} draw_sort_key_t;

typedef struct draw_command {
    draw_sort_key_t  sort_key;
    mesh_handle_t    mesh;
    uint32_t         submesh_index;
    uint32_t         instance_offset;
    uint32_t         instance_count;
} draw_command_t;

typedef struct draw_list {
    draw_command_t *commands;
    uint32_t        count;
    uint32_t        capacity;
} draw_list_t;
```

**Sort key bit layout (MSB to LSB):**
- `[63..48]` shader (16 bits) -- most expensive state change
- `[47..32]` material (16 bits) -- texture binds
- `[31..16]` mesh (16 bits) -- VAO bind
- `[15.. 0]` depth (16 bits) -- front-to-back (opaque) or back-to-front

For transparent passes, invert the depth field (`0xFFFF - depth`) before
building the key to achieve back-to-front ordering.

**Sort algorithm:** 8-pass LSB radix sort on the 64-bit key (stable, O(n)).
A scratch buffer is allocated alongside the command array by `draw_list_init`.

**API:**
- `draw_list_init(list, capacity)` / `draw_list_destroy(list)`
- `draw_list_push(list, cmd)` -- append command
- `draw_list_clear(list)` -- reset count (no free)
- `draw_list_sort(list)` -- radix sort by key

**Sort key construction:**
```c
draw_sort_key_t draw_sort_key_build(uint16_t shader, uint16_t material,
                                     uint16_t mesh, uint16_t depth);
```

Plus extraction helpers: `draw_sort_key_shader()`, `draw_sort_key_material()`,
`draw_sort_key_mesh()`, `draw_sort_key_depth()`.

### 7.7 Batching

**Static batching:** At load time, merge meshes sharing the same material
and transform-invariant (world-space) into a single VBO+IBO. Produces one
draw call per material.

**Dynamic batching:** At runtime, adjacent draw commands with the same
material and mesh can be promoted to instanced draws. Per-instance data
(model matrix, entity ID) is uploaded to a persistent-mapped UBO/SSBO.

---

## 8. UBO System

### 8.1 Frame Params UBO (`frame_params_ubo_t`, binding 1)

Per-frame global uniform block uploaded once before all passes.

```c
typedef struct frame_params {
    float view[16];          /**< View matrix (column-major). */
    float proj[16];          /**< Projection matrix (column-major). */
    float view_proj[16];     /**< View x Projection (column-major). */
    float camera_pos[4];     /**< Camera world position (w unused, pad). */
    float time;              /**< Elapsed time in seconds. */
    float _pad[3];           /**< Padding to 16-byte alignment. */
} frame_params_t;
```

Layout follows std140 rules. Size is a multiple of 16 bytes.

**API:** `frame_params_ubo_init(ubo, loader, binding)`,
`frame_params_ubo_upload(ubo, params)`,
`frame_params_ubo_bind(ubo)`,
`frame_params_ubo_destroy(ubo)`.

### 8.2 Instance Data UBO (`instance_data_ubo_t`, binding 2)

Per-instance model matrix array. Capacity is configurable at init time (no
compile-time limit).

```c
typedef struct instance_data {
    float    model[16];               /**< Model matrix (column-major). */
    float    model_inv_transpose[16]; /**< Inverse-transpose for normals. */
    uint32_t entity_id;               /**< Entity ID for picking/debug. */
    uint32_t _pad[3];                 /**< Padding to 16-byte alignment. */
} instance_data_t;
```

Each entry is 144 bytes (2 x mat4 + uint + 3 x uint pad), padded for std140
array stride.

**API:** `instance_data_ubo_init(ubo, loader, binding, capacity)`,
`instance_data_ubo_upload(ubo, data, count)`,
`instance_data_ubo_bind(ubo)`,
`instance_data_ubo_destroy(ubo)`.

---

## 9. Bone Palette System

### 9.1 Bone Palette Buffer (`bone_palette_buffer_t`)

Wraps a GL buffer for uploading bone matrices to the GPU. Supports three
backend types with automatic fallback:

| Type | Constant | Limit |
|------|----------|-------|
| SSBO | `BONE_PALETTE_BUFFER_SSBO` | No practical limit |
| UBO | `BONE_PALETTE_BUFFER_UBO` | ~128 bones (UBO size limit) |
| TBO | `BONE_PALETTE_BUFFER_TBO` | Texture buffer (uses texture unit) |

Stores all required GL function pointers (11 total) for buffer management,
texture buffer binding, and active texture selection.

**API:**
- `bone_palette_buffer_init(palette, loader, max_bones, binding_point, supports_ssbo, supports_tbo)`
- `bone_palette_buffer_update(palette, data, size)`
- `bone_palette_buffer_bind(palette)`
- `bone_palette_buffer_destroy(palette)`
- `bone_palette_buffer_type(palette)` / `bone_palette_buffer_handle(palette)`

### 9.2 Skinning Shader (`skinning_shader_t`)

Wraps a shader program specialized for GPU skinning. Stores the bone palette
uniform location.

Attribute semantics (note: these are the skinning shader's own attribute layout,
distinct from the canonical static/skeletal mesh layout):

| Constant | Location | Attribute |
|----------|----------|-----------|
| `SKINNING_ATTRIBUTE_POSITION` | 0 | position |
| `SKINNING_ATTRIBUTE_NORMAL` | 1 | normal |
| `SKINNING_ATTRIBUTE_TEXCOORD` | 2 | texcoord |
| `SKINNING_ATTRIBUTE_BONE_WEIGHTS` | 3 | bone weights |
| `SKINNING_ATTRIBUTE_BONE_INDICES` | 4 | bone indices |

**API:** `skinning_shader_create(shader, loader, log, log_cap)`,
`skinning_shader_create_from_source(shader, loader, vert, frag, log, log_cap)`,
`skinning_shader_bind(shader, palette)`,
`skinning_shader_destroy(shader)`.

### 9.3 Skinning Pipeline (`skinning_pipeline_t`)

ECS-integrated pipeline that evaluates skeletons and manages palette uploads.

Components:
- `skinning_skeleton_t` -- skeleton component with joint_count, local_matrices, parent_indices
- `skinning_skin_t` -- skin component referencing a skeleton entity

**Pipeline API:**
- `skinning_pipeline_init(pipeline, skeleton_capacity, max_joints)`
- `skinning_pipeline_update(pipeline, job_system, skeletons, skins)` -- evaluate + build palette mapping
- `skinning_pipeline_palette_index(pipeline, entity, out_index)` -- query palette for entity
- `skinning_pipeline_upload_palette(pipeline, palette, palette_index)` -- upload to GPU
- `skinning_pipeline_build_draw_list(pipeline, skins, out_list, storage, capacity)` -- ordered by palette index

---

## 10. Phase 3 — Scene Graph

### 10.1 Left-Child Right-Sibling Tree

The scene graph is a flat-array tree using LCRS (left-child, right-sibling)
representation over the existing entity set.

```c
typedef struct scene_node {
    uint32_t parent;          /**< Entity index (SCENE_NODE_NONE = root). */
    uint32_t first_child;     /**< Entity index (SCENE_NODE_NONE = leaf). */
    uint32_t next_sibling;    /**< Entity index (SCENE_NODE_NONE = last). */
    uint32_t flags;           /**< SCENE_NODE_DIRTY_LOCAL, _DIRTY_WORLD, _STATIC. */
    mat4_t   local_transform;
    mat4_t   world_transform;
} scene_node_t;

typedef struct scene_graph {
    scene_node_t *nodes;
    uint32_t      capacity;
    uint32_t     *dirty_list;
    uint32_t      dirty_count;
} scene_graph_t;
```

**Flags:**
- `SCENE_NODE_DIRTY_LOCAL (1<<0)` -- local transform modified
- `SCENE_NODE_DIRTY_WORLD (1<<1)` -- world transform stale
- `SCENE_NODE_STATIC (1<<2)` -- baked world transform (skip BFS update)

**Update strategy:**
1. Mark modified entities `DIRTY_LOCAL` via `scene_graph_mark_dirty()`
2. BFS from dirty roots, propagating `world = parent.world x local`
3. Children inherit dirty flag (cascade)
4. Static nodes skip update unless explicitly invalidated

**API:**
- `scene_graph_init(graph, capacity)` / `scene_graph_destroy(graph)`
- `scene_graph_attach(graph, entity_idx, parent_idx)` -- supports re-parenting
- `scene_graph_detach(graph, entity_idx)` -- children become roots
- `scene_graph_mark_dirty(graph, entity_idx)`
- `scene_graph_update(graph)` -- BFS world transform propagation

---

## 11. Phase 4 — Material System

### 11.1 Material Definition

A material binds a shader program to a set of parameters (uniforms + textures).

```c
typedef enum material_texture_slot {
    MATERIAL_TEX_ALBEDO    = 0,
    MATERIAL_TEX_NORMAL    = 1,
    MATERIAL_TEX_ROUGHNESS = 2,
    MATERIAL_TEX_METALLIC  = 3,
    MATERIAL_TEX_EMISSIVE  = 4,
    MATERIAL_TEX_LIGHTMAP  = 5,
    MATERIAL_TEX_COUNT     = 6
} material_texture_slot_t;

typedef struct render_material {
    uint32_t         shader_handle;
    uint32_t         textures[MATERIAL_TEX_COUNT];
    float            base_color[4];
    float            roughness;
    float            metallic;
    float            emissive_strength;
    float            alpha_cutoff;
    material_flags_t flags;
    render_queue_t   queue;
} render_material_t;
```

### 11.2 Shader Permutations

Compile-time permutations via preprocessor defines:

| Define | Purpose |
|--------|---------|
| `HAS_NORMAL_MAP` | Enable tangent-space normal mapping |
| `HAS_LIGHTMAP` | Sample lightmap from UV1 |
| `HAS_VERTEX_COLOR` | Multiply base_color by vertex color |
| `HAS_SKINNING` | Include bone palette transform |
| `ALPHA_CLIP` | Discard fragments below alpha_cutoff |
| `NUM_DIR_LIGHTS` | Number of directional lights (0-4) |
| `NUM_POINT_LIGHTS_TILE` | Max point lights per tile |

---

## 12. Debug Visualization

### 12.1 Debug Lines (`fr_debug_lines_t`)

Fixed-capacity ring buffer for transient debug line segments in world space.
Each line has an expiration time; expired lines are pruned during collection.

```c
typedef struct fr_debug_line {
    vec3_t a, b;
    double expire_time_s;
} fr_debug_line_t;

typedef struct fr_debug_lines {
    fr_debug_line_t *lines;
    size_t capacity, count, head;
} fr_debug_lines_t;
```

**API:** `fr_debug_lines_init()`, `fr_debug_lines_add(store, a, b, now, ttl)`,
`fr_debug_lines_collect_vertices(store, now, out, cap, out_count)`.

### 12.2 Correction Lines

`fr_debug_correction_lines_cube()` generates 8 line segments (16 vertices)
connecting corners of an estimated cube to an authoritative cube, used for
visualizing client/server prediction corrections.

---

## 13. glTF 2.0 Loader

### 13.1 Overview

`gltf_loader.h` provides glTF 2.0 / GLB import using cgltf (vendored in
`src/renderer/gltf/cgltf_impl.c`).

Supports:
- GLB binary container
- Multiple meshes per file
- Skinned meshes (JOINTS_0 / WEIGHTS_0)
- Per-primitive submeshes
- Positions, normals, tangents, UV0, UV1, colors

### 13.2 API

```c
gltf_status_t gltf_scene_load(const char *path, gltf_scene_t **out);
void gltf_scene_destroy(gltf_scene_t *scene);
uint32_t gltf_scene_mesh_count(const gltf_scene_t *scene);
gltf_status_t gltf_scene_mesh_info(scene, index, info);
gltf_status_t gltf_scene_create_static_mesh(scene, index, loader, out);
gltf_status_t gltf_scene_create_skeletal_mesh(scene, index, loader, out);
uint32_t gltf_scene_joint_count(scene);
gltf_status_t gltf_scene_compute_bind_pose(scene, out_mats, capacity);
```

Mesh info includes name, vertex/index/submesh counts, skinned flag, and
glTF mesh array index.

---

## 14. Video Capture System

### 14.1 Overview

`fr_video_capture_t` provides GPU-buffered video capture with async PBO
readback and a dedicated encoder pthread. Captures rendered frames to a
video file without stalling the render loop.

### 14.2 Architecture

- **PBO ring**: Async GPU-to-CPU pixel transfer via persistent-mapped PBOs
- **CPU frame ring**: Lock-free ring buffer between render and encoder threads
- **Encoder thread**: Dedicated pthread that pipes frames to ffmpeg or writes raw RGBA

### 14.3 API

```c
fr_video_capture_t *fr_video_capture_create(const fr_video_capture_desc_t *desc);
void fr_video_capture_submit_frame(fr_video_capture_t *cap);
void fr_video_capture_destroy(fr_video_capture_t *cap);
uint64_t fr_video_capture_frames_written(const fr_video_capture_t *cap);
```

`submit_frame()` harvests completed PBO fences, copies to CPU ring, then
initiates a new async readback. Never blocks on GPU. If the encoder falls
behind, the oldest unread frame is silently dropped.

---

## 15. Clay UI Backend

### 15.1 Overview

`clay_backend_t` renders Clay UI commands (RECTANGLE, TEXT, BORDER, SCISSOR,
IMAGE, CUSTOM) via the renderer module. Uses `shader_program_t`, `vao_t`,
`vbo_t` for all GL resource management.

### 15.2 Architecture

The backend compiles a 2D UI shader (position + color + UV) and uses a dynamic
VBO for per-frame vertex uploads. Vertex layout:

| Offset | Size | Attribute |
|--------|------|-----------|
| 0 | 8 bytes | position (vec2) |
| 8 | 16 bytes | color (vec4) |
| 24 | 8 bytes | UV (vec2) |

Total stride: 32 bytes.

Fragment shader supports two modes:
- Solid color (`u_use_texture = 0`)
- Single-channel texture (`u_use_texture != 0`) -- font atlas alpha from red channel

Supports headless mode: when `get_proc_address` is NULL, init succeeds without
creating any GL resources.

### 15.3 API

- `clay_backend_init(backend, config)` -- compile shaders, create VAO/VBO, init font atlas
- `clay_backend_render(backend, cmds)` -- draw Clay render commands
- `clay_backend_resize(backend, w, h)` -- update window dimensions
- `clay_backend_destroy(backend)` -- free all resources

---

## 16. Scene Editor Viewport Renderer

### 16.1 Overview

The scene editor viewport (`scene_viewport_render.h`) renders 3D entity
visualizations into an off-screen FBO. The resulting color texture is
displayed in the Clay UI viewport panel as an image element.

### 16.2 Architecture

**Renderer state** (`viewport_render_state_t`) contains:
- FBO (color texture + depth renderbuffer, RGBA8 + DEPTH24_STENCIL8)
- 9-pass `render_pipeline_t` (256 draw commands per pass)
- Blinn-Phong shader (entity rendering) with `shader_uniform_cache_t`
- Grid shader (unlit colored lines) with `shader_uniform_cache_t`
- Grid geometry (`vao_t` + `vbo_t`, interleaved position+color, 41x2 lines)
- `mesh_registry_t` (capacity 256) for dynamic/loaded meshes
- Entity mesh cache (maps entity_id to mesh_handle, capacity 4096)
- Orbit camera (`editor_camera_t`)
- GL function pointers for FBO operations (20 function pointers)

### 16.3 Shading

**Entity shader:** Blinn-Phong with:
- Uniforms: `u_model`, `u_view`, `u_projection`, `u_color`, `u_light_dir`, `u_eye_pos`
- Fixed light direction (0.577, 0.577, 0.577)
- Ambient (15%) + diffuse + specular (30%, power 32)
- Per-entity-type colors, selection highlight override (orange)

**Grid shader:** Unlit vertex-colored lines:
- Uniforms: `u_vp` (view-projection)
- Origin axis lines brighter (0.5 gray) than other lines (0.2 gray)
- Grid: -20 to +20 in XZ, step 1

### 16.4 Entity Mesh Resolution

| Entity Type | Mesh Source |
|-------------|-------------|
| BOX | `static_mesh_create_box(1, 1, 1)` (lazy init) |
| SPHERE | `static_mesh_create_sphere(0.5, 16, 12)` (lazy init) |
| CAPSULE | `static_mesh_create_capsule(0.3, 0.5, 16, 4)` (lazy init) |
| HALFSPACE | `static_mesh_create_plane(10, 10)` (lazy init) |
| MESH | Loaded FVMA geometry from entity mesh cache |
| MARKER | Shared sphere primitive (small, solid) |
| Unknown | Box fallback |

Primitive meshes are lazily created on first use (file-static cache) and
destroyed via `viewport_render_destroy_primitives()`.

### 16.5 FVMA Mesh Loading

`viewport_render_load_entity_mesh(state, entity_id, fvma_data, fvma_size)`:
1. Deserializes FVMA binary into `mesh_slot_t` via `mesh_vao_deserialize()`
2. Builds `static_mesh_create_info_t` from slot data
3. Inserts into viewport's `mesh_registry` via `mesh_registry_insert_static()`
4. Replaces any previously loaded mesh for that entity
5. Frees the temporary `mesh_slot_t` (GPU data is now in the registry)

### 16.6 Draw Flow

`viewport_render_draw_scene(editor)`:
1. Resize FBO if viewport panel dimensions changed
2. Compute camera view/projection matrices
3. Bind FBO, set viewport, clear (dark gray background)
4. Enable depth test
5. Draw grid (bind grid shader, set VP uniform, draw lines)
6. Draw entities (bind entity shader, iterate entities, resolve mesh, set model/color, draw all submeshes)
7. Disable depth test, unbind FBO

---

## 17. Vertex Attribute Canonical Locations

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

## 18. UBO Binding Points

| Binding | Name | Contents |
|---------|------|----------|
| 1 | FrameParams | view, proj, VP, camera_pos, time |
| 2 | InstanceData | Per-instance model matrices (configurable capacity) |
| 3 | BonePalette | Bone matrices (SSBO/UBO/TBO, configurable max) |

---

## 19. Phase 5 — Animation System

### 19.1 Architecture

The animation system extends the existing skinning pipeline into a
full constraint-driven system:

```
+------------------------------------------------------+
|                  Animation Pipeline                   |
|                                                      |
|  +-------------+  +--------------+  +-------------+  |
|  |  Clip Eval   |->| Blend Tree   |->| IK / XPBD   | |
|  |  (sampling)  |  |  (layered)   |  | (constraint)| |
|  +-------------+  +--------------+  +-------------+  |
|         |                                    |        |
|         v                                    v        |
|  +-------------+                    +-------------+   |
|  | Bind Pose   |                    | Bone Palette|   |
|  | Fallback    |                    | Upload      |   |
|  +-------------+                    +-------------+   |
+------------------------------------------------------+
```

### 19.2 Clip Evaluation

Animation clips are arrays of `(time, value)` keyframes per bone per channel
(translation, rotation, scale). Evaluation samples at the current time with
linear interpolation (lerp for vec3, slerp for quat).

### 19.3 Blend Tree

Layered blending with per-bone masks:
- **Additive layers:** Walk + lean (additive on upper body)
- **Override layers:** Aim override on spine chain
- **Cross-fade:** Smooth transition between clips with configurable duration

### 19.4 Constraint Solver (XPBD Extension)

After blend tree evaluation, bone transforms are post-processed by
constraints using the existing physics XPBD solver:
- **Look-at / aim constraints**
- **Hinge / ball-socket limits** (reuse `phys_joint_t`)
- **IK chains:** Iterative FABRIK or CCD IK, finalized by XPBD
- **Spring / damper:** Secondary motion (hair, cloth, tails)

### 19.5 Ragdoll Fallback

Per-bone ragdoll activation with configurable transition blending.
Individual bones can ragdoll independently.

### 19.6 Dynamic CCD for Fast Bones

Weapon sweeps and fast-moving extremities use the existing dynamic CCD
system. When bone displacement > 0.5 x bone_collider_radius, CCD sweep
test is applied. Contacts generate hit events for gameplay.

---

## 20. Phase 6 — Lighting and Shadows

### 20.1 Light Types

```c
typedef enum light_type {
    LIGHT_TYPE_DIRECTIONAL = 0,
    LIGHT_TYPE_POINT       = 1,
    LIGHT_TYPE_SPOT        = 2
} light_type_t;

typedef struct light_component {
    light_type_t type;
    float        color[3];
    float        intensity;
    float        range;
    float        inner_cone;
    float        outer_cone;
    uint8_t      cast_shadows;
    uint8_t      is_static;
} light_component_t;
```

### 20.2 Tiled Light Culling

Screen-space tiles (16x16 pixels) with per-tile light index lists in SSBO.

### 20.3 Parallel-Split Shadow Maps (PSSM)

4 cascade splits for directional lights. Static light optimization caches
shadow maps at the caster stage. Point lights use cube maps (6-face, 512x512),
spot lights use 2D shadow maps (1024x1024).

### 20.4 SH Probes (Spherical Harmonics)

Baked L2 spherical harmonics probes on a 3D grid for ambient lighting
(9 coefficients x 3 channels = 27 floats per probe).

---

## 21. File Organization

```
include/ferrum/renderer/
+-- mesh/
|   +-- mesh_handle.h          # mesh_handle_t, mesh_type_t
|   +-- static_mesh.h          # static_mesh_t, create/destroy/draw/primitives
|   +-- skeletal_mesh.h        # skeletal_mesh_t, extends static_mesh
|   +-- mesh_registry.h        # mesh_registry_t, handle-based lookup
+-- draw/
|   +-- draw_list.h            # draw_command_t, draw_list_t
|   +-- draw_sort.h            # draw_sort_key_t, key construction
+-- scene/
|   +-- scene_graph.h          # scene_graph_t, LCRS tree
|   +-- scene_node.h           # scene_node_t, flags
+-- ubo/
|   +-- frame_params_ubo.h     # frame_params_t, frame_params_ubo_t
|   +-- instance_data_ubo.h    # instance_data_t, instance_data_ubo_t
+-- skinning/
|   +-- components.h           # skinning_skeleton_t
|   +-- skin.h                 # skinning_skin_t
|   +-- pipeline.h             # skinning_pipeline_t
+-- gltf/
|   +-- gltf_loader.h          # gltf_scene_t, mesh/skeleton import
+-- gl_loader.h                # gl_loader_t, validation
+-- gl_constants.h             # GL constant defines (no GL header needed)
+-- vao.h                      # vao_t wrapper
+-- vbo.h                      # vbo_t wrapper
+-- vao_attribute.h            # vao_attribute_t descriptor
+-- shader_program.h           # shader_program_t
+-- shader_uniforms.h          # shader_uniform_cache_t
+-- skinning.h                 # aggregator (components + skin + pipeline)
+-- skinning_shader.h          # skinning_shader_t
+-- bone_palette.h             # bone_palette_buffer_t
+-- render_pass_type.h         # render_pass_type_t enum (9 passes)
+-- render_pipeline.h          # render_pipeline_t
+-- render_pipeline_graph.h    # render_pipeline_graph_t
+-- render_resource_views.h    # resource view types
+-- debug_lines.h              # fr_debug_lines_t
+-- debug_correction_lines.h   # correction line generation
+-- video_capture.h            # fr_video_capture_t

src/renderer/
+-- mesh/
|   +-- static_mesh_create.c
|   +-- static_mesh_destroy.c
|   +-- static_mesh_draw.c
|   +-- static_mesh_fvma.c
|   +-- static_mesh_primitives.c
|   +-- skeletal_mesh_create.c
|   +-- skeletal_mesh_destroy.c
|   +-- skeletal_mesh_draw.c
|   +-- skeletal_mesh_fvma.c
|   +-- mesh_registry_init.c
|   +-- mesh_registry_insert.c
|   +-- mesh_registry_remove.c
|   +-- mesh_registry_query.c
|   +-- mesh_registry_count.c
+-- draw/
|   +-- draw_list_init.c
|   +-- draw_list_ops.c
|   +-- draw_list_sort.c        # 8-pass LSB radix sort
+-- scene/
|   +-- scene_graph_init.c
|   +-- scene_graph_update.c
|   +-- scene_graph_attach.c
|   +-- scene_graph_detach.c
+-- ubo/
|   +-- frame_params_ubo.c
|   +-- instance_data_ubo.c
+-- skinning/
|   +-- pipeline_init.c
|   +-- pipeline_update.c
|   +-- pipeline_upload.c
|   +-- pipeline_query.c
|   +-- pipeline_draw_list.c
|   +-- pipeline_internal.h     # private helpers
+-- gltf/
|   +-- cgltf_impl.c            # vendored cgltf implementation
|   +-- gltf_scene.c            # scene load/destroy/query
|   +-- gltf_mesh_create.c      # static + skeletal mesh creation
|   +-- gltf_bind_pose.c        # bind-pose matrix computation
+-- debug_lines/
|   +-- debug_lines.c
|   +-- correction_lines_cube.c
+-- video_capture/
|   +-- video_capture.c         # main lifecycle + PBO readback
|   +-- pbo_ring.c / pbo_ring.h # PBO ring buffer
|   +-- frame_ring.c / frame_ring.h # CPU frame ring buffer
|   +-- encode_thread.c / encode_thread.h # encoder pthread
+-- bone_palette_init.c
+-- bone_palette_update.c
+-- bone_palette_bind.c
+-- bone_palette_destroy.c
+-- bone_palette_handle.c
+-- shader_program_create.c
+-- shader_program_bind.c
+-- shader_program_destroy.c
+-- shader_program_handle.c
+-- shader_uniforms_init.c
+-- skinning_shader_create.c
+-- skinning_shader_bind.c
+-- skinning_shader_destroy.c
+-- skinning_shader_handle.c
+-- vao_create.c
+-- vao_destroy.c
+-- vao_handle.c
+-- vao_bind_attributes.c
+-- vbo_create.c
+-- vbo_destroy.c
+-- vbo_handle.c
+-- vbo_upload.c
+-- gl_loader_validate.c
+-- render_pipeline_init.c
+-- render_pipeline_execute.c
+-- render_pipeline_default.c
+-- render_pipeline_pass.c
+-- render_pipeline_resources.c
+-- render_pipeline_graph.c

include/ferrum/editor/
+-- ui/clay_backend.h           # clay_backend_t (UI rendering)
+-- mesh/mesh_vao_format.h      # FVMA binary format
+-- scene/scene_viewport_render.h # viewport_render_state_t

src/editor/
+-- ui/clay_backend.c           # lifecycle (init/destroy/resize)
+-- ui/clay_backend_render.c    # render Clay commands
+-- scene/scene_viewport_render.c # FBO/shader/mesh/pipeline init
+-- scene/scene_viewport_mesh.c # FVMA loading into mesh registry
+-- scene/scene_viewport_draw.c # 3D draw pass (grid + entities)
```

---

## 22. Integration Points

### 22.1 Physics <-> Animation
- Ragdoll bones are physics bodies with joint constraints
- XPBD solver shared between physics joints and bone constraints
- Dynamic CCD for fast bone sweeps reuses `ccd_dynamic.c` auto-activation

### 22.2 Network <-> Renderer
- Server sends mesh data via `NET_REPL_SCHEMA_MESH_DATA` (existing FVMA chunks)
- Static meshes deserialized into `static_mesh_t` on client receive
- Skeletal mesh data + bind pose sent as extended FVMA with bone flag

### 22.3 Editor <-> Renderer
- `edit_entity_t.materials[5]` maps to `render_material_t` texture slots
- Entity spawn messages include shape_type to mesh_type mapping
- Scene graph mirrors editor entity hierarchy
- Viewport renderer uses `mesh_registry_t` for entity mesh management
- FVMA binary data loaded into mesh registry via `viewport_render_load_entity_mesh()`
- Clay UI backend renders via renderer module wrappers

### 22.4 ECS <-> Scene Graph
- Entity pool indices are scene node indices (parallel arrays)
- Entity creation -> `scene_graph_attach(graph, entity_idx, parent_idx)`
- Entity deletion -> `scene_graph_detach(graph, entity_idx)` (reparents children)

### 22.5 ECS <-> Skinning Pipeline
- Skeleton and skin ECS components (`skinning_skeleton_t`, `skinning_skin_t`)
- Pipeline update driven by job system for parallel evaluation
- Palette indices queried per-entity for draw list ordering

---

## 23. Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Draw calls per frame | < 2000 | After batching |
| State changes per frame | < 500 | Shader + material + VAO |
| Shadow map renders | <= 8 | 4 PSSM cascades + 4 dynamic lights |
| Bone palette uploads | <= 64 | 64 skinned characters on screen |
| Light count | <= 1024 | Tiled culling handles density |
| Tile size | 16x16 px | Balance between culling and overhead |
| Frame budget (render) | < 4ms | At 1080p, 60Hz |

---

## 24. Dependencies

### Allowed
- OpenGL 3.3+ (core profile, function pointers loaded at runtime)
- SDL2 (windowing, input, GL context)
- Standard C library, POSIX
- cgltf (vendored in `src/renderer/gltf/cgltf_impl.c`)
- ffmpeg (external process, piped for video capture)
- Clay (UI layout library)

### Forbidden
- GLAD, GLEW, or other GL loaders (custom `gl_loader_t` pattern used)
- Third-party rendering libraries (no bgfx, no sokol)
- Third-party scene graph libraries
- Third-party animation libraries

### Future (whitelisted when needed)
- stb_image for texture loading
