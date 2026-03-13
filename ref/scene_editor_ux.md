# Scene Editor UX Specification

## 1. Window Layout

The editor is a single SDL2 window divided into four panels (five in
Animation mode, which adds the timeline). Panels are resizable by
dragging dividers. Layout persists across sessions.

```
┌──────────────────────────────────────────────────────────────────────────┐
│  ┌─ Outliner ──────────┐  ┌─ 3D Viewport ───────────────────────────┐  │
│  │                     │  │                                          │  │
│  │  Scene hierarchy    │  │                                          │  │
│  │  with visibility    │  │  Main scene render                       │  │
│  │  and swap toggles   │  │                                          │  │
│  │                     │  │  (occupies maximum available space)       │  │
│  │                     │  │                                          │  │
│  │                     │  │                                          │  │
│  │  ≈ 15–20% width     │  │                                          │  │
│  │                     │  │                                          │  │
│  │                     │  ├─ Inspector ───────────────────────────────┤  │
│  │                     │  │                                          │  │
│  │                     │  │  Entity/component properties             │  │
│  │                     │  │  ≈ 30% of right column height            │  │
│  │                     │  │                                          │  │
│  └─────────────────────┘  └──────────────────────────────────────────┘  │
│  ┌─ TUI Panel ──────────────────────────────────────────────────────┐   │
│  │  Command log + input line                                        │   │
│  │  ≈ 15–25% of window height                                      │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────┘
```

### Default Proportions

| Panel | Width | Height | Position |
|-------|-------|--------|----------|
| Outliner | 18% | top row (75%) | Top-left |
| Viewport | 82% | top row minus inspector | Top-right, above inspector |
| Inspector | 82% | 30% of top row | Top-right, below viewport |
| TUI | 100% | 25% | Bottom, full width |

All panels can be collapsed (double-click divider) or expanded. `F5`
toggles TUI panel. `F6` toggles outliner. `F7` toggles inspector.
`F8` toggles timeline (auto-shown when entering Animation mode).
`F11` or `Alt+Enter` toggles fullscreen.

### Panel Focus

Exactly one panel has keyboard focus at a time, indicated by a highlighted
border. Focus follows:

| Action | Focus target |
|--------|-------------|
| Click in panel | That panel |
| `Tab` | Cycle: Viewport → TUI → Outliner → Inspector |
| `Escape` | Viewport (always) |
| `:` (from viewport) | TUI command line |
| Any typing (from viewport in normal mode) | Stays in viewport |

---

## 2. Viewport Interaction

### 2.1 Camera Controls

| Input | Action |
|-------|--------|
| **Middle mouse drag** | Orbit around focus point |
| **Shift + middle drag** | Pan |
| **Scroll wheel** | Zoom (dolly toward cursor) |
| **Alt + middle drag** | Orbit (alternate binding) |
| **1 / 3 / 7** | Front / Right / Top view |
| **5** | Toggle perspective / orthographic |
| **0** | Reset camera to default |
| **F** | Frame selection (zoom to fit selected objects) |

### 2.2 Object Interaction

| Input | Action |
|-------|--------|
| **Left click** | Select object under cursor |
| **Shift + left click** | Add/remove from selection |
| **Left drag (empty space)** | Box select |
| **Right click** | Context menu |
| **G** | Grab mode (move selection) |
| **R** | Rotate mode |
| **S** | Scale mode |
| **X / Y / Z** (during transform) | Constrain to axis |
| **Shift+X/Y/Z** (during transform) | Constrain to plane (exclude axis) |
| **Enter** (during transform) | Confirm |
| **Escape** (during transform) | Cancel |
| **Delete / X** (with selection, no mode) | Delete selected |

### 2.3 3D Cursor

| Input | Action |
|-------|--------|
| **Ctrl + right click** | Place 3D cursor at raycast hit |
| **Shift + S** | Snap menu (cursor→selection, selection→cursor, etc.) |

### 2.4 Gizmos

Transform gizmos appear on the active selection:

- **Move gizmo**: three axis arrows + center square (free move)
- **Rotate gizmo**: three colored rings
- **Scale gizmo**: three axis cubes + center cube (uniform)

Gizmo visibility toggled via toolbar or `:gizmo move|rotate|scale|off`.

### 2.5 Grid Snap

When snap is enabled for a transform type, interactive transforms
(G/R/S) quantize values to the configured grid increment:

- **Position snap**: values round to the nearest grid size (e.g., 0.25m
  increments). The viewport grid lines match the current position snap
  size.
- **Rotation snap**: angles round to the nearest snap angle (e.g., 15°
  increments).
- **Scale snap**: scale factors round to the nearest increment (e.g.,
  0.1 steps).

Each axis (X, Y, Z) can be independently enabled or disabled for each
transform type. When an axis has snap disabled, that axis moves
continuously even if the overall transform type has snap on.

Snap is active during both keyboard transforms (G/R/S) and gizmo drags.

### 2.6 Pivot Mode

Each entity has a pivot point (local transform origin). Pivot
manipulation:

| Input | Action |
|-------|--------|
| **Shift+P** | Enter pivot mode — dragging moves the pivot, not the object |
| **Enter** (in pivot mode) | Confirm pivot position |
| **Escape** (in pivot mode) | Cancel and restore previous pivot |

In pivot mode, the gizmo appears at the current pivot position and
the cursor changes to indicate pivot editing. Snap applies to pivot
movement when position snap is enabled.

---

## 3. Outliner Panel

### 3.1 Layout

```
┌─ Outliner ──────────────────────┐
│ 🔍 [filter........................]│
├─────────────────────────────────┤
│ ▸ 🌍 World             👁 💾    │
│   ├─ 📁 Terrain        👁 💾    │
│   │  ├─ page_0_0       👁 💾    │
│   │  └─ page_0_1       👁 💾    │
│   ├─ 📁 Static Geo     👁 💾    │
│   │  ├─ 📦 Buildings   👁 💾    │
│   │  │  ├─ house_01    👁 💾    │
│   │  │  └─ house_02    👁 💾    │
│   │  └─ 📦 Props       👁 💾    │
│   ├─ 📁 Characters     👁 💾    │
│   └─ 📁 Lights         👁 💾    │
└─────────────────────────────────┘
```

(Icons shown for clarity; actual rendering uses engine glyph/icon atlas.)

### 3.2 Interactions

| Action | Effect |
|--------|--------|
| **Click item** | Select entity; show in inspector |
| **Shift+click** | Range select |
| **Ctrl+click** | Toggle add/remove from selection |
| **Click 👁** | Toggle visibility (retain in memory) |
| **Click 💾** | Toggle disk residency (swap to/from disk) |
| **Right click** | Context menu: rename, delete, group, ungroup, duplicate |
| **Drag item** | Reparent in hierarchy |
| **Double click** | Rename inline |
| **Type in filter** | Filter by name (incremental) |

### 3.3 Context Menu

```
┌─────────────────────┐
│ Rename         F2   │
│ Duplicate      D    │
│ Delete         Del  │
│ ─────────────────── │
│ Group          G    │
│ Ungroup        U    │
│ ─────────────────── │
│ New Layer      L    │
│ Move to Layer  ►    │
│ ─────────────────── │
│ Hide           H    │
│ Swap to Disk   S    │
│ Hide Others    I    │
│ Show All       A    │
│ ─────────────────── │
│ Lock (exclusive) 🔒 │
│ Freeze (all)     🔐 │
│ Unlock               │
└─────────────────────┘
```

---

## 4. Inspector Panel

### 4.1 Layout

The inspector shows properties of the selected entity. Sections are
collapsible.

```
┌─ Inspector ─────────────────────────────────┐
│ Entity: house_01                 [🔒 Lock]  │
├─────────────────────────────────────────────┤
│ ▾ Transform                                 │
│   Position  [10.00] [0.00] [5.00]           │
│   Rotation  [0.00] [0.00] [0.00]  (euler)   │
│   Scale     [1.00] [1.00] [1.00]            │
├─────────────────────────────────────────────┤
│ ▾ Mesh                                      │
│   Asset: assets/meshes/house.glb            │
│   Vertices: 12,480   Triangles: 8,320       │
│   [Open in Mesh Mode]                       │
├─────────────────────────────────────────────┤
│ ▾ Physics                                   │
│   Body type: [Static ▼]                     │
│   Mass:      [0.0        ]                  │
│   Collider:  [Mesh ▼]                       │
│   Collision mesh: house_collision.glb        │
│     Triangles: 248  [Edit] [Auto] [Clear]   │
│   Restitution: [0.3      ]                  │
│   Friction:    [0.5      ]                  │
├─────────────────────────────────────────────┤
│ ▾ Materials (3)                             │
│   [0] stone_wall     [Edit] [Paint] [2D]    │
│   [1] wood_trim      [Edit] [Paint] [2D]    │
│   [2] roof_tiles     [Edit] [Paint] [2D]    │
├─────────────────────────────────────────────┤
│ ▾ Layers (stone_wall)                       │
│   [👁] [🔒] Detail        Normal   1.0      │
│   [👁] [  ] ● Base Color  Normal   1.0      │
│   [👁] [  ] Overlay       Multiply 0.6      │
│                                              │
│   [+ Add]  [Flatten]  [Duplicate]           │
├─────────────────────────────────────────────┤
│ ▸ Rendering (collapsed)                     │
│ ▸ Custom Attributes (collapsed)             │
└─────────────────────────────────────────────┘
```

- **[2D]** button opens the 2D paint panel (§7.7) for that material
- **Layers section** appears when a material is expanded; shows all layers
  in the active material's layer stack
- **Eye icon** toggles layer visibility; **lock icon** prevents edits
- **● marker** indicates the active paint target layer
- **Click** a layer name to make it active; **drag** to reorder
- **Right-click** a layer for context menu: rename, duplicate, merge down,
  delete, change blend mode, adjust opacity
- **[Flatten]** merges all visible layers into one

### 4.2 Animation Mode Inspector

When in Animation mode with a skeleton selected:

```
┌─ Inspector ─────────────────────────────────┐
│ Skeleton: humanoid_01                       │
│ Bone: upper_arm.L                           │
├─────────────────────────────────────────────┤
│ ▾ Bone Transform                            │
│   Head   [0.25] [1.40] [0.10]              │
│   Tail   [0.25] [1.10] [0.10]              │
│   Roll   [0.00]                             │
├─────────────────────────────────────────────┤
│ ▾ Collision Body                            │
│   Shape:  [Capsule ▼]                       │
│   Radius: [0.04    ]  Height: [0.30    ]    │
│   Mass:   [2.5     ]                        │
│   [Auto-fit from Mesh]                      │
├─────────────────────────────────────────────┤
│ ▾ Joint (to parent: shoulder.L)             │
│   Type: [Cone Twist ▼]                      │
│   ┌─ Limits ──────────────────────────┐     │
│   │ X: [-45°] to [90°]   [✓]         │     │
│   │ Y: [-30°] to [30°]   [✓]         │     │
│   │ Z: [-60°] to [10°]   [✓]         │     │
│   └───────────────────────────────────┘     │
│   Stiffness:  [100.0  ]                    │
│   Damping:    [5.0    ]                     │
│   [Show Limits in Viewport]                 │
├─────────────────────────────────────────────┤
│ ▾ Muscle Drive                              │
│   [✓] Enable                                │
│   Target row: [0]                           │
│   ┌─ Flexor ─────────────────────────┐      │
│   │ Max force:    [200.0 N ]         │      │
│   │ Opt. length:  [0.12 m  ] (auto)  │      │
│   │ Max velocity: [10.0 L/s]         │      │
│   │ τ rise/fall:  [0.015] [0.050] s  │      │
│   │ Tendon slack: [0.00 m  ] (auto)  │      │
│   │ Tendon stiff: [35.0    ]         │      │
│   │ Wrap radius:  [0.02 m  ]         │      │
│   │ [Show Attachment Points]         │      │
│   └──────────────────────────────────┘      │
│   ┌─ Extensor ───────────────────────┐      │
│   │ (same layout)                    │      │
│   └──────────────────────────────────┘      │
├─────────────────────────────────────────────┤
│ ▾ Constraints (2)                           │
│   [1] Damped Track  target: spine  [✓]      │
│   [2] Limit Rotation               [✓]      │
│   [+ Add Constraint]                        │
└─────────────────────────────────────────────┘
```

### 4.3 Multi-Select Inspector

When multiple entities are selected, the inspector shows shared
properties. Changed values apply to all selected entities:

- Fields with identical values across selection: shown normally
- Fields with mixed values: shown as "—" or "[Mixed]"
- Editing a mixed field: sets all selected entities to the new value

---

## 5. TUI Panel

### 5.1 Layout

The TUI panel is a fully functional embedded terminal, identical to the
standalone controller from `ref/editor_ux.md`:

```
┌─ TUI ──────────────────────────────────────────────────────────────┐
│ Cursor: (10.00, 0.00, 5.00)  Grid: 1.0m  Snap: ON  Mode: OBJECT  Synced 12:34:05 │
├────────────────────────────────────────────────────────────────────┤
│  [entity_042] box 2×2×2 at (10, 0, 5) mass=0 static              │
│  > Spawned 2 entities from script gen_courtyard.script            │
│  > Texture stone_wall_01 synthesized (1024×1024, 3 maps)          │
│                                                                    │
├────────────────────────────────────────────────────────────────────┤
│ :spawn box 2 2 2                                       [TAB: 3]   │
└────────────────────────────────────────────────────────────────────┘
```

The full command vocabulary from `ref/editor_ux.md` §3–8 is available.
All TUI keybindings, tab completion, REPL mode, and command history
work identically.

**Sync status indicator** appears in the status bar:
- `Synced HH:MM:SS` — all edits flushed to server
- `Syncing... (3)` — 3 commands in flight
- `Offline (12 queued)` — disconnected, 12 edits queued locally

### 5.2 TUI ↔ Viewport Integration

Actions in the TUI affect the viewport and vice versa:

| TUI action | Viewport effect |
|------------|-----------------|
| `:select house_01` | house_01 highlighted in viewport |
| `:cursor 10 0 5` | 3D cursor moves in viewport |
| `:mode sculpt` | Viewport switches to sculpt mode |
| `:camera front` | Camera snaps to front view |

| Viewport action | TUI effect |
|-----------------|------------|
| Click to select entity | TUI logs `Selected: house_01` |
| Complete transform | TUI logs `Moved house_01 to (12, 0, 5)` |
| Mode switch via hotkey | TUI status bar updates |

### 5.3 Additional TUI Commands

**Save / Sync:**
- `:save force` — flush all pending edits to disk immediately
- `:save status` — show sync state (pending commands, last sync time)

**Undo:**
- `:undo tree` — show branching undo history with orphan branches and
  conflict markers in the TUI

**Paint:**
- `:paint bake` — finalize and bake paint layers to textures
- `:paint 2d` — open the 2D paint panel for the active material
- `:paint 3d` — return to 3D viewport painting

---

## 6. Toolbar

A horizontal toolbar sits between the viewport and the TUI panel (or
along the left edge of the viewport, user-configurable):

```
[Select] [Move] [Rotate] [Scale] | [Mesh] [Sculpt] [Paint] [Weight] | [Play ▶] [Pause ⏸] [Step ⏭] [Reset ⏹]
```

### Tool Groups

| Group | Tools | Description |
|-------|-------|-------------|
| **Transform** | Select, Move, Rotate, Scale | Basic object manipulation |
| **Mode** | Object, Mesh, Sculpt, Paint, Weight, Animation, Terrain | Editor mode switcher |
| **Simulation** | Play, Pause, Step, Reset | Physics simulation controls |
| **Snap** | Magnet icon with dropdown: Position/Rotation/Scale on/off toggles + current grid value per type. Dropdown shows three rows (Pos: ON 0.25, Rot: ON 15°, Scale: ON 0.1). Per-axis granularity via TUI commands | Snap settings |
| **Symmetry** | X, Y, Z mirror toggles | For sculpt/paint tools |

Active tool/mode is highlighted. Hover shows tooltip with keyboard shortcut.

### Spawn Menu

`Ctrl+Shift+A` opens the spawn menu — a popup listing available
prefabs and primitives that can be placed at the 3D cursor. The menu
supports type-ahead filtering and shows a thumbnail preview for each
entry. Select an item and click in the viewport to place it.

---

## 7. Paint and Sculpt Tool UX

### 7.1 Brush Cursor

When a paint or sculpt tool is active, the mouse cursor is replaced with
a brush preview:

- **Circle** showing brush radius on the mesh surface
- **Inner circle** showing falloff boundary
- **Opacity** modulated by current pressure (for tablet hover preview)
- **Normal indicator** — short line showing surface normal at cursor

### 7.2 Brush Settings (Sidebar or Header Bar)

When in paint/sculpt mode, a brush settings bar appears:

```
Radius: [===●=========] 0.25m    Strength: [======●====] 0.7
Falloff: [Smooth ▼]              Symmetry: [X] [ ] [ ]
Mask: [None ▼]                   Channel: [Albedo ▼]
```

- **Radius**: adjustable via slider, `[` / `]` keys, or Ctrl+drag
- **Strength**: adjustable via slider or Shift+drag
- **Falloff**: smooth, linear, sharp, constant, custom curve
- **Mask**: none, face set, color, vertex group, stencil

### 7.3 Sculpt Tools

| Tool | Shortcut | Description |
|------|----------|-------------|
| **Grab** | G | Displace vertices by dragging |
| **Smooth** | Shift (hold) | Average neighboring vertex positions |
| **Flatten** | F | Push/pull vertices toward an average plane |
| **Inflate** | I | Displace vertices along their normals |
| **Crease** | C | Sharpen or soften creases |
| **Pinch** | P | Pull vertices toward brush center |
| **Clay Strips** | T | Add/remove clay-like strips |
| **Draw** | D | Push/pull along view normal |

### 7.4 Paint Tools

| Tool | Shortcut | Description |
|------|----------|-------------|
| **Brush** | B | Paint with color/value |
| **Fill** | Shift+F | Flood fill within mask/face set |
| **Gradient** | G | Linear or radial gradient fill |
| **Clone Stamp** | Alt+C | Clone from a source point |
| **Blur** | Shift+B | Blur/soften painted area |
| **Sharpen** | Alt+S | Sharpen painted area |
| **Eraser** | E | Erase to base layer (or pen eraser end) |

### 7.5 Weight Paint Tools

| Tool | Shortcut | Description |
|------|----------|-------------|
| **Paint Weight** | B | Paint weight value (0–1) |
| **Gradient** | G | Linear/radial weight gradient |
| **Smooth** | Shift (hold) | Smooth weight transitions |
| **Flood** | Shift+F | Set all visible verts to current weight |
| **Sample** | Alt+click | Sample weight under cursor |
| **Normalize** | N | Normalize all weights for selected bone |
| **Mirror** | M | Mirror weights L↔R |

### 7.6 Masking Workflow

1. **Enter mask mode**: `Ctrl+M` or toolbar mask button
2. **Paint mask**: brush paints mask values (0=unmasked, 1=fully masked)
3. **Invert mask**: `Ctrl+I`
4. **Clear mask**: `Alt+M`
5. **Mask by face set**: select face set in inspector, `Ctrl+Shift+M`
6. **Mask by color**: sample color with eyedropper, set threshold
7. **Exit mask mode**: `Ctrl+M` again — mask stays active

Masked areas appear as a dark overlay on the mesh. All subsequent
paint/sculpt operations are modulated by the mask.

### 7.7 2D Paint Panel

The 2D paint panel replaces or splits the 3D viewport to show a flat
UV canvas for the active material.

```
┌─ 2D Paint Panel ──────────────────────────────────────────────────┐
│ ┌─ Layers ───────┐  ┌─ UV Canvas ─────────────────────────────┐  │
│ │ 👁 🔒 Detail   │  │                                         │  │
│ │ 👁    ● Base   │  │   ┌────────┬────────┐                   │  │
│ │ 👁    Overlay   │  │   │        │ painted│                   │  │
│ │                 │  │   │  UV    │ region │                   │  │
│ │ [+ Add Layer]  │  │   │ island │        │                   │  │
│ │                 │  │   └────────┴────────┘                   │  │
│ └─────────────────┘  │                                         │  │
│                      └─────────────────────────────────────────┘  │
│ Tool: [Brush ▼]  Size: [===●===] 24px  Channel: [Albedo ▼]      │
│ Color: [████] #A0522D    Opacity: [======●===] 0.8               │
└───────────────────────────────────────────────────────────────────┘
```

**Navigation:**
- **Scroll wheel** — zoom in/out on canvas
- **Middle-drag** — pan canvas
- **Home** — fit UV islands to view
- **+/-** — zoom in/out in discrete steps

**Interactions:**
- All paint tools from §7.4 are available (brush, fill, gradient, clone,
  blur, sharpen, eraser)
- **Brush size** is in pixels (not world units) in 2D mode
- **UV wireframe** overlay: `Shift+U` to toggle; shows UV island boundaries
- **Tile repeat**: `T` to toggle tiled display for checking seamless
  textures
- **Layer sidebar** mirrors the inspector's layer section; edits sync
  bidirectionally
- **Real-time sync** — strokes in 2D mode appear in the 3D viewport
  immediately and vice versa

**Activation:**
- `:paint 2d` command
- Click [2D] button next to a material in the inspector
- Double-click a material name in the inspector
- **Return to 3D:** `:paint 3d`, click [3D] tab, or press `Escape`

### 7.8 UV Editing (Mesh Mode)

When in Mesh mode, pressing `U` or running `:uv edit` opens a UV editing
sub-panel alongside the viewport. The UV panel shows the active
material's texture with UV wireframe overlaid.

```
┌─ 3D Viewport ───────────────┐ ┌─ UV Editor ─────────────────┐
│                              │ │ ┌──────────────────────────┐│
│   (3D mesh, selected faces   │ │ │  UV islands on texture   ││
│    highlighted)              │ │ │  (selected UVs in white, ││
│                              │ │ │   trimsheet grid shown)  ││
│                              │ │ └──────────────────────────┘│
│                              │ │ Snap: [Grid ▼] 1/16        │
└──────────────────────────────┘ └─────────────────────────────┘
```

**Selection:**
- **Click** — select UV vertex
- **Shift+click** — add to selection
- **Box select** — `B`, then drag
- **Select island** — `L` (linked)
- **Select all** — `A`

**Transform:**
- **G** — grab/move selected UVs
- **R** — rotate selected UVs
- **S** — scale selected UVs
- **Shift+axis** to constrain (e.g., `G`, `Shift+Y` to move along U only)
- All transforms respect UV grid snap when snap is enabled

**Alignment:**
- **Alt+A** — open align menu: top / bottom / left / right / center H / center V
- **Alt+T** — align selection to nearest trimsheet edge (detects
  horizontal/vertical edges in the active trimsheet texture)
- **Ctrl+G** — snap selected UV verts to grid

**Commands:**
- `:uv project <box|planar|cylindrical|sphere>` — auto-project
- `:uv unwrap` — angle-based unwrap
- `:uv pack` — pack UV islands
- `:uv snap grid <size>` — set UV grid resolution (e.g., `1/16` for trimsheets)
- `:uv trimsheet align` — auto-align to trimsheet edges
- `:uv close` — close UV editor panel

### 7.9 Collision Mesh Editing (Mesh Mode)

In Mesh mode, `Shift+C` toggles between editing the render mesh and the
collision mesh. The active target is shown in the toolbar and TUI status
bar.

**Viewport display when editing collision mesh:**
```
┌─ Viewport ──────────────────────────────────────────────────────┐
│                                                                  │
│   ┌─────────────┐   Render mesh shown as semi-transparent ghost  │
│   │  ╱╲  ╱╲    │                                                │
│   │ ╱  ╲╱  ╲   │   Collision mesh drawn as green wireframe,     │
│   │╱    ╱╲   ╲  │   editable with all standard mesh tools       │
│   └─────────────┘                                                │
│                                                                  │
│  [Mesh Target: COLLISION]  Tris: 248                            │
└──────────────────────────────────────────────────────────────────┘
```

**Interactions:**
- All Mesh mode tools work on whichever target is active (extrude,
  bevel, subdivide, delete, merge, etc.)
- When editing the collision mesh, the render mesh is not affected and
  vice versa
- UV editing (`U`) is only available for the render mesh target

**Inspector collision mesh controls:**
- **[Edit]** — switch to Mesh mode with collision mesh target active
- **[Auto]** — generate a simplified collision mesh from the render
  mesh (opens a dialog with target triangle count slider)
- **[Clear]** — remove the collision mesh (entity uses primitive
  collider or no mesh collider)

**Commands:**
- `:mesh target render` — switch to editing render mesh (default)
- `:mesh target collision` — switch to editing collision mesh
- `:mesh collision create` — create collision mesh by copying render mesh
- `:mesh collision from render` — replace collision mesh with copy of
  current render mesh
- `:mesh collision auto <target_tris>` — generate simplified collision
  mesh with ~N triangles
- `:mesh collision clear` — remove the collision mesh
- `:mesh overlay collision on|off` — toggle collision wireframe overlay
  when editing the render mesh

---

## 8. Animation Mode UX

### 8.1 Bone Placement

| Action | Description |
|--------|-------------|
| **Ctrl+click** | Place bone head; drag to set tail |
| **Click existing bone tail** | Start new bone connected to parent |
| **Click bone** | Select bone |
| **E** | Extrude: create child bone from selected tail |
| **Delete** | Delete selected bone(s) |
| **Ctrl+P** | Set parent (select child, Ctrl+P, click parent) |
| **Escape** / **Right-click** | Stop bone creation chain and deselect placement tool |

### 8.2 Constraint Visualization

Active constraints are visualized in the viewport:

| Constraint type | Visualization |
|-----------------|---------------|
| Cone twist | Semi-transparent cone from joint |
| Hinge | Arc showing allowed rotation range |
| Distance | Sphere showing min/max distance |
| IK chain | Dotted line from effector to root |
| Muscle | Lines from origin→insertion, cylinder for wrap surface |
| Twist | Twist arc around axis |
| Ball socket | Sphere at joint position |

Colors:
- Green: within limits
- Yellow: approaching limits (>80% of range)
- Red: at or beyond limits

### 8.3 Timeline Panel

When Animation mode is active, a **timeline panel** appears between the
viewport and TUI:

```
┌─ Timeline ──────────────────────────────────────────────────────────────┐
│ [◀] [▶/⏸] [⏭] [⏹] [⏺]  Frame: [120/300]  FPS: [30]  [🔁 Loop]      │
├── Channel Tree ──┬── Keyframe Sheet ────────────────────────────────────┤
│ ▾ upper_arm.L    │    0    30    60    90   120   150   180   210   240 │
│   Position       │ ───◆──────────────◆────────────────◆──────────      │
│   Rotation       │ ──◆──────────◆────────────◆──────────────────────   │
│   Velocity       │ ─────────────────────◇────────────────────────────  │
│   Damping        │ ──◇─────────────────────────────────────────────    │
│   Muscle Activ.  │ ─────────◇───────────────◇──────────────────────   │
│ ▾ lower_arm.L    │                                                     │
│   Position       │ ──◆──────────────────◆───────────────────────────   │
│   Rotation       │ ─────◆────────────────────◆──────────────────────   │
│   Force          │ ────────────────◇──────────────────────────────     │
│   Mass           │ ──◇─────────────────────────◇────────────────────   │
│ ▸ hand.L         │ (collapsed)                                         │
│ ── Events ────── │                                                     │
│   footstep       │ ────────────────▼──────────────▼──────────────────  │
│   constraint     │ ─────────────────────────✕────────────────────────  │
│   attack_hit     │ ──────────────────────▼───────────────────────────  │
├──────────────────┴─────────────────────────────────────────────────────┤
│ [  ████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░  ]  time selection    │
└────────────────────────────────────────────────────────────────────────┘
```

#### Timeline Interactions

| Input | Action |
|-------|--------|
| **Click on ruler** | Move playhead to frame |
| **Drag on ruler** | Scrub playhead |
| **Shift+drag on ruler** | Set time selection range |
| **Click keyframe ◆/◇** | Select keyframe (shows value in inspector) |
| **Shift+click keyframe** | Add to keyframe selection |
| **Box-drag keyframes** | Select multiple keyframes |
| **G (with keyframes selected)** | Grab-move keyframes in time |
| **S (with keyframes selected)** | Scale keyframes (stretch/compress timing) |
| **Delete (with keyframes selected)** | Delete keyframes |
| **Ctrl+C / Ctrl+V** | Copy/paste keyframes |
| **Right-click on channel** | Channel context menu |
| **Double-click on timeline** | Insert keyframe at playhead for selected channel |
| **Scroll wheel on timeline** | Zoom time scale |
| **Middle-drag on timeline** | Pan time range |

#### Channel Context Menu

```
┌───────────────────────────┐
│ Insert Keyframe       I   │
│ Delete All Keyframes  D   │
│ ─────────────────────────│
│ Interpolation     ►      │
│   ├─ Linear              │
│   ├─ Cubic (Bezier)      │
│   ├─ Step                │
│   └─ Constant            │
│ ─────────────────────────│
│ Bake to Keyframes    B   │
│ Extrapolation     ►      │
│   ├─ Constant            │
│   ├─ Linear              │
│   └─ Cycle               │
└───────────────────────────┘
```

### 8.4 Keyframe Insertion

| Method | Description |
|--------|-------------|
| **I** (in timeline) | Insert keyframe at playhead for all selected channels |
| **Alt+I** | Insert keyframe for all channels of selected bone |
| **Double-click channel at frame** | Insert single-channel keyframe |
| **`:anim key pos 10 0 5`** | Insert position keyframe via command |
| **`:anim key rot 0 45 0`** | Insert rotation keyframe (euler degrees) |
| **`:anim key vel 0 0 -5`** | Insert velocity keyframe |
| **`:anim key force 0 100 0`** | Insert force keyframe |
| **`:anim key mass 2.5`** | Insert mass keyframe |
| **`:anim key muscle flex_activation 0.8`** | Insert muscle activation keyframe |
| **`:anim key damping 0.3`** | Insert joint damping keyframe |
| **`:anim key stiffness 50`** | Insert joint stiffness keyframe |
| **`:anim key limit_x -30 60`** | Insert joint limit keyframe |

Physics keyframes (velocity, force, mass, muscle, damping) display as
**◇ diamonds** to distinguish them from transform keyframes **◆**.

### 8.5 Keyframe Inspector

When a keyframe is selected, the inspector shows its editable values:

```
┌─ Inspector ─────────────────────────────────┐
│ Keyframe: upper_arm.L / Rotation            │
│ Frame: [120]                                │
├─────────────────────────────────────────────┤
│ ▾ Value                                     │
│   X: [0.00°]  Y: [45.00°]  Z: [0.00°]     │
├─────────────────────────────────────────────┤
│ ▾ Interpolation                             │
│   Mode: [Cubic (Bezier) ▼]                  │
│   Left handle:  [-0.3] [0.0]               │
│   Right handle: [0.3]  [0.0]               │
├─────────────────────────────────────────────┤
│ ▾ Physics Context                           │
│   Derived velocity: (0.0, 12.5, 0.0) rad/s │
│   Joint damping at frame: 0.5              │
│   Propagated impulse: 3.2 N·m              │
└─────────────────────────────────────────────┘
```

The **Physics Context** section is unique to our editor — it shows the
velocity that will be derived from this keyframe's displacement and how
it propagates through the constraint graph. This is the primary tool for
tuning animation damping.

### 8.6 Physics-Coupled Playback Controls

| Key | Action |
|-----|--------|
| **Space** | Play / pause simulation from playhead |
| **Right arrow** | Step one physics tick |
| **Shift+Right** | Step 10 ticks |
| **Shift+Space** | Fast-forward (max speed, viewport updates every Nth frame) |
| **Backspace** | Rewind to frame 0 (reset physics, preserve baked keyframes) |
| **Left arrow** | Step back one frame (requires simulation re-run from last checkpoint) |
| **Ctrl+R** | Toggle record mode (capture sim to keyframes) |
| **L** | Toggle time-selection loop |
| **Home / End** | Jump to time selection start / end |

#### Rewind Behavior

Rewinding resets:
- Body positions, velocities, angular velocities → bind pose / frame 0
- Collision forces, contact cache → cleared
- Muscle activations → resting state
- Constraint graph → initial configuration (before any swaps)

Rewinding preserves:
- All keyframes (transform, physics, events)
- Baked keyframe data from recordings
- Constraint swap events (they replay on next play-through)

#### Fast-Forward Modes

- **Synchronous** (`Shift+Space`): viewport renders every 10th frame
- **Async** (`:anim ff --async`): server runs headless, editor shows
  progress bar, results stream back when done

### 8.7 Simulation Baking

Baking converts any simulation into editable keyframes. This works for
skeletal animation (ragdoll, muscle), rigid body physics (falling
objects, explosions), and fractured mesh simulation (building collapse).

**Skeletal baking (record mode):**
1. Enter Animation mode with a skeleton selected
2. Press `Ctrl+R` or `:anim record start` — record indicator appears
   in the toolbar (red dot + "REC")
3. Play simulation normally (Space, step, fast-forward)
4. All bone transforms are captured at each physics tick
5. Press `Ctrl+R` again or `:anim record stop`
6. Baked keyframes appear in the timeline, editable like hand-authored
   keyframes

**Rigid body / fracture baking:**
1. Select entities in Object mode (e.g., fragments of a fractured wall)
2. Run `:anim bake sim` (or `:anim bake sim --range 0 300`)
3. A progress bar appears; the local physics runner simulates the
   selected entities in isolation
4. On completion, position/rotation keyframes are written for each
   entity at each captured frame
5. The timeline shows the baked animation; entities can now be played
   back without re-running the physics solver

**Post-bake cleanup:**
- `:anim bake clean 0.01` — removes keyframes that can be
  reconstructed by interpolation within the given tolerance, reducing
  data size without visible quality loss

**Bake indicator:** During baking, the toolbar shows a progress bar
with frame count: `Baking: 142/300`. The viewport updates periodically
to show simulation progress.

### 8.8 Constraint Swap Events

In the timeline, constraint swap events appear as **✕** markers on a
dedicated `constraint` channel:

| Action | Description |
|--------|-------------|
| **Right-click constraint channel** | Insert constraint swap event at playhead |
| **Click ✕ marker** | Select swap event; inspector shows details |
| **Delete ✕ marker** | Remove constraint swap |

Constraint swap event inspector:

```
┌─ Inspector ─────────────────────────────────┐
│ Constraint Swap Event                       │
│ Frame: [90]   Bone: lower_arm.L             │
├─────────────────────────────────────────────┤
│ ▾ Action                                    │
│   Type: [Replace Joint ▼]                   │
│   ───────────────────────                   │
│   Old: Cone Twist                           │
│   New: [Distance ▼]                         │
│     Rest length: [0.30]                     │
│     Min: [0.0]  Max: [0.5]                  │
│                                             │
│ ▾ Trigger Condition (optional)              │
│   Mode: [At Frame ▼]                        │
│   Alternatives:                             │
│   - [On Collision Impulse > threshold]      │
│   - [On Event "arm_break"]                  │
│   - [On Attribute "health" < value]         │
└─────────────────────────────────────────────┘
```

### 8.9 Gameplay Event Track

The **Events** section of the timeline shows gameplay event triggers:

| Action | Description |
|--------|-------------|
| **Right-click event track** | Insert event at playhead |
| **Click ▼ marker** | Select event; inspector shows name + params |
| **`:anim event "footstep" 15`** | Insert event via command |
| **`:anim bind "on_hit" "flinch"`** | Bind event to animation |

Event inspector:

```
┌─ Inspector ─────────────────────────────────┐
│ Animation Event                             │
│ Frame: [15]                                 │
├─────────────────────────────────────────────┤
│ Name:   [footstep               ]           │
│ Params: [0.8] [0.0] [0.0] [0.0]           │
│                                             │
│ ▾ Event Bindings (this entity)              │
│   "on_hit"  → anim: flinch_upper  [Edit]   │
│   "on_land" → anim: land_impact   [Edit]   │
│   [+ Add Binding]                           │
└─────────────────────────────────────────────┘
```

### 8.10 Muscle Activation Curves

When muscle drive is enabled on a bone, the timeline shows muscle
channels:

```
│ ▾ upper_arm.L        │
│   Flex Activation    │ ──◇───────◇────────◇──────────────────
│   Ext Activation     │ ─────◇───────◇────────◇──────────────
│   Flex Max Force     │ ──◇─────────────────────────────────── (constant unless keyframed)
│   Flex τ Rise        │ ──────────────────────────────────────  (constant)
│   Joint Damping      │ ──◇─────────────◇────────────────────
```

Muscle activation keyframes control the excitation signal `u(t)` over
time. Between keyframes, the activation dynamics ODE produces the actual
activation `a(t)`. The viewport shows muscle force lines colored by
activation level (blue=relaxed, red=max contraction).

### 8.11 Weight Paint Visualization

When weight painting, the mesh is overlaid with a color gradient:

```
Weight: 0.0                     0.5                     1.0
Color:  ████ Dark Blue ████ Cyan ████ Green ████ Yellow ████ Red ████
```

The active bone's influence is shown. Other bones with non-zero weight
on the same vertices are listed in the inspector.

### 8.12 Animation Commands (TUI)

```
:anim key pos 10 0 5              # keyframe position
:anim key rot 0 45 0              # keyframe rotation (euler)
:anim key vel 0 0 -5              # keyframe velocity
:anim key force 0 100 0           # keyframe force
:anim key mass 2.5                # keyframe mass
:anim key muscle flex_activ 0.8   # keyframe muscle activation
:anim key damping 0.3             # keyframe joint damping
:anim key stiffness 50            # keyframe joint stiffness
:anim key limit_x -30 60         # keyframe joint X limits

:anim event "footstep" 15        # insert gameplay event
:anim event "sound:swoosh" 20    # insert namespaced event
:anim swap lower_arm.L 90 distance rest=0.3  # constraint swap at frame 90

:anim bind "on_hit" "flinch"     # bind event to animation
:anim unbind "on_hit"            # remove binding

:anim attr health 0.0 1.0        # animate entity attribute
:anim attr glow 0.0 0.5          # animate glow attribute

:anim play                        # play from playhead
:anim pause                       # pause
:anim stop                        # stop and rewind
:anim step                        # step one tick
:anim step 10                     # step 10 ticks
:anim ff                          # fast-forward (synchronous)
:anim ff --async                  # fast-forward (headless on server)
:anim record start                # start recording sim to keyframes
:anim record stop                 # stop recording

:anim bake sim                    # bake selected entities' physics to keyframes
:anim bake sim --range 0 300      # bake within frame range
:anim bake sim --step 2           # capture every 2nd frame
:anim bake clean 0.01             # remove redundant keyframes within tolerance

:anim range 30 120                # set time selection
:anim loop on|off                 # toggle loop mode
:anim fps 30                      # set playback FPS
:anim frames 300                  # set total frame count
:anim export walk_cycle.fanim     # export clip
:anim import idle.fanim           # import clip
```

### 8.13 Grid Snap Commands (TUI)

```
:snap pos on                      # enable position snap
:snap pos off                     # disable position snap
:snap rot on                      # enable rotation snap
:snap rot off                     # disable rotation snap
:snap scale on                    # enable scale snap
:snap scale off                   # disable scale snap
:snap all on                      # enable snap for all transform types
:snap all off                     # disable snap for all transform types

:snap pos size 0.25               # set position grid size to 0.25 world units
:snap rot size 15                 # set rotation snap angle to 15 degrees
:snap scale size 0.1              # set scale snap increment to 0.1

:snap pos axis x on               # enable position snap on X axis
:snap pos axis y off              # disable position snap on Y axis
:snap pos axis z on               # enable position snap on Z axis
:snap rot axis x off              # disable rotation snap on X axis
:snap scale axis y on             # enable scale snap on Y axis
```

### 8.14 Pivot Commands (TUI)

```
:pivot snap                       # snap pivot to nearest position grid point
:pivot center                     # reset pivot to object bounding box center
:pivot cursor                     # move pivot to current 3D cursor position
:pivot set 1.0 0.0 0.5            # set pivot offset explicitly
```

---

## 9. Keyboard Shortcut Reference

> **Mode-specific shortcuts** (§9.3–§9.7) override global shortcuts when
> that mode is active. For example, `R` means Rotate globally but Raise
> in Terrain mode. To access the global action from within a mode that
> overrides it, use the TUI command (e.g., `:rotate`).

### 9.1 Global (All Panels)

| Key | Action |
|-----|--------|
| `Tab` | Cycle panel focus |
| `Escape` | Focus viewport / cancel current operation |
| `F5` | Toggle TUI panel |
| `F6` | Toggle outliner |
| `F7` | Toggle inspector |
| `F8` | Toggle timeline (Animation mode) |
| `F11` | Toggle fullscreen |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Ctrl+S` | Force sync to disk (`:save force`) |

### 9.2 Viewport — Object Mode

| Key | Action |
|-----|--------|
| `:` | Focus TUI command line |
| `G` | Grab (move) |
| `R` | Rotate |
| `S` | Scale |
| `X` / `Delete` | Delete selection |
| `D` | Duplicate |
| `A` | Select all |
| `Shift+A` | Deselect all |
| `H` | Hide selected |
| `Shift+H` | Show all hidden |
| `F` | Frame selection |
| `1` | Switch to Object mode |
| `2` | Switch to Mesh mode |
| `3` | Switch to Sculpt mode |
| `4` | Switch to Paint mode |
| `5` | Switch to Weight Paint mode |
| `6` | Switch to Animation mode |
| `7` | Switch to Terrain mode |
| `Ctrl+Shift+Tab` | Cycle snap on/off for all transform types |
| `Shift+P` | Enter pivot mode (move pivot instead of object) |
| `Ctrl+Shift+A` | Open spawn menu (prefab browser) |

### 9.3 Viewport — Mesh Mode

> In Mesh mode, `1`/`2`/`3` are reassigned to selection modes (below).
> To switch editor modes, use `:mode <name>` or the toolbar buttons.

| Key | Action |
|-----|--------|
| `U` | Toggle UV editor panel |
| `Shift+C` | Toggle mesh target: render ↔ collision |
| `1` / `2` / `3` | Vertex / Edge / Face select mode (overrides mode switching) |
| (UV editor) `G` | Move selected UVs |
| (UV editor) `R` | Rotate selected UVs |
| (UV editor) `S` | Scale selected UVs |
| (UV editor) `B` | Box select UVs |
| (UV editor) `L` | Select linked (island) |
| (UV editor) `Alt+A` | Align menu |
| (UV editor) `Alt+T` | Align to trimsheet edge |
| (UV editor) `Ctrl+G` | Snap UVs to grid |

### 9.4 Viewport — Sculpt/Paint Mode

| Key | Action |
|-----|--------|
| `[` | Decrease brush radius |
| `]` | Increase brush radius |
| `Shift` (hold) | Smooth tool (temporary) |
| `Ctrl` (hold) | Invert tool (subtract) |
| `Ctrl+M` | Toggle mask mode |
| `Ctrl+I` | Invert mask |
| `Alt+M` | Clear mask |
| `X / Y / Z` | Toggle symmetry axis |

### 9.5 Viewport — Animation Mode

| Key | Action |
|-----|--------|
| `E` | Extrude bone |
| `Ctrl+click` | Place bone |
| `Ctrl+P` | Set parent |
| `Space` | Play/pause simulation |
| `Shift+Space` | Fast-forward simulation |
| `Right` | Step one physics tick |
| `Shift+Right` | Step 10 ticks |
| `Left` | Step back (re-sim from checkpoint) |
| `Backspace` | Rewind to frame 0 |
| `Home` / `End` | Jump to time selection start / end |
| `I` | Insert keyframe at playhead |
| `Alt+I` | Insert keyframes for all channels on bone |
| `Ctrl+R` | Toggle record mode |
| `L` | Toggle time selection loop |
| `Ctrl+click bone` (during sim) | Apply impulse force |
| `Ctrl+drag bone` (during sim) | Spring-drag to cursor |
| Drag bone (during sim) | Kinematic drag (move bone directly) |
| Right-click bone (during sim) | Context menu with "Pin" to freeze bone in place |

### 9.6 Viewport — Terrain Mode

> In Terrain mode, `R`, `S`, `F`, `E` override their global meanings.
> To rotate in Terrain mode, use `:rotate` or switch to Object mode.
> Number keys `1`–`7` retain their global mode-switching behavior.

| Key | Action |
|-----|--------|
| `R` | Raise tool |
| `L` | Lower tool |
| `S` | Smooth tool |
| `F` | Flatten tool |
| `T` | Stamp tool |
| `E` | Erode tool |
| `P` | Paint material tool |
| `[` / `]` | Decrease / increase brush radius |
| `Shift` (hold) | Smooth (temporary override) |

### 9.7 Viewport — Weight Paint Mode

| Key | Action |
|-----|--------|
| `B` | Paint weight brush |
| `G` | Gradient (linear/radial) |
| `Shift` (hold) | Smooth weights |
| `Shift+F` | Flood fill |
| `N` | Normalize |
| `M` | Mirror L↔R |
| `[` / `]` | Decrease / increase brush radius |

---

## 10. Color Scheme and Visual Language

### 10.1 Panel Colors

| Element | Color |
|---------|-------|
| Panel background | Dark gray (#1E1E1E) |
| Panel border | Medium gray (#3C3C3C) |
| Active panel border | Accent blue (#3D7EDB) |
| Text | Light gray (#D4D4D4) |
| Headings | White (#FFFFFF) |
| Input fields | Darker gray (#252526) |
| Active input | Dark with blue border |
| Button | Medium gray (#3C3C3C), lighter on hover |
| Selection highlight | Blue (#264F78) |
| Error text | Red (#F44747) |
| Warning text | Yellow (#CCA700) |

### 10.2 Viewport Overlays

| Overlay | Color |
|---------|-------|
| Selected object outline | Orange (#FF8C00) |
| Active object outline | White (#FFFFFF) |
| 3D cursor | Yellow crosshair |
| Grid | Dark gray, lighter at origin |
| X axis | Red |
| Y axis | Green |
| Z axis | Blue |
| Joint limits (within) | Green, semi-transparent |
| Joint limits (exceeded) | Red, semi-transparent |
| Muscle attachment lines | Cyan |
| Muscle wrap cylinders | Magenta, wireframe |
| Other editor cameras | Light blue frustum wireframe |
| Locked (exclusive) outline | Amber (#FFA500), dashed |
| Frozen outline | Ice blue (#87CEEB), solid, with padlock icon |
| Keyframe marker (transform) | Yellow diamond ◆ |
| Keyframe marker (physics) | Cyan diamond ◇ |
| Event marker | Red triangle ▼ |
| Constraint swap marker | Red ✕ |
| Timeline playhead | White vertical line |
| Time selection | Blue highlight band |
| Derived velocity vector | Orange arrow from bone |
| Muscle activation (low) | Blue fiber lines |
| Muscle activation (high) | Red fiber lines |

---

## 11. Workflow Examples

### 11.1 Place and Paint a Building

1. `Ctrl+Shift+A` → spawn menu → select `prefab: house_01`
2. Move mouse to position in viewport, click to place
3. `G` → adjust position → `Enter` to confirm
4. Select house → press `4` (Paint mode)
5. Inspector: select material slot → click `[Paint]`
6. Use brush tool to paint weathering details onto walls
7. `:paint bake` to finalize textures

### 11.2 Rig and Animate a Character

1. Import mesh: `:import assets/meshes/character.glb`
2. Press `6` (Animation mode)
3. `Ctrl+click` to place first bone (pelvis)
4. `E` to extrude spine, chest, neck, head
5. Select chest tail → `E` to extrude shoulder → upper arm → lower arm → hand
6. Select each bone → Inspector: set joint type and limits
7. Inspector: enable muscle drive on limb bones, tune max force and activation
8. `5` (Weight Paint mode) → paint weights per bone
9. `6` → `Space` to run simulation and verify ragdoll
10. `:anim frames 120` → set 120-frame clip
11. Move to frame 0 → pose character → `I` to insert keyframes
12. Move to frame 30 → pose punch position → `I` to keyframe
13. `:anim key damping 0.3` on shoulder joint (tune propagation to forearm)
14. `:anim key muscle flex_activ 0.9` on upper arm (contract bicep for punch)
15. `Space` to play — watch physics-driven forearm follow through
16. Adjust damping keyframe until forearm motion looks right
17. `:anim event "attack_hit" 22` → mark the impact frame
18. `:anim swap lower_arm.L 45 distance rest=0.3` → arm breaks on big hit
19. `Ctrl+R` → record a full physics sim pass → bake to keyframes
20. `:anim export punch.fanim`

### 11.3 Collaborative Scene Editing

1. Editor A: `:connect 192.168.1.10:9100`
2. Editor B: `:connect 192.168.1.10:9100`
3. Both editors see each other's camera (blue frustum wireframes)
4. Editor A places terrain and buildings (Layer: Static Geo)
5. Editor B places lights and props (Layer: Lights, Layer: Props)
6. Both editors see each other's changes in real-time
7. Editor A: `:lock --group Buildings` → exclusive edit access
8. Editor B sees amber lock icon in outliner, cannot edit buildings
9. Editor A: `:unlock --group Buildings` → releases lock
10. Editor B: `:freeze --layer Terrain` → nobody can edit terrain
11. Both editors see ice-blue padlock on terrain items
12. Editor B: `:unlock --layer Terrain` → terrain editable again
13. `:locks` → shows all active locks with holder names

### 11.4 Bake a Building Collapse

1. Place a building in the scene (`Ctrl+Shift+A` → prefab: tower_01)
2. `:fracture tower_01 --mode voronoi --pieces 30` → fracture mesh into 30 rigid body fragments
3. Outliner now shows `tower_01/` with 30 child fragment entities
4. Select all fragments (`A` in outliner)
5. Press `6` (Animation mode)
6. Set frame range: `:anim frames 180`
7. Optionally keyframe an initial force: select top fragments, `:anim key force 500 0 -200` at frame 5
8. `:anim bake sim --range 0 180` → local physics runner simulates the collapse
9. Progress bar: "Baking: 45/180" — viewport shows fragments falling
10. On completion, each fragment has position/rotation keyframes in the timeline
11. `:anim bake clean 0.05` → reduce keyframe count (~60% reduction typical)
12. Play back with `Space` — deterministic replay without physics solver
13. Adjust specific fragments manually if needed (select fragment, edit keyframes)
14. `:anim export tower_collapse.fanim`

### 11.5 Sculpt Terrain with Tablet

1. Press `7` (Terrain mode)
2. Pick up Wacom pen
3. Select Raise tool (or press `R`)
4. Set radius with `[`/`]`, adjust strength via toolbar
5. Draw strokes on terrain — pressure controls displacement amount
6. Flip pen to eraser end → automatically switches to Smooth tool
7. Smooth out harsh transitions
8. Switch to Stamp tool → select heightmap texture → stamp rock formations
9. Switch to Paint → paint terrain material layers (grass, rock, dirt)
10. All edits batch-flush to server every 250ms during strokes
