# Scene Editor Implementation Design

Multi-phase implementation plan for the scene editor described in
`ref/scene_editor_spec.md` and `ref/scene_editor_ux.md`.

Each phase produces a working, testable milestone. Later phases depend
on earlier ones but can overlap when dependencies are met.

---

## Phase Overview

| Phase | Name | Deliverable | Depends on |
|-------|------|-------------|------------|
| **0** | Foundation | SDL2 window, panel layout, embedded TUI, server connection | Existing client + server |
| **1** | Core Editing | Viewport rendering, outliner, inspector, object mode, selection, transforms, gizmos | Phase 0 |
| **2** | Undo & Collaboration | Branching undo, edit locking, multi-editor sync, presence | Phase 1 |
| **3** | Mesh & Sculpt | Mesh editing mode, sculpt mode, brush engine, tablet input | Phase 1 |
| **4** | Paint | Texture painting, weight painting, masking system | Phase 3 |
| **5** | Animation — Rigging | Bone placement, constraint/joint setup, musculature, inline sim | Phase 1 |
| **6** | Animation — Timeline & Keyframes | Timeline panel, keyframe channels, physics-coupled playback | Phase 5 |
| **7** | Animation — Events & Advanced | Constraint swaps, gameplay events, attr modifiers, event bindings | Phase 6 |
| **8** | Terrain | Page-based terrain, terrain tools, splatmap painting | Phase 3, 4 |
| **9** | Streaming & Scale | Disk swap, LOD streaming, async loading, large scene support | Phase 1 |
| **10** | Polish & Extensions | Smear frames, MCP integration, scripting, asset preview | Phase 6+ |

### Milestone Verification Policy

Every phase ends with a **visual integration test** — a fully
interactive editor session exercising every feature listed in that
phase's milestone criteria. This is not a headless unit test; it is a
real window with real rendering, real input, and a real server
connection. The visual test:

1. **Launches the scene editor** connected to a test server with
   pre-populated test data (entities, meshes, skeletons, terrain as
   appropriate for the phase).
2. **Exercises every milestone bullet** via scripted input sequences
   and/or manual interaction. Each feature must be visually confirmed
   working — not just "doesn't crash" but "produces correct visible
   output and responds to input correctly."
3. **Is recorded** as a named test entry (e.g., `visual/v0_foundation`,
   `visual/v1_core_editing`) that can be re-run at any time to detect
   regressions.
4. **Accumulates** — each phase's visual test includes all prior phases'
   checks. The Phase 3 visual test also verifies Phase 0, 1, and any
   other completed phases still work.

A phase is not considered complete until its visual test passes with
all milestone features functional and visually correct.

---

## Phase 0 — Foundation

**Goal:** A window opens, shows four panels, the TUI accepts commands and
talks to a running game server. Nothing renders in the viewport yet
except a solid background color and the grid.

### 0.1 SDL2 Window and Panel Layout

```
src/editor/scene/
├── scene_main.c            # Entry point, SDL2 init, main loop
├── scene_main.h
├── scene_panel.c           # Panel layout engine (rects, dividers, resize)
├── scene_panel.h           # panel_layout_t, panel_id_t
├── scene_input.c           # SDL2 event dispatch to focused panel
├── scene_input.h
├── snap_state.c            # Grid/snap state: per-transform enable, grid sizes, per-axis toggles
└── snap_state.h            # snap_state_t (local to editor instance, not replicated)
```

**Key types:**
- `scene_editor_t` — top-level context: window, GL context, Clay context, panels, connections, snap state
- `panel_layout_t` — four panel rects + divider positions, resize state
- `snap_state_t` — per-transform-type snap enable, grid sizes, per-axis toggles (local to this editor instance, not synced to server)

**Work items:**
1. SDL2 window creation with OpenGL 4.6 context
2. Panel layout system: four rectangular regions, draggable dividers
3. Panel focus tracking (click-to-focus, Tab cycle, Escape→viewport)
4. Layout persistence (save/load divider positions to config file)
5. F5/F6/F7/F8/F11 panel toggles
6. Main loop: poll SDL events → dispatch to panels → render panels → swap

### 0.1.1 UI Rendering with Clay

All editor UI elements — panel chrome, outliner tree, inspector widgets,
toolbar buttons, brush settings bars, timeline ruler, layer sidebar, and
2D paint panel controls — are rendered using **Clay** (`extern/clay/`),
a single-header C layout library with zero dependencies and zero per-frame
allocations.

Clay does **not** draw anything itself. It computes layout and produces a
sorted array of render commands (`Clay_RenderCommand`). We implement a
thin OpenGL renderer backend that consumes these commands.

```
src/editor/ui/
├── clay_backend.c          # OpenGL renderer for Clay render commands
├── clay_backend.h          # clay_backend_t (GL resources: VAO, shaders, font atlas)
├── clay_fonts.c            # Font loading, glyph atlas, text measurement callback
├── clay_fonts.h            # clay_font_set_t
└── clay_theme.c/.h         # Color palette, spacing, font size constants
```

**Architecture:**

```
 SDL2 events                        Clay layout                    OpenGL
 ───────────►  scene_input.c  ───►  Clay_BeginLayout()
               (translate to         CLAY() { ... }         ───►  clay_backend.c
                Clay pointer/        Clay_EndLayout()              (iterate commands,
                scroll state)        → RenderCommandArray          emit GL draw calls)
```

**Integration pattern (per frame):**
```c
/* 1. Initialize once at startup */
uint64_t mem_size = Clay_MinMemorySize();
Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(
    mem_size, arena_alloc(&editor->arena, mem_size));  /* Use engine arena, not malloc */
editor->clay_ctx = Clay_Initialize(clay_arena, screen_dims, error_handler);

/* 2. Set text measurement callback (uses our glyph atlas) */
Clay_SetMeasureTextFunction(clay_measure_text, &editor->font_set);

/* 3. Each frame */
Clay_SetPointerState(mouse_pos, mouse_down);
Clay_SetLayoutDimensions(window_dims);
Clay_BeginLayout();

/* Declare UI hierarchy using Clay macros */
panel_outliner_declare(editor);   /* outliner tree */
panel_inspector_declare(editor);  /* inspector widgets */
panel_toolbar_declare(editor);    /* toolbar buttons */
panel_tui_declare(editor);        /* TUI text */
/* ... */

Clay_RenderCommandArray cmds = Clay_EndLayout();

/* 4. Render: 3D viewport first (raw GL), then Clay UI on top */
viewport_render_3d(editor);
clay_backend_render(&editor->clay_backend, cmds);
```

**Font rendering:**
- Load a TTF font (e.g., Fira Mono for TUI, Inter or similar for UI
  labels) at startup into a GPU glyph atlas
- Provide `Clay_SetMeasureTextFunction()` callback that returns glyph
  widths from the atlas for layout calculation
- `clay_backend.c` renders `CLAY_RENDER_COMMAND_TYPE_TEXT` commands using
  textured quads from the glyph atlas
- Multiple font sizes supported via separate atlas entries or SDF rendering

**Render command types handled by `clay_backend.c`:**

| Clay command | GL implementation |
|---|---|
| `RECTANGLE` | Colored quad (optional corner radius via SDF shader) |
| `TEXT` | Textured quads from glyph atlas |
| `IMAGE` | Textured quad (icon atlas or material thumbnails) |
| `BORDER` | Line quads or rectangle outlines |
| `SCISSOR` | `glScissor()` for clipping scrollable regions |
| `CUSTOM` | Callback for embedded GL content (viewport FBO, 2D canvas) |

**Viewport embedding:** The 3D viewport and 2D paint canvas are not Clay
elements — they render directly via OpenGL. Clay lays out the surrounding
chrome; the viewport occupies the remaining rectangle. `CUSTOM` render
commands can embed sub-viewports (e.g., the UV editor split view) by
specifying a blit region for an FBO.

**Memory:** Clay's arena is pre-allocated at startup from the engine's
own arena allocator — no malloc/free during layout. ~3.5 MB covers
~8,192 layout elements, which is more than sufficient for the editor UI.

**Dependency:** `extern/clay/` (git submodule). Single-header include:
```c
#define CLAY_IMPLEMENTATION
#include "clay.h"
```

### 0.2 Embedded TUI Panel

```
src/editor/panels/
├── panel_tui.c             # TUI panel: text rendering, scroll, input
├── panel_tui.h             # panel_tui_t
├── panel_tui_render.c      # Glyph atlas rendering for TUI text
└── panel_tui_render.h
```

Reuse the existing controller logic (`ref/editor_design.md` §1
controller/) but render via Clay instead of a terminal emulator.
The TUI panel needs:

1. Text buffer (scrollback ring buffer, colored spans)
2. Clay-based text rendering: each line is a `CLAY_TEXT()` element using
   the monospace font from `clay_fonts.c`; colored spans use per-span
   `Clay_TextElementConfig` with different color configs
3. Scrollback: Clay `CLAY_SCROLL()` container wrapping the text buffer
4. Command-line input with cursor, selection, tab completion (rendered as
   a Clay text input element at the bottom of the panel)
5. Status bar (top of TUI panel, Clay row with labels)
6. Key routing: when TUI has focus, all keys go to TUI input handler

### 0.3 Server Connection

```
src/editor/scene/
├── scene_connection.c      # TCP + UDP connection to server
└── scene_connection.h      # scene_connection_t
```

1. TCP connection to server edit socket (reuse `ctrl_connection.c` pattern)
2. UDP connection for replication snapshots (reuse existing client net code)
3. Command send/receive (JSON over TCP)
4. Entity state receive (snapshots over UDP)
5. Connection status in TUI status bar

### 0.4 Persistence & Sync

```
src/editor/scene/
├── scene_sync.c            # Sync status tracking, command queue, auto-reconnect
└── scene_sync.h            # sync_state_t
```

This project's save model is like Google Drive: every edit syncs to the
server immediately. The server auto-saves to disk. The user sees sync
status in the TUI status bar and can force a flush with `:save force`.

**Work items:**
1. Track in-flight edit commands and server acknowledgments
2. TUI status bar sync indicator: "Synced (12:34:05)" or "Syncing... (3 pending)"
3. `:save force` command: send flush request to server
4. `:save status` command: show pending count, last sync time, connection health
5. Offline queue: buffer edits during disconnect, replay on reconnect
6. Ctrl+S mapped to `:save force`

### 0.5 Tests

- `tests/p200_panel_layout_tests.c` — panel rect computation, divider drag, collapse
- `tests/p201_tui_text_buffer_tests.c` — scrollback ring, colored spans, line wrap
- `tests/p200b_clay_backend_tests.c` — Clay initialization, render command iteration, font measurement callback
- Manual: window opens, panels resize, TUI accepts `:help`, connects to server

### 0.6 Milestone Criteria

- Window opens with four colored panel rectangles
- Clay UI renders: panel chrome, toolbar, outliner placeholder, inspector placeholder
- Font atlas loads; text is measurable and renderable via Clay backend
- Dividers are draggable; layout persists across restarts
- TUI panel renders text, accepts commands, sends to server, shows responses
- Panel focus cycling works (Tab, click, Escape)
- Sync status shows in TUI status bar; `:save force` and `:save status` work
- Offline queue buffers edits during disconnect and replays on reconnect

**Visual test (`visual/v0_foundation`):** Launch editor connected to
test server. Verify four panels render, dividers drag, TUI accepts
commands and shows responses, sync indicator updates, panel focus
cycles correctly.

---

## Phase 1 — Core Editing

**Goal:** Object mode works — you can see entities in the viewport,
select them, move/rotate/scale with gizmos, see them in the outliner,
edit properties in the inspector.

### 1.1 Viewport Rendering

```
src/editor/panels/
├── panel_viewport.c        # Viewport panel: camera, render, overlays
├── panel_viewport.h        # panel_viewport_t
├── viewport_camera.c       # Editor camera: orbit, pan, zoom, snap views
├── viewport_camera.h       # editor_camera_t
├── viewport_grid.c         # Infinite grid rendering
├── viewport_grid.h
├── viewport_gizmo.c        # Transform gizmos (move/rotate/scale)
├── viewport_gizmo.h        # gizmo_state_t
├── viewport_overlay.c      # Selection outlines, cursor, axis indicators
└── viewport_overlay.h
```

The viewport renders into a framebuffer the size of its panel rect.
Rendering reuses the existing client renderer (`static_mesh_t`,
`skeletal_mesh_t`, shaders) — the scene editor links against the same
render code. Entities come from the replication snapshot.

**Work items:**
1. Camera controls: orbit (middle mouse), pan (shift+middle), zoom (scroll)
2. Snap views (1=front, 3=right, 7=top, 5=ortho toggle, 0=reset), frame selection (F)
3. Grid rendering (XZ plane, infinite, fading with distance)
4. 3D cursor (wireframe crosshair with axis colors)
5. Entity rendering from replication snapshot
6. Selection outline (orange outline shader or stencil pass)
7. Transform gizmos (three arrow axes + rings + cubes, hit-test, drag)

### 1.2 Outliner Panel

```
src/editor/panels/
├── panel_outliner.c        # Outliner tree view
├── panel_outliner.h        # panel_outliner_t
├── outliner_tree.c         # Tree data model (layers, groups, entities)
└── outliner_tree.h         # outliner_node_t
```

The outliner is rendered entirely via Clay. Each tree node is a Clay row
with indentation, expand/collapse toggle, icon, and label. Scrolling uses
`CLAY_SCROLL()`. Selection highlight is a colored `RECTANGLE` behind the
active row.

**Work items:**
1. Tree data model built from server entity list
2. Expand/collapse nodes: Clay rows with `CLAY_FLOATING()` for indent
3. Click to select (single, shift-range, ctrl-toggle)
4. Visibility (eye) and residency (disk) toggle icons (Clay image elements).
   Disk swap toggle is rendered but disabled (stub) until Phase 9
5. Filter text input at top (Clay text input widget)
6. Right-click context menu (rename, delete, group, layer, lock, freeze) —
   Clay floating container positioned at click point
7. Drag-to-reparent
8. Double-click to rename inline

### 1.3 Inspector Panel

```
src/editor/panels/
├── panel_inspector.c       # Inspector: property editing
├── panel_inspector.h       # panel_inspector_t
├── inspector_widgets.c     # Float field, vec3, dropdown, checkbox, color
├── inspector_widgets.h     # Immediate-mode widget types
├── clay_context_menu.c     # Shared context menu widget (outliner, timeline, events)
└── clay_context_menu.h     # Clay floating container at click point, dismiss on click-outside or Escape
```

The inspector is rendered via Clay. Each property section is a
collapsible Clay container. Widgets (sliders, dropdowns, text fields,
color pickers) are Clay elements with custom render commands where needed
(e.g., color picker hue wheel uses a `CUSTOM` command to draw via GL).

**Work items:**
1. Clay-based widget library: float/int fields, vec3/quat (euler) rows,
   dropdown menus, checkboxes, text fields, color picker, collapsible
   sections — all composed from Clay layout primitives
2. Entity inspector: transform, mesh info, physics, materials, layer
   stack (§4.1), attributes.
   Layer stack section appears when texture layer system is available (Phase 4); hidden until then
3. Multi-select inspector: shared values, mixed-value indicator
4. Property edits send commands to server
5. Collapsible sections with persistence

### 1.4 Object Mode

```
src/editor/modes/
├── mode_object.c           # Object mode event handling
├── mode_object.h           # mode_object_t
├── mode_manager.c          # Mode switching, current mode state
└── mode_manager.h          # mode_id_t, mode_vtable_t
```

**Mode vtable pattern:** each mode implements:
```c
typedef struct mode_vtable {
    void (*enter)(scene_editor_t *ed);
    void (*exit)(scene_editor_t *ed);
    void (*on_key)(scene_editor_t *ed, SDL_KeyboardEvent *e);
    void (*on_mouse)(scene_editor_t *ed, SDL_MouseButtonEvent *e);
    void (*on_motion)(scene_editor_t *ed, SDL_MouseMotionEvent *e);
    void (*on_scroll)(scene_editor_t *ed, SDL_MouseWheelEvent *e);
    void (*draw_overlay)(scene_editor_t *ed);
    void (*draw_inspector)(scene_editor_t *ed);
} mode_vtable_t;
```

**Object mode implements:**
1. Left-click select (raycast), shift-click add/remove, box select
2. Select all (A), deselect all (Shift+A)
3. G/R/S for grab/rotate/scale → interactive transform with axis constraint
4. X/Delete for delete
5. Duplicate (D)
6. Hide selected (H), show all (Shift+H)
7. Ctrl+right-click to place 3D cursor
8. Transform gizmo interaction (axis click-drag)
9. Grid snap: quantize transforms to snap_state grid when enabled
10. Pivot manipulation: Alt+G enters pivot mode (move pivot, not object);
    `:pivot snap/center/cursor/set` commands; pivot stored server-side as entity data

### 1.5 Selection System

```
src/editor/scene/
├── scene_selection.c       # Selection set management
└── scene_selection.h       # selection_set_t
```

1. Selection set: sparse set of entity IDs
2. Raycast pick: project mouse into world, intersect entity bounding volumes
3. Box select: frustum from screen rect, test all entities
4. Selection sync: TUI `:select` commands ↔ viewport click selections

### 1.6 Toolbar

```
src/editor/panels/
├── panel_toolbar.c         # Toolbar rendering and interaction
└── panel_toolbar.h
```

1. Horizontal strip of icon buttons between viewport and TUI
2. Tool groups: transform, mode, simulation, snap, symmetry.
   Mode buttons for unavailable modes (Mesh, Sculpt, Paint, Weight, Animation, Terrain) are rendered but disabled/grayed until their phase is implemented
3. Active state highlight, hover tooltips
4. Click to activate tool/mode
5. Snap dropdown: magnet icon opens dropdown with Position/Rotation/Scale
   rows, each showing on/off toggle and current grid value; clicking toggles
   snap for that transform type; per-axis control via TUI `:snap` commands

### 1.7 Tests

- `tests/p202_viewport_camera_tests.c` — orbit, pan, zoom math, snap views
- `tests/p203_gizmo_hit_tests.c` — axis/ring/cube hit testing
- `tests/p204_selection_raycast_tests.c` — ray-AABB, ray-sphere, box-frustum
- `tests/p205_outliner_tree_tests.c` — tree build, filter, reparent
- `tests/p206_inspector_widget_tests.c` — value edit, multi-select merge
- `tests/p206a_snap_state_tests.c` — grid quantization, per-axis toggles, negative coords, near-zero snap
- `tests/p206b_pivot_tests.c` — pivot offset, snap pivot to grid, reset to center, pivot to cursor
- Manual: spawn entities via TUI, see them render, select, move with gizmo

### 1.8 Milestone Criteria

- Entities from server visible in viewport with proper transforms
- Camera orbit/pan/zoom, snap views working
- Click-to-select, box select, gizmo grab/rotate/scale
- Outliner shows entity hierarchy, click selects, filter works
- Inspector shows selected entity properties, edits send to server
- Mode switching (only Object mode implemented, others show placeholder)

**Visual test (`visual/v1_core_editing`):** Launch editor with test
server containing ~20 entities. Orbit/pan/zoom camera, snap to front
view. Click-select entities, box-select, multi-select. Move/rotate/
scale with gizmos and verify transforms sync to server. Verify outliner
tree, inspector edits, filter, snap dropdown. Includes all Phase 0
checks.

---

## Phase 2 — Undo & Collaboration

**Goal:** Branching undo/redo works. Multiple editor instances can
connect, see each other, lock objects, and collaborate without conflicts.

### 2.1 Undo System

```
src/editor/undo/
├── undo_stack.c            # Branching undo stack
├── undo_stack.h            # undo_stack_t, undo_record_t
├── undo_rebase.c           # Conflict detection and rebase logic
├── undo_rebase.h
├── undo_conflict.c         # Conflict key computation
└── undo_conflict.h
```

**Work items:**
1. `undo_stack_t`: doubly-linked list of `undo_record_t`, HEAD pointer
2. Push/pop: each edit creates a record with forward + inverse payloads
3. Undo: apply inverse, move HEAD backward
4. Redo: apply forward, move HEAD forward
5. Branching: when editing after undo, attempt rebase of displaced records
6. Conflict detection: compare conflict keys (entity ID, vertex range, etc.)
7. Rebase logic: for each displaced record, if no conflict with new edit,
   transform and append; if conflict, move to orphan branch
8. `:undo tree` command to display branch structure
9. Orphan branch recovery: `:undo recover <branch_id>`

### 2.2 Edit Locking

```
src/editor/collab/
├── lock_manager.c          # Client-side lock tracking
├── lock_manager.h          # lock_entry_t, lock_manager_t
├── lock_protocol.c         # Lock command serialization
└── lock_protocol.h
```

Server-side (in `src/editor/commands/`):
```
├── cmd_lock.c              # lock, freeze, unlock command handlers
```

**Work items:**
1. `lock_manager_t`: hash map of target → lock state (mode, holder, timeout)
2. `:lock`, `:freeze`, `:unlock` commands → TCP messages
3. Server-side lock table: validate mutations against locks, reject unauthorized
4. `lock_notify` broadcast: server pushes lock changes to all editors
5. Lock visualization in outliner (amber for exclusive, ice-blue for freeze)
6. Lock visualization in viewport (dashed outline for locked objects)
7. Timeout handling: server auto-expires exclusive locks after timeout
8. `:locks` command: list all active locks
9. `:unlock --force` with warning broadcast

### 2.3 Multi-Editor Sync

```
src/editor/collab/
├── collab_sync.c           # Remote edit application, presence broadcast
├── collab_sync.h           # collab_state_t
├── collab_presence.c       # Camera/cursor position broadcast
└── collab_presence.h
```

**Work items:**
1. `editor_id` assignment at connection time
2. Edit broadcast receive: apply remote mutations to local state
3. Conflict resolution for unlocked targets (last-write-wins)
4. Presence broadcast: send camera position/rotation at 2Hz
5. Presence rendering: colored frustum wireframes + editor name labels
6. Remote cursor rendering: 3D cursor icons for other editors

### 2.4 Tests

- `tests/p207_undo_stack_tests.c` — push, pop, undo, redo, linear
- `tests/p208_undo_rebase_tests.c` — conflict detection, rebase, orphan
- `tests/p209_lock_manager_tests.c` — lock, freeze, unlock, timeout, force
- `tests/p210_collab_sync_tests.c` — remote edit apply, presence, conflict
- Manual: two editor instances, lock an object in one, verify rejection in other

### 2.5 Milestone Criteria

- Ctrl+Z/Ctrl+Shift+Z undo/redo working for all object mode operations
- Undo after undo + new edit → displaced ops rebased or orphaned
- `:undo tree` shows branch structure
- Two editor instances see each other's cameras in viewport
- Lock/freeze prevents edits from other editors, shown in outliner
- Unlocked edits from editor B appear in editor A in real-time

**Visual test (`visual/v2_undo_collab`):** Two editor instances
connected to the same server. Editor A moves an object, Editor B sees
it. Editor A locks an object, Editor B attempts edit and gets rejection.
Undo/redo a sequence of edits, verify rebase. `:undo tree` shows
branches. Presence indicators (camera frustums) visible. Includes all
prior phase checks.

---

## Phase 3 — Mesh & Sculpt

**Goal:** Mesh editing mode (vertex/edge/face) and sculpt mode with
tablet support.

### 3.1 Mesh Mode

```
src/editor/modes/
├── mode_mesh.c             # Mesh editing mode
├── mode_mesh.h
├── mesh_select.c           # Vertex/edge/face selection
├── mesh_select.h
├── mesh_tools.c            # Extrude, bevel, subdivide, loop cut, etc.
├── mesh_tools.h
├── collision_mesh.c        # Collision mesh target management, auto-generate
├── collision_mesh.h        # collision_mesh_state_t
├── collision_mesh_vis.c    # Collision wireframe overlay, ghost render mesh
└── collision_mesh_vis.h
```

Reuses the mesh editing subsystem from `ref/mesh_modeling_spec.md`
(server-side `mesh_edit_t`). The editor sends mesh commands and
receives mesh snapshots. All mesh tools operate on whichever target
(render or collision) is currently active.

**Work items:**
1. Selection mode toggle: vertex, edge, face, polygroup
2. Element selection: click, box select, loop select, linked select
3. Mesh tools: extrude, bevel, inset, subdivide, loop cut, merge
4. Mesh tool gizmos (extrude direction, bevel width)
5. Server round-trip: send mesh command → receive updated mesh → re-upload VBO
6. Collision mesh target toggle: `Shift+C` or `:mesh target collision`
   switches all mesh editing tools to operate on the collision mesh
7. Collision mesh visualization: when editing collision mesh, render
   mesh shown as semi-transparent ghost; collision mesh as green
   wireframe. When editing render mesh, collision mesh wireframe overlay
   toggled via `:mesh overlay collision on|off`
8. Collision mesh creation: `:mesh collision create` copies render mesh;
   `:mesh collision from render` replaces collision mesh from render mesh
9. Collision mesh auto-generation: `:mesh collision auto <N>` generates
   a simplified hull or decimated mesh with ~N triangles (uses iterative
   edge-collapse decimation)
10. Collision mesh clear: `:mesh collision clear` removes collision mesh
11. Inspector integration: Physics section shows collision mesh info
    with [Edit], [Auto], [Clear] buttons

### 3.2 Sculpt Mode

```
src/editor/modes/
├── mode_sculpt.c           # Sculpt mode event handling
└── mode_sculpt.h

src/editor/brush/
├── brush_engine.c          # Unified brush state and evaluation
├── brush_engine.h          # brush_state_t, brush_params_t
├── brush_falloff.c         # Falloff curves (smooth, linear, sharp, custom)
├── brush_falloff.h
├── brush_mask.c            # Masking: face set, color, vertex group, stencil
└── brush_mask.h            # mask_state_t
```

**Work items:**
1. `brush_engine_t`: maintains brush state, evaluates per-vertex influence
2. Sculpt tools: grab, smooth, flatten, inflate, crease, pinch, clay strips, draw
3. Brush cursor rendering (circle on surface, falloff inner ring, normal line)
4. Radius/strength adjustment: `[`/`]` keys, Ctrl+drag, Shift+drag
5. Symmetry: X/Y/Z mirror planes
6. Local vertex delta cache: accumulate during stroke
7. Batch flush to server on stroke end
8. Masking system: face set, color, vertex group, texture, stencil masks

### 3.3 Tablet Input

```
src/editor/input/
├── tablet_input.c          # Wacom/pen input abstraction
└── tablet_input.h          # tablet_state_t
```

**Work items:**
1. SDL2 pen/tablet event handling (pressure, tilt, eraser, barrel rotation)
2. Pressure curve mapping (linear, ease-in, ease-out, custom)
3. Eraser end detection → auto-switch to smooth tool
4. Tilt → brush angle bias
5. Hover preview (brush circle at hover position before pen-down)
6. Fallback path: mouse input with pressure=1.0, tilt=0

### 3.4 UV Editor

```
src/editor/mesh/uv/
├── uv_editor.c             # UV editor panel layout and interaction
├── uv_editor.h
├── uv_transform.c          # UV translate, rotate, scale, snap
├── uv_transform.h
├── uv_project.c            # Auto-projection (box, planar, cylindrical, sphere)
├── uv_project.h
├── uv_pack.c               # UV island packing
├── uv_pack.h
├── uv_trimsheet.c          # Trimsheet edge detection and UV alignment
└── uv_trimsheet.h
```

**Work items:**
1. UV editor panel: split viewport with UV canvas alongside 3D view
2. UV canvas rendering: draw texture with UV wireframe overlay
3. UV selection: vertex, edge, island; click, box, linked
4. UV transforms: translate, rotate, scale with axis constraint
5. UV grid snap: configurable grid, respects snap settings from §2.5
6. Auto-projection: box, planar, cylindrical, spherical UV mapping
7. UV unwrap: angle-based unwrap of selected faces
8. UV island packing: minimize wasted UV space
9. Trimsheet alignment: detect horizontal/vertical edges in trimsheet texture, snap selected UVs to nearest edge
10. Align tools: align selection to top/bottom/left/right/center

### 3.5 Tests

- `tests/p211_brush_engine_tests.c` — falloff curves, symmetry, mask composition
- `tests/p212_brush_mask_tests.c` — face set, color threshold, stencil
- `tests/p213_sculpt_delta_tests.c` — vertex delta accumulation, batch flush
- `tests/p214_tablet_input_tests.c` — pressure curve, tilt mapping
- `tests/p213a_uv_transform_tests.c` — UV translate, rotate, scale, snap to grid
- `tests/p213b_uv_project_tests.c` — box/planar/cylindrical/sphere projection
- `tests/p213c_uv_trimsheet_tests.c` — trimsheet edge detection, UV alignment
- `tests/p213d_collision_mesh_tests.c` — collision mesh create/copy/clear, target toggle
- `tests/p213e_collision_mesh_decimate_tests.c` — auto-generation, triangle count targeting
- Manual: sculpt a box into an organic shape, verify tablet pressure works
- Manual: open UV editor, project UVs, align to trimsheet grid
- Manual: create collision mesh from render mesh, simplify with auto, verify physics uses it

### 3.6 Milestone Criteria

- Mesh mode: select verts/edges/faces, extrude, bevel, subdivide
- Collision mesh: toggle target, edit collision separately from render, auto-generate
- UV editor: open alongside viewport, select/transform UVs, snap to grid
- UV projection: box and planar projection produce correct UVs
- Trimsheet alignment: auto-snap to trimsheet edges
- Sculpt mode: all 8 sculpt tools work with brush cursor
- Tablet pressure modulates sculpt strength
- Masking constrains sculpt to face sets / painted mask
- Symmetry mirrors sculpt across selected axes
- Sculpt strokes visible immediately (local cache), flushed to server on release

**Visual test (`visual/v3_mesh_sculpt`):** Enter Mesh mode on a cube.
Select faces, extrude, bevel. Toggle collision mesh target (`Shift+C`),
edit collision hull, verify green wireframe vs ghost render mesh. Open
UV editor, project UVs, align to trimsheet. Enter Sculpt mode with
tablet, sculpt organic shape, verify pressure modulation and symmetry.
Test masking. Includes all prior phase checks.

---

## Phase 4 — Paint

**Goal:** Texture layer system, texture painting (3D and 2D), and weight
painting on model surfaces with full masking support.

### 4.1 Texture Layer System

```
src/editor/paint/layer/
├── texture_layer.c         # texture_layer_t, create/destroy/resize
├── texture_layer.h
├── layer_stack.c           # texture_layer_stack_t, add/remove/reorder
├── layer_stack.h
├── layer_blend.c           # Blend mode compositing (normal, multiply, add, overlay, screen)
└── layer_blend.h
```

**Work items:**
1. `texture_layer_t` — per-layer RGBA8 pixel buffer, name, blend mode, opacity, visibility, lock
2. `texture_layer_stack_t` — dynamic array of layers, active layer index
3. Blend mode compositing (CPU reference; GPU fast path later)
4. Layer operations: add, remove, duplicate, merge down, reorder, flatten
5. Per-layer resolution support (layers in a stack may differ in size)
6. Terrain splatmap layers as special case of this system (replaces old 16-layer limit)

### 4.2 Texture Painting

```
src/editor/paint/
├── paint_texture.c         # Texture painting mode + tools
├── paint_texture.h
├── paint_projection.c      # Camera-space UV projection, seam bleed
├── paint_projection.h
├── paint_buffer.c          # Local texture edit buffer + batch flush
└── paint_buffer.h          # paint_buffer_t
```

**Work items:**
1. Paint mode activation: select material slot → enter paint mode
2. Camera-space projection: screenspace brush → UV coordinates → texel writes
3. Seam bleed: extend painted texels past UV seam boundaries
4. Local texture buffer: RGBA buffer per-material, composited on GPU
5. Paint tools: brush, fill, gradient, clone stamp, blur, sharpen, eraser
6. Channel selection: albedo, roughness, metallic, normal, emissive
7. Layer targeting: paint writes to the active layer in the stack
8. Batch flush: send texture delta to server on stroke end / 4Hz throttle
9. `:paint bake` command: flatten the layer stack to final textures

### 4.3 2D Paint Panel

```
src/editor/panels/
├── panel_paint2d.c         # 2D paint panel layout, canvas rendering
├── panel_paint2d.h
├── paint2d_canvas.c        # UV canvas rendering, zoom/pan, tile repeat
└── paint2d_canvas.h
```

**Work items:**
1. 2D panel layout: layer sidebar + UV canvas + tool bar
2. UV canvas rendering: display composited layer stack at texel resolution
3. Pan/zoom navigation (scroll, middle-drag, Home to fit)
4. UV wireframe overlay (toggle with U)
5. Tile repeat mode for seamless texture preview
6. All paint tools operate in 2D pixel coords (no projection needed)
7. Real-time sync: 2D edits reflect in 3D viewport and vice versa
8. Layer sidebar: mirrors inspector layer UI; click, drag-reorder, right-click menu
9. Activation via `:paint 2d`, [2D] inspector button, or double-click material

### 4.4 Weight Painting

```
src/editor/paint/
├── paint_weight.c          # Weight painting mode + tools
├── paint_weight.h
├── paint_weight_vis.c      # Weight visualization (color gradient overlay)
└── paint_weight_vis.h
```

**Work items:**
1. Weight paint mode: select bone → paint influence 0–1
2. Gradient visualization: blue(0)→cyan→green→yellow→red(1) overlay
3. Auto-normalize: weights sum to 1.0 per vertex
4. Weight tools: brush, gradient (linear/radial), smooth, flood, sample
5. Mirror: auto-mirror weights Bone.L ↔ Bone.R
6. Normalize command: fix all weights for selected bone
7. Batch flush: weight data sent to server on stroke end

### 4.5 Tests

- `tests/p220_texture_layer_tests.c` — layer create/destroy, blend modes, compositing, reorder, flatten
- `tests/p215_paint_projection_tests.c` — UV projection, seam bleed
- `tests/p216_paint_buffer_tests.c` — local buffer, batch flush, compositing
- `tests/p217_weight_normalize_tests.c` — auto-normalize, mirror
- `tests/p221_paint2d_canvas_tests.c` — 2D canvas pan/zoom, pixel coord painting, tiled display
- Manual: paint albedo on a cube, verify texture updates; weight-paint a skeleton
- Manual: open 2D paint panel, paint on canvas, verify 3D viewport sync

### 4.6 Milestone Criteria

- Texture layer stack works: add/remove/reorder layers, blend modes composite correctly
- Paint directly on mesh surfaces; result visible immediately
- 2D paint panel: open, paint on UV canvas, edits sync to 3D viewport
- All paint tools work (brush, fill, gradient, clone, blur, sharpen) in both 3D and 2D
- Channel switching (albedo/roughness/etc.)
- Layer management: visibility toggle, lock, rename, merge down, flatten
- Weight painting shows gradient overlay, normalizes automatically
- Mirror weight painting works
- All paint/weight operations respect masking from Phase 3

**Visual test (`visual/v4_paint`):** Enter Paint mode on a textured
mesh. Add layers, paint albedo with brush, verify 3D projection. Open
2D paint panel, paint on UV canvas, verify real-time sync to 3D
viewport. Toggle layer visibility, change blend modes, flatten.
Enter Weight Paint mode on a rigged mesh, paint weights, verify
gradient overlay and auto-normalize. Includes all prior phase checks.

---

## Phase 5 — Animation: Rigging

**Goal:** In Animation mode, place bones, set up collision bodies,
configure joints and constraints, install musculature, and run inline
physics simulation.

### 5.1 Bone Placement and Editing

```
src/editor/anim/
├── anim_bone_place.c       # Bone placement tools (click-drag, extrude)
├── anim_bone_place.h
├── anim_bone_render.c      # Bone wireframe rendering (octahedral or stick)
└── anim_bone_render.h
```

**Work items:**
1. Ctrl+click to place bone head, drag to set tail
2. Click existing bone tail → start connected child bone
3. E key: extrude child from selected bone tail
4. Bone gizmos: move head, move tail, adjust roll
5. Bone wireframe rendering: octahedral shape with head/tail distinction
6. Ctrl+P: set parent (select child, Ctrl+P, click parent)
7. Delete: remove selected bone(s)

### 5.2 Collision Body and Joint Setup

```
src/editor/anim/
├── anim_joint_inspector.c  # Joint inspector panel (type, limits, stiffness)
├── anim_joint_inspector.h
├── anim_constraint_vis.c   # Constraint limit visualization (cones, arcs, spheres)
└── anim_constraint_vis.h
```

**Work items:**
1. Inspector: collision body section (shape dropdown, radius/height/extents, mass)
2. Auto-fit: compute collision shape from mesh bounding box per bone
3. Inspector: joint type dropdown with per-type parameter panels
4. Cone twist: three-axis limit sliders with viewport cone visualization
5. Hinge: axis selector, min/max angle with viewport arc
6. Twist: axis + limits
7. Ball socket, distance, lock, aim, copy/limit rotation/position
8. Joint physics properties: stiffness, damping, compliance, drive
9. Constraint stack: ordered list with enable/disable toggles, add/remove
10. Inspector: animation damping factor (per-joint, for physics-coupled anim)

### 5.3 Musculature Installation

```
src/editor/anim/
├── anim_muscle_inspector.c # Muscle drive inspector panel
├── anim_muscle_inspector.h
├── anim_muscle_vis.c       # Muscle attachment lines + wrap cylinder rendering
└── anim_muscle_vis.h
```

**Work items:**
1. Inspector: muscle enable toggle → flexor/extensor sub-panels
2. All muscle parameters: max_force, optimal_length, max_velocity,
   pennation, tau_rise/fall, tendon slack/stiffness, wrap radius
3. Auto-fill: compute defaults from bone geometry
4. Viewport: line from origin to insertion per muscle unit
5. Viewport: wireframe cylinder for wrap surfaces
6. Color coding: blue=relaxed, red=max contraction (during sim)

### 5.4 Inline Physics Simulation

```
src/editor/anim/
├── anim_sim_control.c      # Simulation play/pause/step/reset
├── anim_sim_control.h
├── anim_sim_runner.c       # Client-local physics tick runner
├── anim_sim_runner.h       # anim_sim_runner_t
├── anim_sim_interact.c     # Drag/force/pin during simulation
└── anim_sim_interact.h
```

The editor runs its **own client-local physics tick runner** for
animation preview. This does NOT use the server's global physics tick
(which continues running for gameplay, NPCs, etc.). The local runner
operates on a snapshot copy of the skeleton's physics state and steps
independently. Other connected editors are unaffected — they see the
skeleton in its last committed pose until baked keyframes are saved.

**Work items:**
1. `anim_sim_runner_t`: local physics world copy for one skeleton;
   initializes from server state snapshot; steps at animation tick rate
2. Play/pause: start/stop the local tick runner (no server round-trip
   for each tick — only notify server of play state for status display)
3. Step forward: single tick, 10-tick (local runner advances N ticks)
4. Reset: rewind to bind pose (re-snapshot from server state)
5. Drag: click-drag bone during sim → spring constraint to cursor
   (applied to local physics copy)
6. Apply force: Ctrl+click → impulse at hit point (local)
7. Pin: right-click → "Pin" to freeze bone in place during sim (local)
8. Simulation state display: "Playing", "Paused", "Frame: N" in toolbar
9. Record commit: on `:anim record stop`, baked keyframes are sent to
   server and become visible to other editors

### 5.5 Animation Mode (mode_vtable)

```
src/editor/modes/
├── mode_animation.c        # Animation mode event handling (rigging sub-mode)
└── mode_animation.h
```

Animation mode has two sub-modes:
- **Rig**: bone placement, constraint editing (no timeline)
- **Animate**: timeline visible, keyframing active

Tab toggles between them, or `:anim rig` / `:anim animate`.

### 5.6 Tests

- `tests/p218_bone_place_tests.c` — bone creation, extrude, parent assignment
- `tests/p219_joint_inspector_tests.c` — joint type params, limit validation
- `tests/p240_constraint_vis_tests.c` — cone/arc/sphere geometry generation
- `tests/p241_muscle_autofill_tests.c` — auto-fill from bone geometry
- Manual: rig a simple 5-bone chain, set cone-twist joints, run sim, drag bones

### 5.7 Milestone Criteria

- Place bones in viewport, extrude chains, set parents
- Inspector shows joint type with all parameters; edits apply to server
- Constraint limits visible as colored overlays in viewport
- Muscle drive inspector with all parameters, auto-fill works
- Muscle attachment lines and wrap cylinders render in viewport
- Play/pause/step/reset simulation; drag bones during sim
- Bones render with correct wireframe, joint limits update during sim

**Visual test (`visual/v5_rigging`):** Import a mesh, enter Animation
mode. Place a 5-bone chain (Ctrl+click, extrude). Set cone-twist joints
with limits, verify limit cones render. Install muscles, verify
attachment lines and wrap cylinders. Run physics sim (Space), drag
bones, apply impulse (Ctrl+click), pin a bone (right-click → Pin).
Verify local runner runs independently of server tick. Includes all
prior phase checks.

---

## Phase 6 — Animation: Timeline & Keyframes

**Goal:** Full timeline panel with keyframeable channels, physics-coupled
playback, velocity derivation, and damping tuning.

### 6.1 Timeline Panel

```
src/editor/timeline/
├── timeline_panel.c        # Timeline panel rendering and interaction
├── timeline_panel.h        # timeline_panel_t
├── timeline_channel.c      # Channel tree (bones × channel types)
├── timeline_channel.h      # timeline_channel_t
├── timeline_ruler.c        # Time ruler, playhead, time selection
├── timeline_ruler.h
├── timeline_sheet.c        # Keyframe sheet (diamonds, scrubbing, selection)
└── timeline_sheet.h
```

The timeline is rendered via Clay. The channel tree on the left is a
Clay `CLAY_SCROLL()` container with rows (one per bone/channel). The
keyframe sheet is a `CUSTOM` Clay render command that draws directly
via GL for performance — potentially thousands of keyframe diamonds
need to render without layout overhead. The transport controls (play,
pause, step, rewind, etc.) are Clay buttons.

**Work items:**
1. Panel layout: transport controls (top), channel tree (left), keyframe
   sheet (right), time selection bar (bottom)
2. Transport controls: play, pause, step, rewind, fast-forward, record, loop
3. Channel tree: hierarchical (bone → channels), expand/collapse, scroll
4. Keyframe sheet: render ◆/◇/▼/✕ markers at correct frame positions
5. Playhead: vertical line, click/drag on ruler to move
6. Time selection: shift-drag on ruler to set range
7. Zoom: scroll wheel on timeline to zoom time scale
8. Pan: middle-drag to pan time range
9. Keyframe selection: click, shift-click, box-drag
10. Keyframe manipulation: G to move in time, S to scale, Delete to remove
11. Copy/paste keyframes: Ctrl+C/V within or between channels

### 6.2 Keyframe System

```
src/editor/keyframe/
├── keyframe_store.c        # Per-entity keyframe storage
├── keyframe_store.h        # keyframe_store_t, keyframe_t
├── keyframe_channel.c      # Channel definitions and metadata
├── keyframe_channel.h      # keyframe_channel_type_t
├── keyframe_interp.c       # Interpolation (step, linear, cubic/Bezier)
├── keyframe_interp.h
├── keyframe_physics.c      # Physics channel evaluation (velocity, force, mass)
└── keyframe_physics.h
```

**Key type:**
```c
typedef struct keyframe {
    float    time;              /* Frame number (float for sub-frame) */
    float    value[4];          /* Up to vec4; unused components = 0 */
    uint8_t  interp;            /* 0=step, 1=linear, 2=cubic */
    float    handle_left[2];    /* Bezier handle (time, value) */
    float    handle_right[2];   /* Bezier handle (time, value) */
} keyframe_t;

typedef struct keyframe_channel {
    uint32_t              entity_id;
    uint32_t              bone_idx;  /* UINT32_MAX for entity-level */
    keyframe_channel_type_t type;    /* POSITION, ROTATION, VELOCITY, FORCE, MASS, ... */
    uint32_t              count;
    uint32_t              capacity;
    keyframe_t           *keys;      /* Sorted by time */
} keyframe_channel_t;
```

**Work items:**
1. Channel types enum: all 17 channel types from spec §6.6
2. Keyframe store: per-entity, sparse array of channels
3. Insert/delete/modify keyframes
4. Interpolation evaluation: step, linear, cubic Bezier with handles
5. Extrapolation modes: constant, linear, cycle
6. Server sync: keyframe changes sent as `anim_keyframe` commands

### 6.3 Physics-Coupled Playback

```
src/editor/keyframe/
├── keyframe_velocity_derive.c  # Velocity derivation from displacement
└── keyframe_velocity_derive.h
```

Physics-coupled playback reuses the client-local physics tick runner
from Phase 5 (§5.4). The local runner steps the skeleton copy; keyframe
channels are evaluated and injected into the local physics state each
tick. The server's global physics tick is never involved.

**Work items:**
1. Velocity derivation: compute `v = (pos[n] - pos[n-1]) / dt` for kinematic bones
2. Animation damping: multiply derived velocity by per-joint damping factor
3. Physics channel injection: at each local sim tick, evaluate velocity/force/mass/muscle
   keyframes and inject into the local physics runner (not the server)
4. Keyframe inspector: show derived velocity, propagated impulse, damping factor

### 6.4 Playback State Machine

```
src/editor/anim/
├── anim_playback.c         # Playback state: play, pause, step, rewind, ff
├── anim_playback.h         # anim_playback_state_t
├── anim_record.c           # Simulation recording (capture sim → keyframes)
└── anim_record.h
```

**Work items:**
1. Play: step local physics runner each tick, advance playhead
2. Pause: stop advancing; local runner holds state
3. Step: advance one tick, pause
4. Rewind: reset local physics state, reset playhead to 0 or selection start
5. Fast-forward (sync): run at max rate, render every Nth frame
6. Fast-forward (async): `:anim ff --async`, server spawns temporary
   runner, streams result back
7. Record: capture simulation output (positions, rotations, velocities) as
   baked keyframes at physics rate (see §6.5)
8. Loop: repeat time selection range

### 6.5 Simulation Baking

```
src/editor/anim/
├── anim_bake.c             # General-purpose simulation bake system
├── anim_bake.h             # anim_bake_config_t, anim_bake_result_t
├── anim_bake_clean.c       # Post-bake keyframe reduction (redundancy removal)
└── anim_bake_clean.h
```

Baking converts simulation output into standard keyframes. The system
is general-purpose — it captures entity transforms regardless of what
produced them (constraint solver, rigid body, muscle drive, fracture
sim, or manual interaction). Once baked, the keyframes replace the
simulation at runtime for deterministic, fast playback.

**Work items:**
1. `anim_bake_config_t`: target entities, frame range, step interval,
   channel mask (position, rotation, scale, velocity)
2. Skeletal record mode: `Ctrl+R` toggles recording; local physics
   runner captures bone transforms per tick; results become keyframes
   in the timeline on stop
3. Rigid body bake: `:anim bake sim` spawns local physics runner for
   selected entities (not the server's global tick); runs simulation
   for frame range; captures per-entity transforms as keyframes
4. Fracture bake: same as rigid body — each fragment entity gets its
   own position/rotation keyframe track (e.g., building collapse →
   per-rubble-piece animation)
5. Progress display: toolbar shows "Baking: N/M" with progress bar;
   viewport updates periodically
6. Post-bake cleanup: `:anim bake clean <tolerance>` removes keyframes
   reconstructable by interpolation within tolerance (Douglas-Peucker
   on the curve)
7. Bake commit: baked keyframes are sent to server as standard
   `anim_keyframe` commands, becoming visible to other editors
8. Baked keyframes are indistinguishable from hand-authored ones — they
   can be edited, deleted, re-baked, or exported

### 6.6 Tests

- `tests/p222_keyframe_interp_tests.c` — step, linear, cubic, handles, extrapolation
- `tests/p223_keyframe_store_tests.c` — insert, delete, sorted order, channel lookup
- `tests/p224_velocity_derive_tests.c` — displacement→velocity, damping, edge cases
- `tests/p225_timeline_layout_tests.c` — time→pixel, pixel→time, zoom, pan
- `tests/p226_bake_tests.c` — skeletal bake, rigid body bake, frame range, step interval
- `tests/p227_bake_clean_tests.c` — keyframe reduction, tolerance, curve fidelity
- Manual: keyframe a bone rotation, play with physics, tune damping, record result
- Manual: fracture a box, bake sim, verify per-fragment keyframe tracks

### 6.7 Milestone Criteria

- Timeline panel appears in Animation mode with channel tree and keyframe sheet
- Insert keyframes for position, rotation, scale via I key and commands
- Physics channels (velocity, force, mass, muscle activation, damping) keyframeable
- Play/pause/step/rewind work; physics simulation runs in local runner
- Derived velocity shown in keyframe inspector
- Damping parameter tunes child bone response (tunable, visible effect)
- Record mode captures skeletal simulation to baked keyframes
- `:anim bake sim` captures rigid body / fracture sim to entity keyframes
- Post-bake cleanup reduces keyframe count without visible quality loss
- Time selection enables loop playback

**Visual test (`visual/v6_timeline`):** Open timeline in Animation mode.
Insert position/rotation keyframes at multiple frames via `I` key and
commands. Insert physics keyframes (velocity, damping, muscle
activation). Play back, verify physics-coupled motion. Tune damping,
verify derived velocity in inspector. Record a sim pass (Ctrl+R), verify
baked keyframes appear in timeline. Bake a rigid body sim (`:anim bake
sim`), verify per-entity keyframe tracks. Run `:anim bake clean`,
verify keyframe count reduction. Set time selection, verify loop.
Includes all prior phase checks.

---

## Phase 7 — Animation: Events & Advanced

**Goal:** Constraint swaps, gameplay events, animated attribute modifiers,
event-to-animation bindings.

### 7.1 Constraint Swap Events

```
src/editor/anim/events/
├── anim_constraint_swap.c  # Constraint swap event management
├── anim_constraint_swap.h  # anim_constraint_event_t
├── constraint_swap_inspector.c  # Inspector panel for swap events
└── constraint_swap_inspector.h
```

**Work items:**
1. ✕ markers on constraint channel in timeline
2. Right-click to insert swap event at playhead
3. Inspector: action (replace/remove/add), old/new joint type + params
4. Trigger conditions: at-frame, on-collision-impulse, on-event, on-attribute
5. Server-side: `anim_constraint_event_t` evaluation between ticks
6. Replay: constraint swaps re-execute on rewind+play
7. Collision-driven swap: joint `break_strength` triggers automatic swap

### 7.2 Gameplay Event Triggers

```
src/editor/anim/events/
├── anim_gameplay_event.c   # Gameplay event trigger management
├── anim_gameplay_event.h   # anim_event_t
├── gameplay_event_inspector.c  # Inspector for event markers
└── gameplay_event_inspector.h
```

**Work items:**
1. ▼ markers on event channel in timeline
2. Right-click to insert event at playhead
3. Inspector: name (namespaced), float params
4. Server-side: event dispatch to engine event bus at trigger time
5. `:anim event "name" frame` command

### 7.3 Event-to-Animation Bindings

```
src/editor/anim/events/
├── anim_event_binding.c    # Event→animation bindings
└── anim_event_binding.h    # anim_event_binding_t
```

**Work items:**
1. Inspector: per-entity binding list (event name → animation clip)
2. `:anim bind "event" "clip"` and `:anim unbind` commands
3. Priority and blend weight per binding
4. Server-side: event bus subscriber that triggers animation playback

### 7.4 Animated Attribute Modifiers

```
src/editor/anim/attr/
├── anim_attr_modifier.c    # Attribute modifier keyframe evaluation
├── anim_attr_modifier.h    # anim_attr_modifier_t
├── attr_modifier_inspector.c  # Inspector for attr modifier channels
└── attr_modifier_inspector.h
```

**Work items:**
1. Attribute modifier channels in timeline (per-entity)
2. Keyframe arbitrary entity attributes (health, glow, etc.)
3. Interpolation: step, linear, cubic
4. Server-side: `anim_attr_modifier_t` component, evaluates and writes to `entity_attrs_t`
5. `:anim attr <key> <time> <value>` command

### 7.5 Tests

- `tests/p226_constraint_swap_tests.c` — swap at frame, collision trigger, replay
- `tests/p227_gameplay_event_tests.c` — event insert, dispatch, namespacing
- `tests/p228_event_binding_tests.c` — bind, unbind, priority, blend
- `tests/p229_attr_modifier_tests.c` — interpolation, entity attr write
- Manual: set up a constraint swap on arm break, play sim with high-velocity
  collision, verify joint changes mid-animation

### 7.6 Milestone Criteria

- Constraint swap events visible in timeline, editable in inspector
- Constraint swaps execute during playback (replace joint type mid-sim)
- Collision-driven constraint swap works (break_strength threshold)
- Gameplay events fire at correct frames during playback
- Event-to-animation bindings trigger clips on event receipt
- Animated attribute modifiers drive entity attributes over time
- All new event types have proper undo support

**Visual test (`visual/v7_events`):** Set up a constraint swap on an
arm bone (break at frame 45). Play sim with high-velocity collision,
verify joint changes mid-animation. Insert gameplay events, verify they
fire at correct frames. Set up event-to-animation binding, verify clip
triggers on event. Add animated attribute modifiers (health, glow),
verify values drive entity attributes during playback. Includes all
prior phase checks.

---

## Phase 8 — Terrain

**Goal:** Page-based terrain editing with sculpt and paint tools.

### 8.1 Terrain System

```
src/editor/terrain/
├── terrain_manager.c       # Page grid management, LOD, streaming
├── terrain_manager.h       # terrain_manager_t, terrain_page_t
├── terrain_tools.c         # Terrain-specific sculpt tools (raise, flatten, stamp, erode)
├── terrain_tools.h
├── terrain_paint.c         # Splatmap painting (material layer blending)
├── terrain_paint.h
├── terrain_render.c        # Terrain page rendering, LOD transitions
└── terrain_render.h
```

**Work items:**
1. Page grid: 256×256 vertex pages, world-space positioning
2. Page streaming: load/unload based on camera distance
3. Terrain tools: raise/lower, smooth, flatten, stamp, erode
4. Erosion: hydraulic and thermal erosion simulation
5. Splatmap: uses texture layer stack (§4.1) — dynamic layer count, no fixed limit; painted per-vertex
6. Terrain rendering: per-page mesh, LOD with geomorphing
7. Integration with brush engine (Phase 3) for terrain sculpt/paint
8. Outliner: terrain pages as children of Terrain layer
9. Hide/swap-to-disk per-page

### 8.2 Tests

- `tests/p230_terrain_page_tests.c` — page creation, neighbor stitching
- `tests/p231_terrain_tools_tests.c` — raise, smooth, flatten, stamp math
- `tests/p232_terrain_splatmap_tests.c` — layer blending, normalize
- Manual: create terrain, sculpt hills, paint material layers

### 8.3 Milestone Criteria

- Create terrain from TUI command, pages appear in viewport
- Sculpt terrain with tablet (raise, smooth, flatten, stamp)
- Erosion simulation produces natural-looking terrain
- Paint terrain material layers (splatmap)
- Pages stream in/out based on camera distance
- Individual pages can be hidden or swapped to disk

**Visual test (`visual/v8_terrain`):** Create terrain via TUI. Sculpt
hills and valleys with tablet (raise, smooth, flatten, stamp). Run
erosion, verify natural-looking result. Paint material layers on
splatmap, verify blending. Verify pages stream in/out with camera
movement. Hide and swap individual pages. Includes all prior phase
checks.

---

## Phase 9 — Streaming & Scale

**Goal:** Handle large scenes by streaming data to/from disk, LOD, and
async loading.

### 9.1 Disk Swap System

```
src/editor/stream/
├── disk_swap.c             # Object swap-to-disk management
├── disk_swap.h             # disk_swap_entry_t
├── disk_swap_io.c          # Background I/O thread for swap read/write
└── disk_swap_io.h
```

**Work items:**
1. Swap-to-disk: serialize entity + mesh + textures → temp file, free GPU/CPU memory
2. Swap-from-disk: background thread reads temp file, main thread uploads to GPU
3. Bounding box retention: swapped objects render as wireframe AABB
4. Outliner state: grayed-out with disk icon
5. Bulk swap: swap entire layer/group with one command
6. Auto-swap: distance-based automatic swap for static geometry

### 9.2 LOD and Async Loading

```
src/editor/stream/
├── lod_manager.c           # View-dependent LOD selection
├── lod_manager.h
├── async_loader.c          # Background mesh/texture loading thread
└── async_loader.h
```

**Work items:**
1. LOD levels per static mesh: precomputed at import (decimation)
2. Screen-space size calculation for LOD selection
3. Async mesh load: background thread reads from disk, queues GPU upload
4. Async texture load: background mip level loading
5. GPU upload queue: main thread processes upload requests each frame (budgeted)

### 9.3 Tests

- `tests/p233_disk_swap_tests.c` — serialize, deserialize, memory free, reload
- `tests/p234_lod_select_tests.c` — screen-space size → LOD level
- Manual: load a 10K-entity scene, swap distant objects, verify frame rate stable

### 9.4 Milestone Criteria

- Swap objects/groups/layers to disk; wireframe AABBs remain visible
- Swap back from disk: full mesh/texture restored
- LOD levels select based on screen-space size
- Async loading prevents frame hitches during swap-in
- Large scene (10K+ entities) maintains interactive frame rate

**Visual test (`visual/v9_streaming`):** Load a 10K+ entity test scene.
Swap distant groups to disk, verify wireframe AABBs remain. Swap back,
verify full restore. Navigate rapidly, verify LOD transitions and async
loading without frame hitches. Verify interactive frame rate throughout.
Includes all prior phase checks.

---

## Phase 10 — Polish & Extensions

**Goal:** Future extensions and quality-of-life features.

### 10.1 Smear-Frame Generation

```
src/editor/anim/
├── smear_frame.c           # GJK+sweep mesh generation
├── smear_frame.h
├── smear_render.c          # Swept volume rendering (motion blur material)
└── smear_render.h
```

**Work items:**
1. Compute swept volume between keyframes using GJK support function
2. Generate mesh hull from swept volume
3. Render with stretch+fade material
4. Toggle: `:anim smear on|off`
5. Per-bone smear threshold (only generate for high-velocity bones)

### 10.2 MCP Server Integration

Reuse MCP server from `ref/editor_design.md` §1 mcp/. Extend tools
with scene editor commands (animation, paint, sculpt).

### 10.3 Script Integration

Reuse script runtime from `ref/editor_design.md` §1 script/. Extend
API with:
- Animation control: `anim_play()`, `anim_key()`, `anim_event()`
- Brush operations: `sculpt_stroke()`, `paint_stroke()`
- Timeline: `anim_set_frame()`, `anim_range()`

### 10.4 Asset Preview

```
src/editor/panels/
├── panel_asset_preview.c   # Asset preview rendering
└── panel_asset_preview.h
```

Preview meshes/textures/materials near cursor during tab-completion
or browse results.

### 10.5 Animation Clip Import/Export

```
src/editor/anim/
├── anim_clip_io.c          # .fanim format read/write
└── anim_clip_io.h
```

Custom `.fanim` format containing:
- Keyframe channels (all types including physics)
- Event triggers
- Constraint swap events
- Attribute modifiers
- Metadata (FPS, frame count, bone names)

---

## Module Layout Summary

```
src/editor/
├── scene/                  # Phase 0: window, panels, connections
│   ├── scene_main.c/.h
│   ├── scene_panel.c/.h
│   ├── scene_input.c/.h
│   ├── scene_connection.c/.h
│   ├── scene_sync.c/.h
│   ├── snap_state.c/.h
│   └── scene_selection.c/.h
├── ui/                     # Phase 0: Clay UI rendering
│   ├── clay_backend.c/.h
│   ├── clay_fonts.c/.h
│   └── clay_theme.c/.h
├── panels/                 # Phase 0–1: panel implementations
│   ├── panel_tui.c/.h
│   ├── panel_tui_render.c/.h
│   ├── panel_viewport.c/.h
│   ├── panel_outliner.c/.h
│   ├── panel_inspector.c/.h
│   ├── panel_toolbar.c/.h
│   ├── panel_paint2d.c/.h            (Phase 4)
│   ├── paint2d_canvas.c/.h           (Phase 4)
│   ├── panel_asset_preview.c/.h      (Phase 10)
│   ├── viewport_camera.c/.h
│   ├── viewport_grid.c/.h
│   ├── viewport_gizmo.c/.h
│   ├── viewport_overlay.c/.h
│   ├── outliner_tree.c/.h
│   ├── inspector_widgets.c/.h
│   └── clay_context_menu.c/.h
├── modes/                  # Phase 1+: mode vtables
│   ├── mode_manager.c/.h
│   ├── mode_object.c/.h
│   ├── mode_mesh.c/.h              (Phase 3)
│   ├── mode_sculpt.c/.h            (Phase 3)
│   ├── mode_paint.c/.h             (Phase 4)
│   ├── mode_weight.c/.h            (Phase 4)
│   ├── mode_animation.c/.h         (Phase 5)
│   └── mode_terrain.c/.h           (Phase 8)
├── brush/                  # Phase 3: unified brush engine
│   ├── brush_engine.c/.h
│   ├── brush_falloff.c/.h
│   └── brush_mask.c/.h
├── input/                  # Phase 3: tablet
│   └── tablet_input.c/.h
├── mesh/                   # Phase 3: mesh tools
│   ├── collision_mesh.c/.h
│   ├── collision_mesh_vis.c/.h
│   └── uv/
│       ├── uv_editor.c/.h
│       ├── uv_transform.c/.h
│       ├── uv_project.c/.h
│       ├── uv_pack.c/.h
│       └── uv_trimsheet.c/.h
├── paint/                  # Phase 4: painting
│   ├── layer/
│   │   ├── texture_layer.c/.h
│   │   ├── layer_stack.c/.h
│   │   └── layer_blend.c/.h
│   ├── paint_texture.c/.h
│   ├── paint_projection.c/.h
│   ├── paint_buffer.c/.h
│   ├── paint_weight.c/.h
│   └── paint_weight_vis.c/.h
├── undo/                   # Phase 2: branching undo
│   ├── undo_stack.c/.h
│   ├── undo_rebase.c/.h
│   └── undo_conflict.c/.h
├── collab/                 # Phase 2: collaboration
│   ├── lock_manager.c/.h
│   ├── lock_protocol.c/.h
│   ├── collab_sync.c/.h
│   └── collab_presence.c/.h
├── anim/                   # Phase 5–7: animation
│   ├── anim_bone_place.c/.h
│   ├── anim_bone_render.c/.h
│   ├── anim_joint_inspector.c/.h
│   ├── anim_constraint_vis.c/.h
│   ├── anim_muscle_inspector.c/.h
│   ├── anim_muscle_vis.c/.h
│   ├── anim_sim_control.c/.h
│   ├── anim_sim_runner.c/.h        # Client-local physics tick runner (NOT server global tick)
│   ├── anim_sim_interact.c/.h
│   ├── anim_playback.c/.h
│   ├── anim_record.c/.h
│   ├── anim_bake.c/.h              # General-purpose sim bake (Phase 6)
│   ├── anim_bake_clean.c/.h        # Post-bake keyframe reduction (Phase 6)
│   ├── anim_clip_io.c/.h           (Phase 10)
│   ├── smear_frame.c/.h            (Phase 10)
│   ├── smear_render.c/.h           (Phase 10)
│   ├── events/
│   │   ├── anim_constraint_swap.c/.h    (Phase 7)
│   │   ├── constraint_swap_inspector.c/.h
│   │   ├── anim_gameplay_event.c/.h
│   │   ├── gameplay_event_inspector.c/.h
│   │   ├── anim_event_binding.c/.h
│   │   └── anim_event_binding.h
│   └── attr/
│       ├── anim_attr_modifier.c/.h      (Phase 7)
│       └── attr_modifier_inspector.c/.h
├── timeline/               # Phase 6: timeline
│   ├── timeline_panel.c/.h
│   ├── timeline_channel.c/.h
│   ├── timeline_ruler.c/.h
│   └── timeline_sheet.c/.h
├── keyframe/               # Phase 6: keyframe system
│   ├── keyframe_store.c/.h
│   ├── keyframe_channel.c/.h
│   ├── keyframe_interp.c/.h
│   ├── keyframe_physics.c/.h
│   └── keyframe_velocity_derive.c/.h
├── terrain/                # Phase 8: terrain
│   ├── terrain_manager.c/.h
│   ├── terrain_tools.c/.h
│   ├── terrain_paint.c/.h
│   └── terrain_render.c/.h
├── stream/                 # Phase 9: streaming
│   ├── disk_swap.c/.h
│   ├── disk_swap_io.c/.h
│   ├── lod_manager.c/.h
│   └── async_loader.c/.h
├── cache/                  # Phase 3+: local edit caching
│   ├── stroke_cache.c/.h
│   └── batch_flush.c/.h
└── commands/               # Phase 0+: editor commands
    ├── cmd_connect.c                (Phase 0) — :connect <host:port>
    ├── cmd_save.c                   (Phase 0) — :save force, :save status
    ├── cmd_camera.c                 (Phase 1) — :camera front/back/left/right/top/bottom
    ├── cmd_mode.c                   (Phase 1) — :mode object/mesh/sculpt/paint/weight/animation/terrain
    ├── cmd_select.c                 (Phase 1) — :select <name>, :select all, :select none
    ├── cmd_cursor.c                 (Phase 1) — :cursor <x> <y> <z>
    ├── cmd_import.c                 (Phase 1) — :import <path>
    ├── cmd_lock.c                   (Phase 2)
    ├── cmd_mesh.c                   (Phase 3) — :mesh target, :mesh collision create/from render/auto/clear, :mesh overlay
    ├── cmd_uv.c                     (Phase 3) — :uv project, :uv unwrap, :uv pack, :uv align, :uv snap grid, :uv trimsheet align, :uv close
    ├── cmd_paint.c                  (Phase 4) — :paint bake, :paint 2d, :paint 3d
    ├── cmd_anim.c                   (Phase 5)
    ├── cmd_anim_key.c               (Phase 6)
    ├── cmd_anim_event.c             (Phase 7)
    └── cmd_terrain.c                (Phase 8)

include/ferrum/editor/
├── scene_editor.h          # scene_editor_t, init, shutdown
├── panel_layout.h          # panel_layout_t, panel_id_t
├── editor_mode.h           # mode_id_t, mode_vtable_t
├── brush.h                 # brush_state_t, brush_params_t
├── keyframe.h              # keyframe_t, keyframe_channel_t
├── undo.h                  # undo_stack_t, undo_record_t
├── lock.h                  # lock_entry_t, lock_manager_t
└── timeline.h              # timeline_panel_t
```

---

## Header Ownership (2-Type Rule Compliance)

| Header | Types |
|--------|-------|
| `clay_backend.h` | `clay_backend_t`, `clay_backend_config_t` |
| `clay_fonts.h` | `clay_font_set_t`, `clay_font_id_t` (enum) |
| `scene_editor.h` | `scene_editor_t`, `scene_editor_config_t` |
| `panel_layout.h` | `panel_layout_t`, `panel_id_t` (enum) |
| `editor_mode.h` | `mode_vtable_t`, `mode_id_t` (enum) |
| `brush.h` | `brush_state_t`, `brush_params_t` |
| `keyframe.h` | `keyframe_t`, `keyframe_channel_t` |
| `undo.h` | `undo_stack_t`, `undo_record_t` |
| `lock.h` | `lock_entry_t`, `lock_manager_t` |
| `timeline.h` | `timeline_panel_t`, `timeline_channel_t` |
| `texture_layer.h` | `texture_layer_t`, `texture_layer_stack_t` |
| `uv_editor.h` | `uv_editor_t`, `uv_selection_t` |

---

## Engine-Side Work Required

Several features require new engine components (not just editor code):

| Feature | Engine module | Phase |
|---------|---------------|-------|
| Server auto-sync (world persistence) | `src/editor/protocol/edit_autosave.c` | 0 |
| `pivot_offset` entity field | `src/game/entity/entity_fields.c` | 1 |
| Collision mesh asset storage | `src/asset/collision_mesh_asset.c` — per-entity collision mesh separate from render mesh | 3 |
| Mesh decimation (edge-collapse) | `src/mesh/mesh_decimate.c` — iterative simplification for auto-collision generation | 3 |
| Server-side undo stack | `src/editor/protocol/edit_undo_stack.c` | 2 |
| `anim_simulate` handler | `src/editor/protocol/edit_anim_handler.c` | 5 |
| `anim_record` handler | `src/editor/protocol/edit_anim_handler.c` | 6 |
| Baked keyframe playback (bypass solver) | `src/animation/playback/baked_anim_eval.c` | 6 |
| Animated attribute modifiers | `src/animation/attr/anim_attr_eval.c` | 7 |
| Constraint swap events | `src/physics/animated/constraint_swap.c` | 7 |
| Gameplay event bus | `src/game/event_bus.c` | 7 |
| Event-to-animation binding | `src/animation/state/anim_event_trigger.c` | 7 |
| Velocity derivation for kinematic bones | `src/physics/animated/kinematic_velocity.c` | 6 |
| Animation damping factor | `bone_joint_desc_t.anim_damping` field | 6 |
| Animation clip format (.fanim) | `src/animation/format/fanim_load.c` | 10 |
| Smear-frame GJK+sweep | `src/physics/collision/gjk_sweep_volume.c` | 10 |
| Lock table (server-side) | `src/editor/protocol/edit_lock_table.c` | 2 |
| Edit broadcast (server-side) | `src/editor/protocol/edit_broadcast.c` | 2 |

---

## Build Configuration

```makefile
# Scene editor (links client renderer + editor code)
make build/scene_editor EDITOR=1 SCENE=1

# Scene editor with Tracy profiling
make build/scene_editor EDITOR=1 SCENE=1 TRACY=1

# Scene editor tests
make build/p200_panel_layout_tests EDITOR=1 SCENE=1
# ... through p241

# All scene editor tests
make test_scene_editor EDITOR=1 SCENE=1
```

Compile flags:
- `EDITOR_ENABLE` — gates editor code in server
- `SCENE_EDITOR_ENABLE` — gates scene editor code (superset of EDITOR_ENABLE)
- `SCRIPTING_ENABLE` — gates scripting runtime (optional)

External dependencies:
- `extern/clay/` — Clay UI layout library (single-header, `#define CLAY_IMPLEMENTATION` in `clay_backend.c`)
- `extern/tracy/` — Tracy profiler (optional, `TRACY=1`)
- `extern/cgltf/` — glTF loader (existing)

---

## Dependency Graph

```
Phase 0 (Foundation)
    │
    ▼
Phase 1 (Core Editing)
    │
    ├──────────────┬──────────────┬──────────────┐
    ▼              ▼              ▼              ▼
Phase 2        Phase 3        Phase 5        Phase 9
(Undo/Collab)  (Mesh/Sculpt)  (Anim:Rigging) (Streaming)
               │              │
               ▼              ▼
            Phase 4        Phase 6
            (Paint)        (Anim:Timeline)
               │              │
               ├──────┐       ▼
               ▼      │    Phase 7
            Phase 8   │    (Anim:Events)
            (Terrain) │       │
                      │       ▼
                      └──► Phase 10
                          (Polish)
```

Phases 2, 3, 5, and 9 can all proceed in parallel after Phase 1.
Phase 4 requires Phase 3 (brush engine). Phase 6 requires Phase 5
(rigging). Phase 7 requires Phase 6 (timeline). Phase 8 requires
Phases 3 and 4 (brush + paint). Phase 10 collects remaining work.

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| **GL context sharing** — editor UI + scene render in same context | Single-thread GL; render panels into FBOs, composite to screen |
| **Tablet input on Linux** — SDL2's pen support is limited | Direct libinput/XInput2 fallback if SDL2 pen events unavailable |
| **Undo rebase complexity** — conflict detection edge cases | Conservative: if unsure, orphan rather than corrupt; manual recovery via `:undo tree` |
| **Physics-coupled playback latency** — round-trip to server per tick | Local physics prediction for viewport; server confirms; desync correction on pause |
| **Large scene memory** — 10K+ entities with textures | Phase 9 streaming is critical; implement visibility culling early in Phase 1 |
| **Multi-editor consistency** — eventual consistency gaps | Lock system (Phase 2) is the primary answer; last-write-wins is the fallback |
| **Constraint swap stability** — changing constraints mid-simulation | Warm-start new constraint from old; clamp velocities post-swap to prevent explosion |
