# Scene Editor Specification

## 1. Overview

The **Scene Editor** is a unified graphical application that replaces the
three-process (server/client/controller) split with a single-window
SDL2 + OpenGL tool embedding the TUI, viewport, outliner, and inspector
in one process. It connects to the authoritative game server over the
existing edit protocol (TCP) and replication channel (UDP), so multiple
editor instances can view and edit the same world simultaneously.

```
┌─────────────────────────────────────────────────────────────────────┐
│                          SCENE EDITOR                               │
│                                                                     │
│  ┌─ Outliner ──────────┐  ┌─ 3D Viewport ──────────────────────┐  │
│  │ ▸ World             │  │                                     │  │
│  │   ├─ Terrain        │  │   (OpenGL scene render)             │  │
│  │   ├─ StaticGeo      │  │                                     │  │
│  │   │  ├─ pillar_01   │  │                                     │  │
│  │   │  └─ wall_sect   │  │                                     │  │
│  │   ├─ Characters     │  │                                     │  │
│  │   └─ Lights         │  │                                     │  │
│  └─────────────────────┘  └─────────────────────────────────────┘  │
│                            ┌─ Inspector ────────────────────────┐  │
│                            │ Entity: pillar_01                  │  │
│                            │ Position: (10, 0, 5)               │  │
│                            │ Rotation: (0, 0, 0, 1)             │  │
│                            │ Physics: static, mass=0            │  │
│                            │ Collider: mesh                     │  │
│                            └────────────────────────────────────┘  │
│  ┌─ TUI Panel (embedded terminal) ─────────────────────────────┐  │
│  │ > spawn box 2 2 2                                            │  │
│  │ [entity_042] box 2×2×2 at (10, 0, 5) mass=0 static          │  │
│  │ :                                                            │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### Design Principles

1. **Collaborative** — all edits push to the server; multiple editor
   instances are views of the same authoritative world.
2. **Procedural + manual** — every operation is a command (scriptable,
   AI-drivable), but hand-editing and painting tools give per-vertex /
   per-texel control.
3. **Tablet-native** — Wacom pressure, tilt, and eraser are first-class
   inputs for sculpting, painting, and weight painting.
4. **Scalable** — static geometry streams from disk; layers, groups, and
   objects can be hidden or swapped to disk independently.
5. **Full undo** — branching undo with rebaseable redo.

---

## 2. Architecture

### 2.1 Process Model

The editor is a single SDL2/OpenGL process. It embeds:
- The **3D viewport** (OpenGL rendering context)
- The **TUI**, **outliner**, **inspector**, **toolbar**, **timeline**,
  **2D paint panel**, and all other UI chrome — rendered using
  **Clay** (`extern/clay/`), a single-header C layout library that
  outputs render commands consumed by a thin OpenGL backend
  (see `scene_editor_design.md` §0.1.1 for integration details)

It connects to:
- **Game server** (UDP: replication snapshots; TCP: edit protocol)
- Other editor instances share the same server — all mutations are
  server-authoritative and broadcast to all connected editors.

```
Editor A ──TCP──┐              ┌──TCP── Editor B
Editor A ──UDP──┤  Game Server ├──UDP── Editor B
                └──────────────┘
```

### 2.2 Edit Protocol Extensions

The existing edit protocol (newline-delimited JSON over TCP) is extended
with collaborative features:

| Message | Direction | Purpose |
|---------|-----------|---------|
| `edit_lock` | editor→server | Request exclusive or freeze lock on target |
| `edit_unlock` | editor→server | Release lock |
| `edit_broadcast` | server→editors | Broadcast mutation to all connected editors |
| `undo_push` | editor→server | Push undo record to server undo stack |
| `undo_pop` | editor→server | Server-side undo (broadcasts inverse to all) |
| `anim_keyframe` | editor→server | Set/delete keyframe on a channel |
| `anim_event` | editor→server | Add/remove animation event or constraint swap |
| `anim_simulate` | editor→server | Play/pause/step/rewind/ff simulation |
| `anim_record` | editor→server | Start/stop simulation recording |
| `lock_notify` | server→editors | Broadcast lock state changes |

Each editor maintains a **local undo stack** for its own operations.
The server maintains a **global undo stack** that records all mutations
from all editors (for crash recovery and session replay).

### 2.3 Local Caching for Performance

High-frequency operations (sculpting, painting, terrain editing) cache
edits locally before flushing to the server in batches:

```
User input (60Hz+)
    │
    ▼
Local edit buffer (immediate visual feedback)
    │
    ▼ (throttled, e.g. 4Hz or on tool release)
Batch flush to server via edit protocol
    │
    ▼
Server applies + broadcasts to other editors
```

This eliminates round-trip latency for interactive tools while keeping
the server authoritative. Conflicts (two editors sculpting the same
region) are resolved by last-write-wins at the vertex/texel level.

### 2.4 Asset Streaming and Disk Swapping

Large scenes require streaming:

| Mechanism | Granularity | Description |
|-----------|-------------|-------------|
| **Hide** | Per-object/group/layer | GPU resources retained; skip draw call |
| **Swap to disk** | Per-object/group/layer | GPU + CPU memory freed; reload on demand |
| **LOD stream** | Per-static-mesh | Distance-based LOD; coarse hull in memory, detail on disk |
| **Terrain pages** | 256×256 vertex pages | Page-in/page-out based on camera distance |

The outliner shows hide/swap state with icons. Swapped objects render as
bounding-box wireframes until loaded.

### 2.5 Grid and Snap System

The editor provides a per-transform-type grid snapping system with
per-axis control. Snap settings are **local to each editor instance** —
they are not replicated to the server or other editors, because
different editors may want different snap configurations.

#### Snap Settings

Each transform type maintains independent snap parameters:

| Transform | Setting | Default |
|-----------|---------|---------|
| **Position** | Grid size (world units) + per-axis enable (X, Y, Z) | 1.0, all axes on |
| **Rotation** | Snap angle (degrees) + per-axis enable (X, Y, Z) | 15°, all axes on |
| **Scale** | Snap increment (factor) + per-axis enable (X, Y, Z) | 0.1, all axes on |

When snap is enabled for a transform type, values are quantized to the
nearest grid increment during interactive transforms (G/R/S). Per-axis
toggles allow snapping on some axes while leaving others continuous.

#### Pivot Point

Each entity has a **pivot point** that serves as the local origin for
transforms (rotate and scale center, grab anchor). The pivot offset is
stored as a server-side entity field (`pivot_offset` vec3 relative to
entity origin), so it persists across sessions and is visible to all
connected editors. Pivot manipulation is performed via edit commands:

- Move pivot: enter pivot mode and drag to reposition
- Snap pivot to grid: quantize pivot position to position grid
- Reset pivot to entity center (bounding box center)
- Move pivot to 3D cursor position

Grid dimension manipulation and per-axis snap toggles are controlled
via TUI commands (see UX spec §2.5 and §8.12).

---

## 3. Undo System

### 3.1 Branching Undo with Rebaseable Redo

The undo system goes beyond linear undo/redo:

```
Operation history:          A ─ B ─ C ─ D
                                        ▲ HEAD

Undo to B:                  A ─ B ─ C ─ D   (C, D on redo stack)
                                ▲ HEAD

New edit E:                 A ─ B ─ E        (attempt to rebase C, D)
                                    ▲ HEAD

Rebase result:
  - If C and D do not conflict with E → A ─ B ─ E ─ C' ─ D'
  - If C conflicts with E → A ─ B ─ E ─ D'  (C dropped, D rebased)
  - If both conflict → A ─ B ─ E  (C, D moved to orphan branch)
```

- `:undo tree` — opens a text-mode tree view of the undo history in the TUI, showing active branch, orphan branches, and conflict markers.

### 3.2 Conflict Detection

Two operations conflict when they modify overlapping state:

| Operation type | Conflict key |
|----------------|-------------|
| Transform | Entity ID |
| Vertex edit | (Mesh ID, vertex index range) |
| Texel paint | (Texture ID, UV region) |
| Component set | (Entity ID, component name) |
| Spawn/delete | Entity ID |
| Keyframe set/delete | (Entity ID, channel, frame) |
| Constraint swap | (Entity ID, bone index, frame) |
| Anim event | (Entity ID, event name, frame) |

Non-conflicting operations rebase cleanly. Conflicting operations are
dropped from the redo stack (but preserved in an orphan branch accessible
via `:undo tree` for manual recovery).

### 3.3 Undo Record Format

```c
typedef struct undo_record {
    uint32_t id;               /* Monotonic ID */
    uint32_t editor_id;        /* Which editor created this */
    uint64_t timestamp;        /* Server tick */
    uint32_t conflict_key_type;/* Entity, vertex range, UV region, etc. */
    uint64_t conflict_key;     /* Hash of the affected resource */
    uint32_t forward_size;     /* Byte size of forward (do) payload */
    uint32_t inverse_size;     /* Byte size of inverse (undo) payload */
    uint8_t  data[];           /* forward_payload | inverse_payload */
} undo_record_t;
```

---

## 4. Editor Modes

The editor supports multiple modes, each activating a different tool
palette and viewport interaction model:

| Mode | Purpose | Tools |
|------|---------|-------|
| **Object** | Scene placement, transforms | Move, rotate, scale, snap, align |
| **Mesh** | Static geometry editing | Vertex/edge/face select, extrude, bevel, subdivide, loop cut, UV editing (§5.9), collision mesh editing (§4.1) |
| **Sculpt** | Organic geometry shaping | Grab, smooth, flatten, inflate, crease, pinch, clay strips |
| **Paint** | Texture/vertex color painting | Brush, fill, gradient, clone stamp, blur, sharpen |
| **Weight** | Bone weight painting | Brush, gradient, flood fill, normalize, mirror |
| **Animation** | Rigging, keyframing, simulation | Place bone, set constraints, keyframe channels, timeline, physics sim, muscle tuning, event triggers |
| **Terrain** | Landscape editing | Raise, lower, smooth, flatten, stamp, erode |

Mode switching via toolbar buttons, keyboard shortcuts, or `:mode <name>` command.

Grid snap (§2.5) applies across all modes that involve transforms —
Object, Mesh, Animation, and Terrain modes all respect the current snap
settings when moving, rotating, or scaling entities, vertices, or bones.

### 4.1 Collision Mesh Editing

In Mesh mode, the editor can switch between editing the **visual mesh**
(render geometry) and the **collision mesh** (physics geometry). These
are often separate — a detailed 10K-triangle building render mesh might
use a simplified 200-triangle collision hull.

**Mesh target toggle:**
- `:mesh target render` — edit the visual/render mesh (default)
- `:mesh target collision` — edit the collision mesh
- `Shift+C` — toggle between render and collision mesh targets

**Viewport display:**
- When editing the collision mesh, the render mesh is shown as a
  semi-transparent ghost. The collision mesh is drawn as a wireframe
  overlay in a distinct color (green).
- When editing the render mesh, the collision mesh wireframe can be
  toggled as an overlay via `:mesh overlay collision on|off`.

**Collision mesh lifecycle:**
- If an entity has no collision mesh, `:mesh collision create` creates
  one by copying the render mesh. The user can then simplify it.
- `:mesh collision from render` — replaces the collision mesh with a
  copy of the current render mesh (useful after editing the visual mesh)
- `:mesh collision auto <target_tris>` — generates a simplified convex
  hull or decimated mesh with approximately `target_tris` triangles
- `:mesh collision clear` — removes the collision mesh (entity falls
  back to primitive collider or no mesh collider)

**Engine note:** The physics engine already fully decouples collision
geometry from render meshes. `phys_collider_t` references a
`phys_mesh_shape_t` via index into the world's mesh pool; the physics
system has zero knowledge of render meshes. The collision mesh is
stored as a separate asset alongside the render mesh, with its own
triangle array and BVH. No engine changes are required — the editor
simply manages two mesh assets per entity and associates the collision
mesh with the collider via `phys_world_set_mesh_collider()`.

---

## 5. Painting and Sculpting System

### 5.1 Brush Engine

All paint and sculpt tools share a unified brush engine:

```c
typedef struct brush_state {
    float position[3];      /* World-space hit point */
    float normal[3];        /* Surface normal at hit */
    float pressure;         /* Tablet pressure 0–1 (1.0 for mouse) */
    float tilt[2];          /* Tablet tilt X, Y in radians (0 for mouse) */
    float rotation;         /* Barrel rotation / brush twist */
    float radius;           /* Brush radius in world units */
    float strength;         /* Base strength before pressure curve */
    float falloff;          /* 0=hard edge, 1=full Gaussian falloff */
    uint32_t symmetry;      /* Bitmask: X=1, Y=2, Z=4 mirror planes */
} brush_state_t;
```

### 5.2 Wacom Tablet Support

- **Pressure** → brush strength (configurable curve: linear, ease-in, ease-out, custom)
- **Tilt** → brush angle / sculpt direction bias
- **Eraser end** → auto-switch to eraser/smooth tool
- **Barrel rotation** → brush texture rotation (for textured brushes)
- **Hover** → preview brush radius/shape before stroke begins

SDL2's tablet event support (`SDL_EVENT_PEN_*` in SDL3, or via raw
platform input on SDL2) provides pressure and tilt. A fallback path
uses mouse input with pressure=1.0.

### 5.3 Masking

Every paint and sculpt tool supports masking:

| Mask type | Description |
|-----------|-------------|
| **Face set** | Operations restricted to selected face set(s) |
| **Color mask** | Sample a color; only affect vertices/texels matching within threshold |
| **Vertex group** | Only affect vertices in the named group |
| **Texture mask** | Grayscale texture modulates brush strength |
| **Stencil** | Screen-space image overlay clips the brush |

Masks compose multiplicatively: `effective_strength = brush_strength * pressure * mask_value`.

### 5.4 Texture Layer System

Every texture on every object (and every terrain splatmap) is backed by
a **layer stack** — an ordered list of layers composited top-to-bottom.

```c
typedef struct texture_layer {
    char     name[64];          /* User-visible layer name */
    uint8_t  blend_mode;        /* 0=normal, 1=multiply, 2=add, 3=overlay, 4=screen */
    float    opacity;           /* 0.0–1.0 */
    bool     visible;           /* Toggle without deleting */
    bool     locked;            /* Prevent edits */
    uint32_t width, height;     /* Resolution (independent per layer) */
    uint8_t *pixels;            /* RGBA8 texel data (owned) */
} texture_layer_t;

typedef struct texture_layer_stack {
    uint32_t         count;
    uint32_t         capacity;   /* Grows dynamically; no fixed limit */
    texture_layer_t *layers;     /* Bottom-to-top order */
    uint32_t         active;     /* Index of the layer being painted */
} texture_layer_stack_t;
```

**Design decisions:**
- **No fixed layer limit** — the layer array grows dynamically. The
  practical limit is system memory (a 4096×4096 RGBA8 layer is 64 MB;
  dozens of layers are feasible on modern hardware).
- **Per-layer resolution** — a detail layer can be 8192×8192 while a
  base color layer is 2048×2048.
- **Blend modes**: Normal (alpha-blend), Multiply, Add, Overlay, Screen.
  More can be added later without breaking the format.
- **Terrain splatmap layers** are a special case of this system: each
  terrain material layer corresponds to a texture layer in the splatmap
  stack. The old 16-layer limit is removed; terrain can have as many
  material layers as system resources allow.

### 5.5 Texture Painting

Paint directly onto model surfaces in the viewport or in the 2D paint
panel (§5.8):

- **Target**: albedo, roughness, metallic, normal (as height→normal), emissive, or any custom channel
- **Resolution**: per-layer, configurable (256–8192)
- **Projection**: camera-space projection onto UV; supports multi-UV-set
- **Seam handling**: bleed margin (configurable, default 4px) across UV seams
- **Layer targeting**: paint operations write to the active layer in the stack
- **Local buffer**: strokes accumulate in a local texture buffer; flushed
  to server on stroke end or at 4Hz, whichever comes first
- **Baking**: `:paint bake` flattens the layer stack into final textures

### 5.6 Sculpting

Sculpt tools modify vertex positions on static geometry:

- **Local cache**: vertex deltas accumulate locally during a stroke
- **Batch flush**: deltas sent to server as a single `mesh_edit` command
  on stroke end
- **Multires**: future extension — subdivide for sculpt detail, bake to
  normal map

### 5.7 Weight Painting

Paint bone weights for skeletal meshes:

- **Visualization**: color gradient overlay (blue=0, red=1) on mesh
- **Auto-normalize**: weights always sum to 1.0 per vertex (excess
  subtracted proportionally from other groups)
- **Gradient tool**: linear or radial gradient fill within selection
- **Texture brush**: apply weight from a grayscale texture
- **Mirror**: auto-mirror weights to opposite bone (naming convention:
  `Bone.L` ↔ `Bone.R`)

### 5.8 2D Paint Panel

A secondary viewport that displays the active material's UV layout as a
flat 2D canvas. All paint tools (§5.5) are available in 2D mode, operating
directly on texel coordinates rather than via camera-space projection.

```
┌─ 2D Paint Panel ──────────────────────────────────────────────────┐
│ ┌─ Layer Stack ──┐  ┌─ UV Canvas ──────────────────────────────┐ │
│ │ ▸ Detail       │  │                                          │ │
│ │ ● Base Color   │  │   (flat UV layout of active material,    │ │
│ │ ▸ Overlay      │  │    layer stack composited in real time)   │ │
│ │                │  │                                          │ │
│ │ [+ Add Layer]  │  │                                          │ │
│ └────────────────┘  └──────────────────────────────────────────┘ │
│ Brush: [Brush ▼]  Radius: [===●===] 24px  Channel: [Albedo ▼]   │
└───────────────────────────────────────────────────────────────────┘
```

**Features:**
- **Pan/zoom** on 2D canvas (scroll to zoom, middle-drag to pan)
- **UV wireframe** overlay (toggleable) shows mesh UV boundaries
- **Seam visualization** — UV seams drawn as colored edges
- **Layer sidebar** — shows the current texture's layer stack; click to
  select active layer, toggle visibility, reorder via drag
- **Pixel-perfect editing** — at high zoom, individual texels are visible
  and editable
- **Mirrored preview** — edits in the 2D panel are reflected in the 3D
  viewport in real time (and vice versa)
- **Tile repeat** — optional tiled display mode for checking seamless
  textures

**Activation:** `:paint 2d` command, or click "2D" tab in the viewport
header, or double-click a material in the inspector.

### 5.9 UV Editing (Mesh Mode)

When in Mesh mode, a UV editing sub-panel is available for manipulating
UV coordinates of selected geometry. This provides basic UV manipulation
sufficient for aligning static geometry to texture atlases and trimsheets.

**Tools:**
- **Translate** — move selected UV vertices/faces
- **Rotate** — rotate selected UVs around pivot
- **Scale** — scale selected UVs (uniform or per-axis)
- **Snap to grid** — align UV vertices to a configurable grid (respects
  §2.5 snap settings for UV space)
- **Align to trimsheet edge** — select mesh edges/vertices and snap them
  to a visual edge of the active trimsheet (auto-detects horizontal and
  vertical edges in the trimsheet texture)

**UV Display:**
- UV wireframe overlaid on the current material texture
- Selected UVs highlighted; unselected UVs dimmed
- Trimsheet grid lines drawn when a trimsheet material is active
- UDIM tile indicators (future extension)

**Commands:**
- `:uv project <box|planar|cylindrical|sphere>` — auto-project UVs
- `:uv unwrap` — angle-based unwrap of selected faces
- `:uv pack` — pack UV islands to minimize wasted space
- `:uv align <top|bottom|left|right|center-h|center-v>` — align selected UVs to edge or center axis
- `:uv snap grid <size>` — set UV grid snap resolution
- `:uv trimsheet align` — snap selection to nearest trimsheet edge
- `:uv close` — close the UV editing sub-panel

**Design note:** This is intentionally minimal — enough for positioning
static geo on atlases and trimsheets. A full UV editor with seam marking,
island manipulation, and projection painting is a future extension.

---

## 6. Animation and Rigging System

### 6.1 Why In-Editor Animation

Blender (and other DCCs) cannot be the animation source of truth for
this engine because:

1. **Constraint mismatch** — our constraint system (cone-twist, hinge,
   twist, ball-socket, distance, lock, aim, limit rotation/position,
   copy rotation, etc.) does not map 1:1 to Blender's. Animations
   exported from Blender will look different under our constraints.

2. **Physics-coupled animation** — nonkinematic constraint chains
   produce physically-derived motion between keyframes. You cannot
   teleport or position-drive bones without generating massive velocity
   spikes that propagate through the constraint graph to children. The
   velocity must be derived from displacement, and per-joint damping
   must be tuned to get the right look. This tuning can only happen
   with our physics solver in the loop.

3. **Musculature** — no DCC tool supports our Hill-type muscle model
   with antagonist pairs, tendon elasticity, activation dynamics, and
   moment-arm geometry. Muscle-driven animations must be authored and
   tuned with the muscle system active.

4. **Runtime simulation** — our simulations can do things Blender
   can't preview in real-time (full ragdoll + muscle + constraint
   interaction), and these affect inter-keyframe motion.

5. **Simulation baking** — our constraint-driven animation system is
   powerful but can get expensive at runtime. The editor must support
   baking simulation results (skeletal, rigid body, fracture) into
   standard keyframes for deterministic, solver-free playback. This
   baking system is general-purpose: a building collapse simulation
   becomes a keyframe animation of rubble pieces (§6.12).

6. **Future extensions** — smear-frame generation via GJK+sweep mesh,
   constraint swaps during animation (e.g., bones breaking), keyframed
   force/velocity/mass channels, animated attribute modifiers — none of
   which exist in any DCC.

### 6.2 Bone Placement

In Animation mode, the viewport provides tools to:

1. **Place bones** — click to set head, drag to set tail, click parent to attach
2. **Edit bone transforms** — gizmos on bone heads/tails
3. **Set bone properties** — mass, collision shape, joint type via inspector

### 6.3 Constraint and Joint Setup

The inspector panel exposes the full bone_joint_desc and constraint_def
parameter sets. For each bone:

- **Joint type** dropdown (cone_twist, hinge, distance, lock, ball_socket, twist, etc.)
- **Limit visualization** — viewport overlays show cone/hinge limits as colored arcs
- **Constraint stack** — ordered list of animation constraints with enable/disable toggles

### 6.4 Musculature Installation

From the inspector, per-bone muscle setup:

- **Enable muscle pair** → flexor/extensor sub-panels appear
- **Attachment point visualization** — lines from origin to insertion rendered
  in viewport, with cylinders for wrap surfaces
- **Parameter tuning** — max_force, optimal_length, tau_rise/fall, tendon
  stiffness all adjustable with immediate visual feedback
- **Auto-fill** — compute reasonable defaults from bone geometry (length,
  mass, neighboring bone relationships)

### 6.5 Animation Timeline

The timeline is a horizontal panel that appears at the bottom of the
viewport (above the TUI) when in Animation mode. It displays keyframes,
the playhead, time selection, and simulation state.

```
┌─ Timeline ──────────────────────────────────────────────────────────┐
│ ◀ ▶ ⏸ ⏭ ⏹  │  Frame: [120]  FPS: [30]  Range: [0─300]           │
├─────────────────────────────────────────────────────────────────────┤
│ ── Channels ──                0    30    60    90    120   150      │
│ ▾ upper_arm.L                                                      │
│   Position   ─────◆──────────────◆────────────────◆──────────      │
│   Rotation   ──◆──────────◆────────────◆──────────────────────     │
│   Velocity   ─────────────────────◇────────────────────────────    │
│   Mass       ─────────────────────────────────────────────────     │
│ ▾ lower_arm.L                                                      │
│   Position   ──◆──────────────────◆───────────────────────────     │
│   Rotation   ─────◆────────────────────◆──────────────────────     │
│   Force      ────────────────◇──────────────────────────────────   │
│ ▸ hand.L     (collapsed)                                           │
│ ── Events ──                                                       │
│   evt        ────────────────▼──────────────▼──────────────────     │
│   constraint ─────────────────────────✕────────────────────────    │
├─────────────────────────────────────────────────────────────────────┤
│ [  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░  ]  time select   │
└─────────────────────────────────────────────────────────────────────┘

◆ = standard keyframe    ◇ = physics keyframe (velocity/force/mass)
▼ = event trigger        ✕ = constraint swap event
```

### 6.6 Keyframeable Channels

Every bone and entity supports keyframing on these channel types:

| Channel | Symbol | Description |
|---------|--------|-------------|
| **Position** | ◆ | World or local position (vec3) |
| **Rotation** | ◆ | Quaternion rotation |
| **Scale** | ◆ | Scale (vec3) |
| **Velocity** | ◇ | Linear velocity (vec3) — injected into physics solver |
| **Angular velocity** | ◇ | Angular velocity (vec3) — injected into physics solver |
| **Force** | ◇ | Persistent force applied at keyframe time (vec3) |
| **Torque** | ◇ | Persistent torque applied at keyframe time (vec3) |
| **Mass** | ◇ | Body mass — can change mid-animation |
| **Muscle activation** | ◇ | Per-muscle excitation signal u(t) ∈ [0,1] |
| **Muscle max force** | ◇ | Animate muscle strength over time |
| **Muscle tau rise/fall** | ◇ | Animate activation dynamics |
| **Tendon stiffness** | ◇ | Animate tendon properties |
| **Joint damping** | ◇ | Per-joint damping factor (critical for tuning physics-driven motion) |
| **Joint stiffness** | ◇ | Per-joint stiffness |
| **Joint limits** | ◇ | Animate constraint limits (e.g., loosen a joint over time) |
| **Attribute modifier** | ◆ | Drive any entity attribute by keyframe (§6.7) |

#### Velocity Derivation for Kinematic Bones

When a bone is **kinematic** (animation-driven, not physics-driven),
its velocity is **derived from displacement** between keyframes rather
than set directly. This prevents the massive velocity spikes that would
occur from teleporting a physics body:

```
v_derived = (pos[frame] - pos[frame-1]) / dt
```

The derived velocity is injected into the physics solver so that
children in the constraint graph receive physically correct impulses.
A per-joint **animation damping factor** controls how much of the
derived velocity propagates to children:

```
v_child_impulse = v_derived * joint_anim_damping
```

This damping factor is the primary tuning parameter for getting the
right "feel" on physically-driven child bones. It is keyframeable.

### 6.7 Animated Attribute Modifiers

An **animated attribute modifier** is a keyframe set that drives an
arbitrary entity attribute (from the `entity_attrs_t` key-value space).
This enables keyframing gameplay-relevant properties without hardcoding
them into the animation system:

```
:anim attr health  0.0 1.0   # keyframe "health" attr from 1.0 to 0.0
:anim attr glow    0.0 0.5   # keyframe "glow" attr ramp up
```

The engine-side implementation adds a new component:

```c
typedef struct anim_attr_modifier {
    char     attr_key[64];     /* Entity attribute key */
    uint32_t keyframe_count;
    float   *times;            /* Sorted keyframe times */
    float   *values;           /* One value per keyframe */
    uint8_t  interp;           /* 0=step, 1=linear, 2=cubic */
} anim_attr_modifier_t;
```

At evaluation time, the animation system interpolates the value and
writes it into the entity's attribute space. Scripts and gameplay code
can read these attributes normally.

### 6.8 Constraint Swap Events

A **constraint swap event** is a keyframe that modifies the constraint
graph mid-animation. This enables:

- **Breaking bones** — replace a cone-twist joint with a distance joint
  (ragdoll-like flail) when a collision impact exceeds a threshold
- **Locking joints** — swap a hinge to a lock joint (e.g., character
  grabs and holds an object)
- **Releasing constraints** — remove a constraint entirely (e.g.,
  character drops something)
- **Changing limits** — swap to a version with wider/narrower limits

Engine-side, this requires:

```c
typedef struct anim_constraint_event {
    float    time;              /* Trigger time */
    uint32_t bone_idx;         /* Which bone's joint to modify */
    uint8_t  action;           /* 0=replace, 1=remove, 2=add */
    bone_joint_desc_t new_desc; /* New joint descriptor (for replace/add) */
} anim_constraint_event_t;
```

The physics solver applies constraint swaps between ticks, at the
event's trigger time. The swap is atomic — the old constraint is
removed and the new one inserted in a single drain pass.

Constraint swaps are also triggerable by **collision events** using
the joint's `break_strength` field: when accumulated impulse exceeds
`break_strength`, the engine fires a constraint swap event automatically.

### 6.9 Gameplay Event Triggers

Animation events can fire gameplay events without requiring a script:

```
:anim event "footstep" 15      # fire "footstep" event at frame 15
:anim event "attack_hit" 22    # fire "attack_hit" at frame 22
:anim event "sound:swoosh" 20  # fire namespaced event
```

Events are stored as:

```c
typedef struct anim_event {
    float    time;             /* Trigger time */
    char     name[64];         /* Event name (namespaced: "sound:footstep") */
    float    params[4];        /* Optional float params (intensity, etc.) */
} anim_event_t;
```

Events are dispatched to the engine's event bus. Gameplay systems
subscribe to event names and respond without a script intermediary.
Scripts can also subscribe if more complex logic is needed, but the
common case (footstep sounds, particle spawns, hitbox activation)
should not require a script.

#### Event-to-Animation Binding

Animations can also be **triggered by** gameplay events:

```
:anim bind "on_hit" "flinch_upper"     # play flinch when hit
:anim bind "on_land" "land_impact"     # play landing on ground contact
:anim bind "on_grab" "grab_reach"      # play grab on interaction
```

Bindings are stored per-entity and evaluated by the animation state
machine. Multiple bindings can target the same event with priority and
blend weights.

### 6.10 Timeline Simulation Controls

The timeline integrates with a **client-local physics simulation** for
animation preview. When the editor enters physics playback, it spawns
its own parallel physics tick runner — it does **not** use the global
server physics tick (which runs for all connected clients and gameplay).
This means:

- Animation simulation is isolated: only the editor client running the
  preview is affected. Other editors see the skeleton in its last
  committed pose.
- The local tick runner operates on a copy of the skeleton's physics
  state (bodies, constraints, muscles). It steps independently at the
  animation tick rate.
- Baked keyframes (from recording) are committed to the server on
  `:anim record stop` or explicit save, at which point they become
  visible to other editors.
- The server's global physics tick continues running unaffected —
  gameplay entities, NPC physics, etc. are not paused or disturbed.

Simulation controls:

| Control | Key | Description |
|---------|-----|-------------|
| **Play** | `Space` | Run simulation forward from playhead |
| **Pause** | `Space` | Freeze simulation |
| **Step forward** | `Right` | Advance one physics tick |
| **Step 10** | `Shift+Right` | Advance 10 ticks |
| **Rewind** | `Left` or `Backspace` | Reset to frame 0 (or time selection start) |
| **Fast-forward** | `Shift+Space` | Run simulation at max speed (no frame sync) |
| **Fast-forward (async)** | `:anim ff --async` | Server runs physics without display sync |
| **Record** | `Ctrl+R` | Capture simulation as baked keyframes |
| **Loop** | `L` | Toggle time selection loop |

#### Rewind Semantics

Rewinding resets the physics state (positions, velocities, forces) to
the state at frame 0 **except** for forces/velocities baked into
keyframes — those are replayed on the next play-through. This means:

- Gravity, collision forces, muscle forces: reset
- Keyframed velocities, forces, muscle activations: preserved
- Constraint state: reset to initial constraint graph (before any swaps)

#### Fast-Forward Modes

| Mode | Description |
|------|-------------|
| **Synchronous** (`Shift+Space`) | Physics runs as fast as possible; viewport updates every Nth frame |
| **Asynchronous** (`:anim ff --async`) | A temporary server-side physics runner spins up for this skeleton only, runs headless without display; editor shows progress bar; results stream back when done |

Async fast-forward is useful for long simulations (e.g., cloth settle,
ragdoll rest). The server spins up a temporary physics runner for this
skeleton only (separate from the global gameplay tick) and runs it
headless, streaming the final state back to the requesting editor.

### 6.11 Time Selection

The timeline supports a **time selection** — a highlighted range used
for:

- Loop playback (play loops within selection)
- Batch keyframe operations (copy/paste/delete keyframes in range)
- Bake range (record simulation only within selection)
- Export range (export clip covering selection)

Set via drag on the timeline ruler, or `:anim range 30 120`.

### 6.12 Simulation Baking

Baking converts simulation output into keyframe data. This is a
general-purpose system — it works for **any** simulation, not just
skeletal animation:

| Scenario | What gets baked |
|----------|----------------|
| **Skeletal animation** | Bone positions/rotations from ragdoll, muscle, or constraint-driven sim |
| **Rigid body simulation** | Entity transforms from physics (e.g., falling debris, explosions) |
| **Fractured objects** | Per-fragment transforms from fracture sim (building collapse → rubble animation) |
| **Cloth / soft body** | Vertex positions per frame (future) |

**Workflow (skeleton):**
1. Enter animation mode, set up keyframes and/or enable physics sim
2. Press `Ctrl+R` to start recording, or `:anim record start`
3. Play simulation — the local physics runner captures transforms at
   each tick
4. Press `Ctrl+R` again to stop, or `:anim record stop`
5. Baked keyframes appear in the timeline as regular keyframes (editable)

**Workflow (rigid body / fracture):**
1. Select one or more entities in Object mode
2. Set up physics simulation (e.g., fracture a mesh, enable rigid body)
3. Switch to Animation mode: `:anim bake sim`
4. The editor spawns a local physics runner for the selected entities
   (isolated from the global server tick, just like skeletal sim)
5. The sim runs for the specified frame range (time selection, or
   `:anim bake sim --range 0 300`)
6. Entity transforms are captured at each tick and written as position/
   rotation keyframes on each entity
7. The baked animation is committed to the server

**Commands:**
- `:anim record start` / `:anim record stop` — manual record (skeletal)
- `:anim bake sim` — bake selected entities' physics sim to keyframes
- `:anim bake sim --range <start> <end>` — bake within frame range
- `:anim bake sim --step <n>` — capture every Nth frame (reduce data)
- `:anim bake clean <tolerance>` — remove redundant keyframes within
  tolerance (post-bake simplification)

**Design decisions:**
- Baking produces standard keyframes — once baked, the animation can be
  edited, exported, or re-simulated just like hand-authored keyframes.
- Baked keyframes replace simulation at runtime: the engine plays the
  baked data instead of re-running the physics solver, giving
  deterministic, fast playback.
- The baking system is mode-agnostic: it captures entity transforms
  regardless of what produced them (constraint solver, rigid body,
  muscle drive, or manual interaction).
- Post-bake cleanup (`:anim bake clean`) removes keyframes that can be
  reconstructed by interpolation within a tolerance, reducing data size.

### 6.13 Inline Physics Simulation

Beyond timeline-driven playback, the viewport supports free-form
physics interaction. Like timeline playback (§6.10), this uses the
editor's client-local physics tick runner — not the server's global
tick. All interactions below operate on the local physics state copy:

- **Drag** — click and drag bones during simulation to test ragdoll response
- **Apply force** — Ctrl+click on a bone during sim to apply impulse
- **Spring drag** — Ctrl+drag a bone to spring-attach it to cursor
- **Freeze bone** — right-click bone during sim → "Pin" to hold in place

### 6.14 Future: Smear-Frame Generation

A planned extension is **smear-frame generation** using GJK+sweep:

1. Between two keyframes, compute the swept volume of each bone's
   collision geometry using GJK's support function
2. Generate a mesh hull of the swept volume
3. Render the swept mesh with motion-blur material (stretch + fade)
4. Use this as a smear-frame for high-velocity animations (punches,
   weapon swings, fast locomotion)

This requires no DCC support — it is generated entirely from the
physics collision geometry and the animation data.

---

## 7. Scene Organization

### 7.1 Outliner Hierarchy

```
World
├── [Layer] Terrain
│   ├── terrain_page_0_0
│   └── terrain_page_0_1
├── [Layer] Static Geometry
│   ├── [Group] Buildings
│   │   ├── house_01
│   │   └── house_02
│   └── [Group] Props
│       ├── barrel_01
│       └── crate_01
├── [Layer] Characters
│   └── humanoid_01
└── [Layer] Lights
    ├── sun
    └── point_light_01
```

### 7.2 Visibility and Memory Management

Each item in the outliner has two independent toggles:

| Toggle | Icon | Effect |
|--------|------|--------|
| **Visible** | 👁 | Hide from rendering; GPU/CPU data retained |
| **Resident** | 💾 | Swap to disk; free GPU + CPU memory |

These work at any hierarchy level:
- Toggle on a **Layer** → affects all children
- Toggle on a **Group** → affects group members
- Toggle on an **Object** → affects that object only

Swapped-to-disk objects appear as bounding-box wireframes in the viewport
and grayed-out entries in the outliner.

### 7.3 Scalability for Large Scenes

| Technique | Description |
|-----------|-------------|
| **Spatial indexing** | BVH over static geometry for fast culling |
| **GPU instancing** | Identical meshes drawn with instanced calls |
| **Texture streaming** | Mip levels loaded on demand based on screen coverage |
| **Async mesh load** | Background thread loads mesh data; main thread uploads to GPU |
| **Octree-based swap** | Auto-swap distant static geometry to disk |
| **View-dependent LOD** | LOD selection per draw call based on screen-space size |

---

## 8. Collaborative Editing

### 8.1 Multi-Editor Protocol

Multiple editor instances connect to the same game server:

1. Each editor has a unique `editor_id` assigned at connection time.
2. All edit commands include the `editor_id`.
3. The server broadcasts mutations to all connected editors.
4. Each editor applies remote mutations to its local view.

### 8.2 Edit Locking

The locking system provides two lock modes to prevent conflicts before
they happen. Locks operate on any selectable target: individual entities,
groups, layers, bones, mesh geometry regions, or terrain pages.

#### Lock Modes

| Mode | Command | Effect |
|------|---------|--------|
| **Exclusive** | `:lock <target>` | Only the locking editor can edit; others can view |
| **Freeze** | `:freeze <target>` | Nobody can edit (including the locking editor) until unfrozen |

Both modes are **enforced** — the server rejects mutations to locked
targets from unauthorized editors, returning an error with the lock
holder's editor name.

#### Lock Targets

Locks are context-aware — the target type depends on the current mode:

| Mode | Lock target | Description |
|------|-------------|-------------|
| Object | Entity or group or layer | Lock bodies, transforms, components |
| Mesh | Mesh slot or face set | Lock geometry editing |
| Sculpt | Mesh region (AABB) | Lock sculpt area |
| Paint | Texture + UV region | Lock paint area |
| Animation | Skeleton or bone subset | Lock bone transforms, constraints, keyframes |
| Terrain | Terrain page(s) | Lock terrain editing |

#### Commands

```
:lock                         # Lock current selection (exclusive to this editor)
:lock house_01                # Lock specific entity
:lock --layer "Static Geo"    # Lock entire layer
:lock --group Buildings       # Lock group
:freeze                       # Freeze current selection (nobody can edit)
:freeze --layer Terrain       # Freeze terrain layer
:unlock                       # Unlock current selection
:unlock --all                 # Unlock everything this editor holds
:locks                        # List all active locks (who holds what)
```

#### Lock Protocol

```json
{"cmd": "edit_lock", "args": {"target": "house_01", "mode": "exclusive", "timeout": 300}}
{"cmd": "edit_lock", "args": {"target": "house_01", "mode": "freeze"}}
{"cmd": "edit_unlock", "args": {"target": "house_01"}}
```

Lock state is visible in the outliner (lock icon + editor name for
exclusive, padlock icon for freeze). Locks have an optional timeout
(default 5 minutes for exclusive, no timeout for freeze) and can be
forcibly broken by any editor with `:unlock --force <target>` (which
logs a warning to all editors).

### 8.3 Conflict Resolution

For unlocked targets, conflicts are resolved by last-write-wins:

| Scenario | Resolution |
|----------|-----------|
| Two editors move the same entity | Last-write-wins (server timestamp) |
| Two editors sculpt overlapping vertices | Per-vertex last-write-wins |
| Two editors paint overlapping texels | Per-texel last-write-wins |
| One editor deletes entity another is editing | Delete wins; editing editor gets notification |
| Two editors modify same component | Last-write-wins per field |
| Two editors modify same keyframe | Last-write-wins per channel per frame |

The recommended workflow is to lock what you're actively editing. The
lock system makes conflicts impossible for locked targets; last-write-wins
is the fallback for unlocked shared work.

### 8.4 Presence Indicators

Each editor's camera frustum and cursor position are broadcast to other
editors. The viewport renders other editors as small camera icons with
colored frustum wireframes.

---

## 9. Terrain System

### 9.1 Page-Based Terrain

Terrain is divided into fixed-size pages (e.g., 256×256 vertices).
Each page is an independent mesh that can be:
- Edited (sculpt tools)
- Hidden or swapped to disk independently
- LOD-streamed based on camera distance

### 9.2 Terrain Tools

| Tool | Description |
|------|-------------|
| **Raise/Lower** | Displace vertices along normal |
| **Smooth** | Average neighboring vertex heights |
| **Flatten** | Set vertices to a target height |
| **Stamp** | Apply a heightmap texture as displacement |
| **Erode** | Simulate hydraulic/thermal erosion |
| **Paint** | Blend between terrain material layers |

### 9.3 Terrain Material Layers

Terrain material layers are a special case of the texture layer system
(§5.4). Each terrain splatmap layer has:
- Albedo, normal, roughness textures
- Tiling scale
- Height-blend parameters (for natural transitions)

There is no fixed layer limit — the layer stack grows dynamically per
§5.4. Weight painting tools (§5.7) apply to terrain splatmap layers.

---

## 10. Integration with Existing Systems

### 10.1 Reuse from Existing Editor Specs

| System | Source | Reuse |
|--------|--------|-------|
| Edit protocol | `ref/editor_spec.md` §2.1, §5 | TCP command format, command vocabulary |
| Asset registry | `ref/editor_spec.md` §2.2 | Asset catalog, hot-reload |
| Asset download | `ref/editor_spec.md` §2.3 | TCP asset transfer |
| Script runtime | `ref/editor_spec.md` §2.5 | Procedural generation, REPL |
| Texture synthesis | `ref/editor_spec.md` §6 | Noise primitives, bake-to-UV |
| Mesh modeling | `ref/mesh_modeling_spec.md` | Full vertex/edge/face editing |
| Command vocabulary | `ref/editor_ux.md` §3–8 | All commands carry over |
| MCP server | `ref/editor_spec.md` §4.5 | AI agent integration |

### 10.2 New Modules

| Module | Location | Description |
|--------|----------|-------------|
| Clay UI backend | `src/editor/ui/` | OpenGL renderer for Clay commands, font atlas, theme |
| Scene editor main | `src/editor/scene/` | SDL2 window, panel layout, event routing |
| Panel system | `src/editor/panels/` | Outliner, inspector, viewport, TUI panel |
| Brush engine | `src/editor/brush/` | Unified brush state, pressure curves, masking |
| Tablet input | `src/editor/input/tablet.c` | Wacom/pen input abstraction |
| Paint system | `src/editor/paint/` | Texture painting, weight painting |
| Sculpt system | `src/editor/sculpt/` | Vertex displacement tools |
| Terrain editor | `src/editor/terrain/` | Page management, terrain tools |
| Undo system | `src/editor/undo/` | Branching undo, conflict detection, rebase |
| Collab sync | `src/editor/collab/` | Multi-editor presence, lock management |
| Local cache | `src/editor/cache/` | Stroke buffering, batch flush |
| Disk swap | `src/editor/stream/` | Object swap-to-disk, async loading |
| Animation tools | `src/editor/anim/` | Bone placement, constraint UI, simulation control |
| Animation timeline | `src/editor/timeline/` | Timeline panel, keyframe editing, time selection |
| Keyframe system | `src/editor/keyframe/` | Channel keyframes, interpolation, physics channels |
| Anim events | `src/editor/anim/events/` | Gameplay events, constraint swaps, event bindings |
| Attr modifiers | `src/editor/anim/attr/` | Animated attribute modifier evaluation |
| Texture layers | `src/editor/paint/layer/` | Layer stack, blend modes, compositing |
| 2D paint panel | `src/editor/panels/panel_paint2d.c` | 2D UV canvas for texture painting |
| UV editor | `src/editor/mesh/uv/` | UV manipulation tools for mesh mode |
| Selection system | `src/editor/scene/` | Click, box, shift/ctrl multi-select, linked select |
| Command handlers | `src/editor/commands/` | TUI command dispatch: mode, select, camera, cursor, paint, uv, connect, import, save |

### 10.3 Build Configuration

```makefile
make build/scene_editor EDITOR=1 SCENE=1

# With Tracy profiling
make build/scene_editor EDITOR=1 SCENE=1 TRACY=1
```

---

## 11. Persistence and Sync

The editor does **not** have traditional save/load. All edits are sent
to the server via the edit protocol (§2.2) as they happen. The server
persists world state to disk automatically (auto-sync). This is
analogous to Google Drive — every edit is synced as soon as possible.

### Save Commands

| Command | Description |
|---------|-------------|
| `:save force` | Tell the server to flush all pending state to disk immediately |
| `:save status` | Show detailed sync state: pending commands, last sync time, connection health |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+S` | Rebound to `:save force` (force flush to disk) |
| `Ctrl+Shift+S` | **Removed** — there is no "save as" since persistence is server-side |

### Sync Status Indicator

The TUI status bar displays a persistent sync indicator:

- **"Synced"** — all edits have been acknowledged by the server, with
  a timestamp showing the last successful sync
- **"Syncing..."** — one or more edit commands are in flight and have
  not yet been acknowledged

### Offline Queue

If the connection to the server is lost, pending edits are queued
locally and replayed automatically on reconnect. The status bar shows
"Disconnected (N edits queued)" until the connection is restored.

---

## 13. Non-Goals

- **Full Photoshop-equivalent image editor** — the 2D paint panel (§5.8)
  provides basic paint tools and layer management for textures, but
  advanced 2D compositing features (vector layers, text tool, adjustment
  layers, filter stacks, etc.) are out of scope. Use external tools for
  complex 2D image work.
- **Full UV editor** — basic UV manipulation is provided (§5.9) for
  aligning static geo to atlases and trimsheets. Advanced UV operations
  (seam marking, automatic island splitting, UDIM, projection painting)
  are future extensions.
- **Terrain with unlimited resolution** — pages have fixed vertex
  density; detail is added via normal maps and displacement shaders.
- **Real-time voice chat** — collaboration is visual (presence indicators)
  and text (TUI); voice is out of scope.
- **NLA / animation layer blending UI** — the editor supports keyframing
  and simulation but does not provide a full nonlinear animation editor.
  Animation blending and state machines are configured via the inspector
  and commands, not a visual NLA strip editor.
