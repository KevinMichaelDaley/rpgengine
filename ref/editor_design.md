# Level Editor Implementation Design

## 1. Module Layout

```
src/editor/
├── protocol/
│   ├── json_write.c             # JSON serialization helpers
│   ├── json_parse.c             # JSON tokenizer / parser
│   └── json_access.c            # JSON value accessors (get string, number, etc.)
├── io/
│   ├── edit_io_thread.c         # Dedicated editor I/O thread (TCP accept/read/write)
│   ├── edit_cmd_ring.c          # Lock-free SPSC command ring (I/O thread -> tick loop)
│   ├── edit_cmd_ring_consume.c  # Ring consume operations
│   └── edit_cmd_ring_query.c    # Ring query operations
├── dispatch/
│   └── edit_dispatch.c          # Command -> handler routing (runs on main tick thread)
├── state/
│   ├── edit_entity_store.c      # Entity store lifecycle (init/destroy/create/remove)
│   ├── edit_entity_store_access.c # Entity store get/get_mut/restore/count/find_by_name
│   ├── edit_entity_store_query.c  # Entity store query helpers
│   ├── edit_entity_types.c      # Entity type registry (box, sphere, capsule, marker, mesh, halfspace)
│   ├── edit_selection.c         # Selection mutation (add/remove/toggle/clear)
│   ├── edit_selection_query.c   # Selection queries (contains/count/ids)
│   ├── edit_selection_info.c    # Selection info helpers
│   ├── edit_undo.c              # Undo/redo stack lifecycle and recording
│   ├── edit_undo_ops.c          # Undo step and redo operations
│   ├── edit_undo_group.c        # Undo group begin/end
│   ├── edit_serialize.c         # Level -> JSON serialization (entities only)
│   ├── edit_deserialize.c       # JSON -> level deserialization (entities only)
│   ├── edit_serialize_full.c    # Level -> JSON v2 (entities + groups)
│   ├── edit_deserialize_full.c  # JSON v2 -> level (entities + groups)
│   ├── edit_history.c           # Command history ring buffer + JSONL log
│   ├── edit_history_flush.c     # History flush to file
│   └── edit_history_ctx.c       # History context snapshot (cursor, selection, aliases)
├── commands/
│   ├── cmd_select.c             # select (by ID, toggle)
│   ├── cmd_select_all.c         # select_all
│   ├── cmd_select_near.c        # select_near (spatial radius)
│   ├── cmd_select_regex.c       # select_regex (name pattern)
│   ├── cmd_select_touching.c    # select_touching (physics query) + select_fill (flood-fill)
│   ├── cmd_deselect_near.c      # deselect_near
│   ├── cmd_deselect_regex.c     # deselect_regex
│   ├── cmd_delete.c             # delete (selection-based)
│   ├── cmd_delete_id.c          # delete_id (by entity ID)
│   ├── cmd_move.c               # move (selection-based, delta)
│   ├── cmd_move_id.c            # move_id (by entity ID)
│   ├── cmd_rotate.c             # rotate (selection-based)
│   ├── cmd_rotate_id.c          # rotate_id (by entity ID)
│   ├── cmd_scale.c              # scale (selection-based)
│   ├── cmd_scale_id.c           # scale_id (by entity ID)
│   ├── cmd_clone.c              # clone (duplicate selected entities)
│   ├── cmd_save.c               # save (level to file)
│   ├── cmd_load.c               # load (level from file)
│   ├── cmd_list_types.c         # list_types (entity type registry)
│   ├── cmd_list_entities.c      # list_entities (with optional regex filter)
│   ├── cmd_alias.c              # alias_create, alias_delete
│   ├── cmd_alias_list.c         # alias_list
│   ├── cmd_cursor.c             # cursor_push, cursor_pop, cursor_snap
│   ├── cmd_group.c              # group, ungroup, select_group, group_info
│   ├── cmd_group_mask.c         # group_mask resolution
│   ├── cmd_material.c           # material (set/get slot paths)
│   ├── cmd_asset_list.c         # asset_list (catalog listing)
│   ├── cmd_asset_search.c       # asset_search (regex on asset paths)
│   ├── cmd_complete.c           # complete (tab-completion)
│   ├── cmd_browse.c             # browse (asset browser)
│   ├── cmd_physics_pause.c      # physics_pause, physics_resume, physics_reset
│   ├── cmd_physics_step.c       # physics_step (single tick while paused)
│   └── edit_cmd_resolve.c       # Entity ID resolution (number or name lookup)
├── assets/
│   ├── edit_asset_registry.c    # Asset catalog (init, add, scan directory tree)
│   ├── edit_asset_query.c       # Asset queries (find, list, search, complete)
│   └── edit_asset_serve.c       # TCP asset transfer (server-side)
├── mesh/
│   ├── mesh_slot.c              # Slot lifecycle (init/destroy/clear/face_count)
│   ├── mesh_slot_reserve.c      # Buffer capacity growth (vertices, indices)
│   ├── mesh_slot_add.c          # Append vertex / triangle
│   ├── mesh_edit.c              # Top-level mesh edit subsystem (16 slots)
│   ├── mesh_edit_mode.c         # Selection mode switching
│   ├── mesh_edit_slot.c         # Active slot get/set
│   ├── mesh_sel_bitset.c        # Selection bitset (set/unset/toggle/test/clear)
│   ├── mesh_sel_bitset_ops.c    # Bitset bulk operations
│   ├── mesh_sel_convert.c       # Selection conversion (face<->vertex, face<->edge)
│   ├── mesh_edge_table.c        # Edge table build/find/destroy
│   ├── mesh_select.c            # Element selection commands
│   ├── mesh_select_topo.c       # Topology-based selection (edge loop, edge ring)
│   ├── mesh_select_grow.c       # Selection grow/shrink
│   ├── mesh_vao_serialize.c     # FVMA binary serialization
│   ├── mesh_vao_deserialize.c   # FVMA binary deserialization
│   ├── mesh_prim_box.c          # Box primitive generator
│   ├── mesh_prim_plane.c        # Plane primitive generator
│   ├── mesh_prim_cylinder.c     # Cylinder primitive generator
│   ├── mesh_prim_sphere.c       # UV sphere primitive generator
│   ├── mesh_extrude.c           # Face extrusion (region)
│   ├── mesh_extrude_individual.c # Individual face extrusion
│   ├── mesh_inset.c             # Face inset
│   ├── mesh_outset.c            # Face outset
│   ├── mesh_bevel.c             # Edge bevel
│   ├── mesh_bevel_vertex.c      # Vertex bevel
│   ├── mesh_subdiv_linear.c     # Linear subdivision
│   ├── mesh_subdiv_loop.c       # Loop subdivision
│   ├── mesh_merge.c             # Vertex merge (by distance)
│   ├── mesh_collapse.c          # Edge/face collapse
│   ├── mesh_clip.c              # Plane clipping
│   ├── mesh_normals.c           # Normal recalculation (smooth/flat/area-weighted)
│   ├── mesh_split_detach.c      # Selection split / detach
│   ├── mesh_bridge.c            # Edge loop bridge
│   ├── mesh_triangulate.c       # N-gon triangulation
│   ├── mesh_uv_planar.c         # Planar UV projection
│   ├── mesh_uv_wrap.c           # Box, cylindrical, spherical UV
│   ├── mesh_uv_seam.c           # UV seam marking
│   ├── mesh_uv_fit.c            # UV fit to [0,1]
│   ├── mesh_uv_transform.c      # UV translate/rotate/scale
│   ├── mesh_uv_smart.c          # Angle-based smart unwrap
│   ├── mesh_uv_island.c         # UV island detection
│   ├── mesh_uv_pack.c           # UV island packing
│   ├── mesh_uv_density.c        # Texel density equalization
│   ├── mesh_material.c          # Per-face material assignment
│   ├── mesh_material_assign.c   # Material batch assign
│   ├── mesh_material_lift.c     # Lift material from face
│   ├── mesh_material_replace.c  # Replace material across mesh
│   ├── mesh_brush.c             # Quake-style brush (half-plane intersection)
│   ├── mesh_csg_hollow.c        # CSG hollow (inner shell)
│   ├── mesh_csg_boolean.c       # CSG merge/subtract/intersect
│   ├── mesh_clipboard.c         # Mesh copy/paste
│   ├── mesh_export_obj.c        # Wavefront OBJ export
│   ├── mesh_import_obj.c        # Wavefront OBJ import
│   ├── mesh_snapshot.c          # Mesh state snapshot for undo
│   ├── mesh_snapshot_update.c   # Snapshot restore
│   ├── mesh_undo.c              # Mesh-level undo stack
│   ├── mesh_undo_ops.c          # Mesh undo/redo operations
│   └── mesh_commit.c            # Bake mesh -> entity + FVMA binary
├── controller/
│   ├── ctrl_input.c             # TUI key processing (mode dispatch)
│   ├── ctrl_tui.c               # Terminal UI rendering (ANSI)
│   ├── ctrl_log.c               # Controller log ring buffer
│   ├── ctrl_log_query.c         # Log query helpers
│   ├── ctrl_render.c            # TUI screen rendering
│   └── ctrl_browse.c            # Asset browser TUI mode
├── client/
│   ├── client_cursor.c          # Client-side cursor state
│   ├── client_cursor_render.c   # Cursor rendering
│   ├── client_state_dispatch.c  # State change dispatch
│   ├── client_state_push.c      # Push state to renderer
│   ├── client_editor_input.c    # Client-side editor input handling
│   ├── client_asset_download.c  # Asset download over TCP
│   ├── client_asset_cache.c     # Client-side asset cache
│   └── client_asset_cache_io.c  # Cache file I/O
├── scene/
│   ├── scene_main.c             # Scene editor lifecycle (init/shutdown/run/frame)
│   ├── scene_panel.c            # Panel layout (4 regions + dividers)
│   ├── scene_panel_persist.c    # Panel layout save/load
│   ├── scene_input.c            # SDL2 event dispatch
│   ├── scene_frame.c            # Per-frame pump + action dispatch
│   ├── scene_connection.c       # TCP/UDP connection lifecycle
│   ├── scene_connection_io.c    # Connection send/recv
│   ├── scene_connection_status.c # Connection status formatting
│   ├── scene_sync.c             # Offline queue lifecycle
│   ├── scene_sync_state.c       # Sync state machine
│   ├── scene_cmd.c              # JSON command formatting (spawn, delete, select)
│   ├── scene_cmd_transform.c    # JSON command formatting (move, rotate, scale, list)
│   ├── scene_cmd_parse.c        # JSON response parsing
│   ├── scene_ui_outliner.c      # Outliner panel Clay layout
│   ├── scene_ui_inspector.c     # Inspector panel Clay layout
│   ├── scene_ui_viewport.c      # Viewport panel Clay layout
│   ├── scene_ui_tui.c           # TUI panel Clay layout
│   ├── scene_viewport_render.c  # Viewport FBO/shader/pipeline setup
│   ├── scene_viewport_draw.c    # Scene draw (grid + entities + camera)
│   ├── scene_viewport_mesh.c    # FVMA entity mesh loading
│   └── snap_state.c             # Per-editor snap/grid state
├── editor_tick_drain.c          # Per-tick drain loop (ring -> dispatch -> response)
└── (scene editor entry point is scene_main.c)
```

### Public Headers

```
include/ferrum/editor/
├── json_parse.h                 # JSON parser types and API
├── edit_cmd_ring.h              # SPSC command ring
├── edit_io_thread.h             # I/O thread lifecycle
├── edit_dispatch.h              # Command dispatch table
├── edit_entity.h                # Entity types, entity store
├── edit_selection.h             # Selection set
├── edit_undo.h                  # Undo/redo stack
├── edit_cmd_ctx.h               # Command context (physics bridge, groups, cursor)
├── edit_commands.h              # Command handler declarations + registration
├── edit_serialize.h             # Level save/load JSON
├── edit_history.h               # Command history (ring + JSONL file)
├── edit_asset_registry.h        # Asset catalog
├── edit_physics_ctrl.h          # Physics sim control callbacks
├── edit_script_env.h            # Script environment (snapshots, update blob)
├── edit_script_rebase.h         # Script update rebase onto entity store
├── editor_ctx.h                 # Top-level editor context
├── ctrl_tui.h                   # TUI controller context
├── ctrl_conn.h                  # TCP connection to server
├── ctrl_log.h                   # Controller log ring
├── ctrl_cmd_defs.h              # TUI command table + text-to-JSON conversion
├── ctrl_browse.h                # Asset browser TUI mode
├── ctrl_mesh_mode.h             # Mesh mode keybinding dispatch
├── protocol/
│   └── edit_autosave.h          # Server-side autosave
├── mesh/
│   ├── mesh_slot.h              # Editable mesh slot (indexed triangle mesh)
│   ├── mesh_edit.h              # Mesh edit subsystem (16 slots, selection bitsets)
│   ├── mesh_selection.h         # Edge table + selection conversion
│   ├── mesh_select.h            # Element selection commands
│   ├── mesh_vao_format.h        # FVMA binary wire format
│   ├── mesh_primitives.h        # Primitive generators (box, plane, cylinder, sphere)
│   ├── mesh_extrude.h           # Face extrusion
│   ├── mesh_inset.h             # Face inset
│   ├── mesh_bevel.h             # Edge/vertex bevel
│   ├── mesh_subdivide.h         # Linear + Loop subdivision
│   ├── mesh_merge.h             # Vertex merge
│   ├── mesh_clip.h              # Plane clipping
│   ├── mesh_normals.h           # Normal recalculation
│   ├── mesh_bridge.h            # Edge loop bridge
│   ├── mesh_triangulate.h       # Triangulation
│   ├── mesh_uv.h                # UV projection (planar, box, cylindrical, spherical)
│   ├── mesh_uv_seam.h           # UV seam marking
│   ├── mesh_uv_transform.h      # UV transforms
│   ├── mesh_uv_smart.h          # Smart unwrap
│   ├── mesh_uv_pack.h           # UV island packing
│   ├── mesh_material.h          # Per-face material assignment
│   ├── mesh_material_ops.h      # Material lift/replace
│   ├── mesh_brush.h             # Quake-style brush geometry
│   ├── mesh_csg.h               # CSG operations (hollow, merge, subtract, intersect)
│   ├── mesh_transfer.h          # Copy/paste + OBJ import/export
│   ├── mesh_snapshot.h          # Mesh state snapshot
│   ├── mesh_undo.h              # Mesh undo/redo stack
│   └── mesh_commit.h            # Bake mesh -> entity + FVMA
├── scene/
│   ├── scene_main.h             # Scene editor context + lifecycle
│   ├── scene_panel.h            # Panel layout (Outliner, Viewport, Inspector, TUI)
│   ├── scene_input.h            # SDL2 event processing
│   ├── scene_frame.h            # Per-frame pump + action dispatch
│   ├── scene_connection.h       # TCP/UDP server connection
│   ├── scene_sync.h             # Offline queue + in-flight tracking
│   ├── scene_cmd.h              # Command formatting + response parsing
│   ├── scene_ui.h               # UI panel builders (Clay)
│   ├── scene_viewport_render.h  # 3D viewport rendering (FBO, shaders, meshes)
│   └── snap_state.h             # Grid/snap state
├── viewport/
│   ├── viewport_camera.h        # Orbit camera
│   ├── viewport_gizmo.h         # Transform gizmo
│   └── selection_raycast.h      # Ray/frustum intersection for picking
├── panels/
│   ├── outliner_tree.h          # Outliner tree model with filtering
│   ├── inspector_widgets.h      # Inspector property widgets
│   └── panel_toolbar.h          # Toolbar button state
├── mode/
│   ├── mode_manager.h           # Editor mode vtable dispatch
│   └── mode_object.h            # Object mode
├── ui/
│   ├── clay_backend.h           # Clay UI renderer backend
│   ├── clay_fonts.h             # Font atlas
│   ├── clay_theme.h             # Color theme
│   └── glad_gl_loader.h         # GL function loader
└── client/
    ├── client_cursor.h          # Client cursor state
    ├── client_editor_input.h    # Client editor input
    ├── client_state_dispatch.h  # Client state dispatch
    ├── client_state_socket.h    # Client state socket
    ├── client_asset_download.h  # Asset download
    ├── client_asset_cache.h     # Asset cache
    └── client_mesh_render.h     # Client mesh rendering
```

---

## 2. Entity System

### Entity Types

Six built-in entity types (defined as constants in `edit_entity.h`):

| Constant                    | Value | Description                  |
|-----------------------------|-------|------------------------------|
| `EDIT_ENTITY_TYPE_BOX`      | 0     | Axis-aligned box collider    |
| `EDIT_ENTITY_TYPE_SPHERE`   | 1     | Sphere collider              |
| `EDIT_ENTITY_TYPE_CAPSULE`  | 2     | Capsule collider             |
| `EDIT_ENTITY_TYPE_MARKER`   | 3     | Non-physical marker          |
| `EDIT_ENTITY_TYPE_MESH`     | 4     | Custom mesh (FVMA geometry)  |
| `EDIT_ENTITY_TYPE_HALFSPACE`| 5     | Infinite half-plane collider |

Maximum 32 types supported (`EDIT_ENTITY_TYPE_MAX`). Type registry in `edit_entity_types.c` maps names to IDs for string-based lookup.

### Entity Structure (`edit_entity_t`)

Each entity stores:
- `pos[3]` — world position
- `rot[3]` — Euler rotation in degrees (pitch, yaw, roll)
- `scale[3]` — per-axis scale factors
- `pivot_offset[3]` — local-space pivot offset for transforms
- `type` — entity type (`EDIT_ENTITY_TYPE_*`)
- `body_index` — physics body index (`UINT32_MAX` = none)
- `active` — whether slot is in use
- `name[256]` — optional display name
- `materials[5][256]` — material slot paths (albedo, normal, roughness, metallic, emissive)
- `attrs` — dynamic key-value attributes (`entity_attrs_t`) for gameplay scripts

### Entity Store (`edit_entity_store_t`)

Flat-array entity store with O(1) LIFO freelist allocation:

- `entities` — mmap'd array of `edit_entity_t` slots
- `freelist` — stack of free slot indices
- `capacity`, `free_count`, `active_count`

API:
- `edit_entity_store_init` / `destroy`
- `edit_entity_store_create(type)` — allocates slot, returns ID
- `edit_entity_store_remove(id)` — marks inactive, pushes to freelist
- `edit_entity_store_get` / `get_mut` — access by ID
- `edit_entity_store_restore(id, snapshot)` — re-activate slot (for undo)
- `edit_entity_store_count` — active entity count
- `edit_entity_store_find_by_name` — name-based lookup

Thread safety: only mutated from the main tick thread during drain.

---

## 3. Command System

### Dispatch (`edit_dispatch.h`)

Command dispatch table with handler registration:

- Up to 64 handlers (`EDIT_DISPATCH_MAX_HANDLERS`)
- Command name max 32 chars
- Handler signature: `bool handler(edit_dispatch_t *d, const json_value_t *args, json_value_t *result, json_arena_t *arena)`
- `edit_dispatch_exec()` — parse JSON, lookup handler, call it, serialize response
- Parse and response arenas are per-dispatch (reused per command)
- `user_data` pointer holds `edit_cmd_ctx_t`
- Optional `edit_history` for command logging

### Command Context (`edit_cmd_ctx_t`)

Shared context for all command handlers, stored as `dispatch->user_data`:

- `entities` — entity store pointer
- `selection` — selection set pointer
- `undo` — undo/redo stack pointer
- `bridge` — physics bridge callbacks (NULL = no physics)
- `physics` — physics simulation control (NULL = no physics)
- `cursor_stack[16][3]` — LIFO cursor position stack
- `groups` — named selection groups (heap-allocated, up to 64 slots)
- `asset_registry` — asset catalog (NULL if no asset directory)
- `mesh` — mesh editing context (NULL if mesh mode not initialized)
- `script_runtime` — Aegis script runtime (NULL if scripting not configured)

### Command Resolution

`edit_cmd_resolve_entity()` accepts either a numeric ID or a string name and resolves to an entity ID. This enables commands to reference entities by name (e.g., `"entity": "player_spawn"`) or by ID (`"entity": 42`).

### Available Commands

**Entity lifecycle:**
- `spawn` — create entity (type, pos, rot, scale, name)
- `delete` — delete selected entities (with undo)
- `delete_id` — delete by entity ID
- `clone` — duplicate selected entities (optional offset)
- `entity_def` — spawn with pre-applied attrs

**Transforms:**
- `move` / `move_id` — translate (selection or by ID)
- `rotate` / `rotate_id` — rotate (selection or by ID)
- `scale` / `scale_id` — scale (selection or by ID)

**Selection:**
- `select` / `deselect` — by entity ID (toggle support)
- `select_all` / `deselect_all`
- `select_near` / `deselect_near` — spatial radius query
- `select_regex` / `deselect_regex` — name pattern matching
- `select_touching` — physics contact query
- `select_fill` — flood-fill via touching until stable

**Groups:**
- `group` — create named group from selection (with pivot, optional parent)
- `ungroup` — dissolve group (with undo)
- `select_group` — select all entities in a group
- `group_info` — query group metadata
- `group_list` / `group_save` / `group_delete` — manage named groups

Group names must start with `&`. Up to 64 groups, 4096 entities per group. Groups support nesting via a `parent` field.

**Aliases (@ markers):**
- `alias_create` — create named alias with position/rotation
- `alias_delete` — remove alias
- `alias_list` — list aliases (optional regex filter)

**Cursor:**
- `cursor_push` / `cursor_pop` — position stack
- `cursor_snap` — snap cursor to entity or selection center

**Level I/O:**
- `save` — serialize to JSON file
- `load` — deserialize from JSON file
- Supports v1 (entities only) and v2 (entities + groups) formats

**Physics control:**
- `physics_pause` / `physics_resume` / `physics_step` / `physics_reset`

**Materials:**
- `material` — set/get per-slot material paths (albedo, normal, roughness, metallic, emissive)
- `physics_material` — set friction/restitution on physics body
- `setattr` — set dynamic key-value attribute on entity

**Joints:**
- `joint` — create physics joint between two entities (distance, ball, hinge)

**Assets:**
- `asset_list` — catalog listing (prefix filter, type filter)
- `asset_search` — regex search on asset paths
- `asset_complete` — tab-completion for asset paths
- `browse` — asset browser with prefix/filter
- `complete` — general tab-completion for command input

**Scripting:**
- `script` — manage Aegis scripts (load, unload, list)
- `source` — execute a file of text commands

**Query:**
- `list_types` — enumerate entity type registry
- `list_entities` — list active entities (optional regex filter)

**Mesh modeling commands** (see Section 8).

---

## 4. Physics Bridge

The `edit_physics_bridge_t` (defined in `edit_cmd_ctx.h`) provides callback hooks for bridging editor entity operations to the physics engine. All callbacks are optional (NULL = no-op).

| Callback             | Signature                                                          | Purpose                                     |
|----------------------|--------------------------------------------------------------------|---------------------------------------------|
| `on_spawn`           | `uint32_t (*)(void*, uint32_t entity_id, const edit_entity_t*)`    | Create physics body after entity spawn       |
| `on_delete`          | `void (*)(void*, uint32_t entity_id, uint32_t body_index)`         | Destroy physics body before entity delete    |
| `on_move`            | `void (*)(void*, uint32_t entity_id, uint32_t body_index, float[3])` | Teleport physics body after entity move    |
| `on_query_touching`  | `uint32_t (*)(void*, uint32_t entity_id, candidates, out, max)`    | Query contact pairs for select_touching      |
| `on_mesh_data`       | `void (*)(void*, uint32_t body_index, uint8_t* fvma, uint32_t)`   | Deliver FVMA mesh data to physics engine     |
| `on_joint`           | `uint32_t (*)(void*, body_a, body_b, type, anchor_a, anchor_b, axis)` | Create joint between two bodies          |
| `on_set_material`    | `void (*)(void*, uint32_t body_index, float friction, float restitution)` | Set physics material properties      |

### Physics Simulation Control (`edit_physics_ctrl_t`)

Callback interface for pause/resume/step/reset:
- `on_pause`, `on_resume`, `on_step`, `on_reset`, `is_paused`
- Host application (e.g., demo_server) provides implementations

---

## 5. Undo / Redo System

### Entity-Level Undo (`edit_undo.h`)

Ring buffer undo stack with dedicated snapshot arena:

- Default capacity: 4096 entries, 16 MB snapshot arena
- Each entry stores: forward/inverse command type, group ID, entity ID, optional snapshot pointer, compact delta (`float[4]`)
- Grouped undo: `begin_group` / `end_group` — all entries in a group are undone/redone atomically
- Snapshot arena: bump allocator for entity snapshots (used by delete undo to reconstruct entities)
- Ring wrapping silently discards oldest entries

Command type tags (`edit_cmd_type_t`): SPAWN, DELETE, MOVE, ROTATE, SCALE, GROUP_CREATE, GROUP_DELETE.

### Mesh-Level Undo (`mesh_undo.h`)

Separate undo system for mesh editing:
- Full-slot snapshots (positions, normals, indices, polygroup IDs)
- Max depth: 64 entries
- `mesh_undo_push` / `mesh_undo` / `mesh_redo`

---

## 6. Selection System

### Entity Selection (`edit_selection.h`)

Sorted array of selected entity IDs:
- Max 4096 simultaneously selected entities
- O(log n) lookup via binary search on sorted array
- Version counter increments on every change (dirty tracking for sync)
- API: `add`, `remove`, `toggle`, `clear`, `contains`, `count`, `ids`

### Mesh Element Selection

Per-element bitsets in `mesh_edit_t`:
- `sel_vertices`, `sel_edges`, `sel_faces` — dynamic bitsets
- Selection modes: VERTEX, EDGE, FACE, POLYGROUP, OBJECT
- Selection conversion: face<->vertex, face<->edge (via edge table)
- Topology selection: edge loops, edge rings, grow/shrink

### Selection Groups

Named groups (prefix `&`) stored in `edit_cmd_ctx_t.groups`:
- Up to 64 groups, 4096 entities per group
- Each group has: name, entity IDs, count, pivot point, optional parent group
- Full undo support for group create/delete

---

## 7. Scene Editor (GUI)

### Overview

Standalone SDL2/OpenGL application (`scene_main.c`) that connects to a game server and provides a graphical editing interface. Uses Clay for immediate-mode UI layout.

### Context (`scene_editor_t`)

Top-level aggregation:
- SDL2 window + OpenGL context
- `panel_layout_t` — four-panel layout with draggable dividers
- `snap_state_t` — per-editor grid/snap state
- `clay_backend_t` — Clay UI renderer
- `arena_t` — main editor arena allocator
- `scene_connection_t` — TCP/UDP server connection
- `scene_sync_t` — offline queue and sync state
- `edit_entity_store_t` — local entity mirror
- `edit_selection_t` — selected entity set
- `viewport_render_state_t` — 3D viewport (FBO, shaders, meshes, camera)
- `scene_ui_state_t` — interactive UI state (actions, scroll, mouse, TUI input)

### Panel Layout (`scene_panel.h`)

Four panel regions with draggable dividers:

```
  OUTLINER (left) | VIEWPORT (center-top)   | INSPECTOR (right)
                  | TUI (center-bottom)      |
```

- Three dividers: LEFT (outliner/viewport), RIGHT (viewport/inspector), BOTTOM (viewport/TUI)
- Divider positions stored as fractions [0,1] of window dimensions
- Min panel size: 40px
- Toggle visibility per panel; focus tracking with Tab cycling
- Persistence: save/load divider positions to file

### 3D Viewport Rendering (`scene_viewport_render.h`)

Off-screen FBO rendered 3D scene displayed as Clay UI image element:

- Framebuffer with color texture + depth renderbuffer
- 9-pass `render_pipeline_t` (forward + debug passes)
- Blinn-Phong shader for entities, separate unlit shader for grid
- Mesh registry for all entity shapes:
  - Built-in primitives: box, sphere, capsule, halfspace plane
  - Per-entity FVMA mesh cache for MESH type entities
- Entity rendering: all 6 types supported, selection-highlighted entities drawn in orange
- Grid: dynamic line geometry

### Editor Camera (`viewport_camera.h`)

Orbit camera with:
- Focus point + yaw + pitch + distance (spherical coordinates)
- Perspective and orthographic projection
- Controls: orbit (yaw/pitch delta), pan (screen-space), zoom (dolly)
- Snap views: Front(1), Back(Ctrl+1), Right(3), Left(Ctrl+3), Top(7), Bottom(Ctrl+7)
- Toggle perspective/orthographic
- Frame selection (fit AABB)
- Screen-to-ray casting for entity picking

### Transform Gizmo (`viewport_gizmo.h`)

Per-axis transform manipulation:
- Modes: Translate, Rotate, Scale
- Axis IDs: X, Y, Z, NONE
- Hit testing: ray vs axis-aligned geometry (cylinders for translate, tori for rotate, cubes for scale)
- Constrained drag delta: projects movement onto active axis

### Selection Raycast (`selection_raycast.h`)

Picking and box selection:
- Ray-AABB intersection (slab method)
- Ray-sphere intersection
- View frustum extraction from camera
- Frustum-AABB intersection test
- `pick_nearest_entity()` — test ray against candidate AABBs

### UI Panels (Clay Layout)

**Outliner** (`scene_ui_outliner.c`):
- Create buttons (spawn box/sphere/capsule)
- Entity list with click-to-select/deselect
- Outliner tree model with text filtering and scrolling

**Inspector** (`scene_ui_inspector.c`):
- Selected entity properties (position, rotation, scale as vec3 widgets)
- Float widgets with min/max clamping
- Dropdown widgets, checkbox widgets
- Delete button for selected entities

**Viewport** (`scene_ui_viewport.c`):
- 3D viewport texture from FBO displayed as Clay image element

**TUI** (`scene_ui_tui.c`):
- Log ring buffer display (normal + error lines)
- Command input line (activated by `:`)
- Sync status display

**Toolbar** (`panel_toolbar.h`):
- Transform mode buttons (Translate, Rotate, Scale)
- Snap toggle button

### UI Actions

UI interactions produce `scene_ui_action_t` values dispatched after layout:
- SPAWN_BOX, SPAWN_SPHERE, SPAWN_CAPSULE
- SELECT_ENTITY, DESELECT_ENTITY
- DELETE_SELECTED
- MODE_TRANSLATE, MODE_ROTATE, MODE_SCALE
- TUI_COMMAND (execute text command)

### Editor Modes (`mode_manager.h`)

Mode vtable dispatch with enter/exit callbacks:
- Currently supports OBJECT mode
- Max 8 registered modes
- Mode switch triggers exit on old, enter on new

### Snap State (`snap_state.h`)

Per-transform-type grid snap:
- Independent enable/grid-size for: POSITION (default 1.0), ROTATION (default 15.0 degrees), SCALE (default 0.1)
- Per-axis toggles (X, Y, Z) per transform type
- `snap_state_quantize()` — snap value to grid respecting axis toggles

---

## 8. Mesh Editing

### Mesh Slot (`mesh_slot.h`)

Single indexed triangle mesh with full vertex attributes:
- Positions (vec3), normals (vec3), tangents (vec4), 2 UV channels (vec2 each), colors (vec4)
- Triangle indices (uint32), per-face polygroup IDs (uint16)
- Dynamic buffer growth (doubling strategy)
- Max 4M vertices, 12M indices
- API: `init`, `destroy`, `clear`, `face_count`, `reserve_vertices`, `reserve_indices`, `add_vertex`, `add_triangle`

### Mesh Edit Subsystem (`mesh_edit.h`)

Top-level management of editable meshes:
- 16 simultaneously editable mesh slots (`MESH_MAX_EDITABLE`)
- Active slot index tracking
- Per-element selection bitsets (vertices, edges, faces)
- 5 selection modes: VERTEX, EDGE, FACE, POLYGROUP, OBJECT

### Mesh Operations

**Primitives** (`mesh_primitives.h`):
- Box (with per-axis segments)
- Plane (with subdivisions, configurable up axis)
- Cylinder (radius, height, segments, axis)
- Sphere (UV sphere with configurable segments/rings)

**Topology editing:**
- Extrude (region and individual face)
- Inset, outset
- Bevel (edge and vertex)
- Subdivision (linear and Loop)
- Merge (by distance), collapse (edge/face)
- Plane clipping
- Split / detach selection
- Edge loop bridge
- Triangulation

**UV mapping** (`mesh_uv.h` and related):
- Projection: planar, box (triplanar), cylindrical, spherical
- Seam marking
- UV transforms (translate, rotate, scale)
- Smart unwrap (angle-based)
- UV island detection
- UV island packing
- Texel density equalization

**Materials:**
- Per-face material assignment
- Material lift (read from face)
- Material replace (across mesh)

**CSG** (`mesh_csg.h`):
- Hollow (create inner shell with configurable thickness)
- Boolean union (merge)
- Boolean subtraction
- Boolean intersection

**Brush geometry** (`mesh_brush.h`):
- Quake/TrenchBroom-style brush: convex mesh from half-plane intersection
- Clips initial bounding box with each plane to produce convex solid

**Transfer** (`mesh_transfer.h`):
- Deep copy mesh
- Paste (append) mesh data
- Wavefront OBJ import/export

**Normal recalculation** (`mesh_normals.h`):
- Smooth, flat, area-weighted modes

### FVMA Binary Format (`mesh_vao_format.h`)

Wire format for mesh serialization (little-endian):
```
[4] magic 'FVMA' (0x414D5646)
[4] version (1)
[4] vertex_count
[4] index_count
[4] flags (attribute presence bitmask)
[4] polygroup_count
--- conditional attribute data ---
positions, normals, tangents, uv0, uv1, colors, indices, polygroup_ids
```

Flags: NORMALS (bit 0), TANGENTS (bit 1), UV0 (bit 2), UV1 (bit 3), COLORS (bit 4), BONES (bit 5).

API: `mesh_vao_serialized_size`, `mesh_vao_serialize`, `mesh_vao_deserialize`.

### Mesh Commit (`mesh_commit.h`)

Bakes editable mesh into world entity + FVMA binary:
- Serializes mesh to FVMA format
- Creates entity of type `EDIT_ENTITY_TYPE_MESH`
- Positions at mesh bounding box center
- Optional material override, optional slot clear
- Returns: entity ID, heap-allocated FVMA data (caller frees)

### Mesh Undo (`mesh_undo.h`)

Full-slot snapshot undo stack:
- Max 64 snapshots
- Each snapshot stores copies of positions, normals, indices, polygroup IDs
- Circular buffer — oldest discarded on overflow
- `mesh_undo_push` / `mesh_undo` / `mesh_redo`

---

## 9. TUI Editor Interface

### Controller (`ctrl_tui.h`)

Standalone terminal process connecting to editor server over TCP:
- Raw termios mode with ANSI escape rendering
- Double-buffered screen output
- Input modes: NORMAL (hotkeys), COMMAND (`:` prefix), REPL (script), GRAB (entity), CONTEXT (menu)
- Command-line editing (bottom bar)
- Log area (ring buffer)
- Terminal dimension tracking

### Command Definitions (`ctrl_cmd_defs.h`)

Static command table:
- Each entry: name, alias, usage, help, argument format
- Argument format descriptors: `s:` (string), `f:` (float), `f3:` (vec3), `u:` (uint), `b:` (bool)
- `ctrl_cmd_build_json()` — convert text input to JSON command
- `ctrl_cmd_complete()` — tab-completion candidates
- `ctrl_cmd_build_entity_def_json()` — multi-line entity_def block to JSON

### Mesh Mode Keybindings (`ctrl_mesh_mode.h`)

Mode-dependent key dispatch:
- Face mode: e=extrude, i=inset, g=grow, G=shrink
- Edge mode: b=bevel, c=loop_cut, x=edge_ring, l=edge_loop
- All modes: Tab=wireframe, ~=xray, u=unwrap, 1-5=mode switch
- Display state: wireframe toggle, x-ray toggle
- Statistics: selected count, vertex count, triangle count

---

## 10. Connection and Sync

### Server Connection (`ctrl_conn.h`)

Non-blocking TCP connection:
- 32 MB receive buffer (handles asset transfers)
- JSON protocol: `{"id":N,"cmd":"name","args":{...}}\n`
- Response: `{"id":N,"ok":true,"result":...}` or `{"id":N,"ok":false,"error":"code"}`
- Auto-incrementing request IDs for correlation

### Scene Connection (`scene_connection.h`)

Dual-channel connection for scene editor:
- TCP control channel (via `ctrl_conn_t`) — edit commands
- UDP replication channel (via `net_udp_socket_t`) — world snapshots
- Connection states: DISCONNECTED, CONNECTED, ERROR
- `scene_connection_send_cmd()` — format and send command
- `scene_connection_pump()` — non-blocking read from both channels
- `scene_connection_pop_response()` — extract complete response lines

### Scene Sync (`scene_sync.h`)

Google Drive-style sync with offline support:
- States: IDLE (all flushed), SYNCING (commands in flight), OFFLINE (queued locally)
- Offline edit queue: circular buffer of command strings (default 256 slots, max 1024 chars each)
- In-flight tracking: count of sent-but-not-acked commands
- Queue / dequeue operations (FIFO)
- Force save support (`:save force`)

### Scene Frame (`scene_frame.h`)

Per-frame update:
- `scene_frame_pump()` — read TCP/UDP, parse responses, update local entity store
- `scene_frame_dispatch_action()` — convert UI actions to server commands
- `scene_frame_request_entity_list()` — request full entity list from server

---

## 11. Asset Registry

### Server-Side Registry (`edit_asset_registry.h`)

Asset catalog with directory scanning:
- Types: MESH (.glb, .obj), TEXTURE (.png, .ktx2, .jpg), MATERIAL (.mat), PREFAB (.prefab), SCRIPT (.wren, .ed), UNKNOWN
- Entries: path (max 256 chars), type, size, CRC32 hash
- `edit_asset_registry_scan()` — recursive directory walk
- Queries: `find` (exact path), `list` (prefix + type filter), `search` (regex + type filter), `complete` (path prefix)

### Asset Serving (`edit_asset_serve.h`)

Server-side TCP asset transfer for on-demand client downloads.

### Client Asset Cache

Client-side caching:
- `client_asset_download.c` — download assets over TCP
- `client_asset_cache.c` / `client_asset_cache_io.c` — local file cache

---

## 12. Scripting Integration

### Script Environment (`edit_script_env.h`)

Data structures for the script thread:

**Entity snapshots** (tick thread -> script thread):
- `script_entity_snapshot_t` — read-only copy of entity state (pos, rot, scale, type, name, materials, attrs)
- `script_entity_view_t` — view into snapshot array
- `script_snapshot_build()` — build snapshots from entity store

**Update blob** (script thread -> tick thread):
- `script_update_buffer_t` — double-buffered blob with atomic ready flag
- Packed `script_entity_update_t` headers + `script_attr_write_t` + payloads
- `script_env_write_attr()` — append attribute write to blob

**Script environment** (`script_env_t`):
- Read-only entity view
- Write-only update blob
- Command ring for structural commands (spawn, delete, group)
- Context: cursor position, selection snapshot

### Script Rebase (`edit_script_rebase.h`)

Applies script updates onto authoritative entity state:
- Iterates packed blob, looks up entities by ID
- Well-known keys (POS, ROT, SCALE, TYPE, BODY_IDX, NAME) applied to fixed fields
- User/dynamic keys applied to `entity_attrs_t`
- Returns counts of applied and skipped updates

---

## 13. Level Serialization

### JSON Formats (`edit_serialize.h`)

**Version 1** (entities only):
```json
{"version":1,"entities":[
  {"id":0,"type":"box","name":"wall_1",
   "pos":[0,5,0],"rot":[0,0,0],"scale":[1,1,1],
   "pivot_offset":[0,0,0],
   "materials":{"albedo":"textures/brick.png"},
   "attrs":{...}
  }, ...
]}
```

**Version 2** (entities + groups):
```json
{"version":2,"entities":[...],"groups":[
  {"name":"&walls","ids":[0,1,2],"pivot":[0,5,0],"parent":""},
  ...
]}
```

Deserialization supports both versions transparently. Entities restored at their original IDs where possible.

### Command History (`edit_history.h`)

Full command log for level reconstruction:
- Ring buffer (4096 entries) + JSONL file backing
- Each entry: seq, ISO-8601 timestamp, command name, raw args JSON, result JSON, context snapshot (cursor, selection, aliases), success flag
- `edit_history_record()` — capture after each dispatch
- `edit_history_flush()` — write pending entries to file
- Context snapshot: cursor position, selected entity IDs, active @ aliases

### Autosave (`edit_autosave.h`)

Server-side periodic and forced save:
- Configurable interval (default 30 seconds)
- Dirty tracking: mark on entity mutation
- Force save: immediate save on `:save force`
- `edit_autosave_should_save()` checks interval + dirty + force
- `edit_autosave_did_save()` clears flags

---

## 14. Editor Context (`editor_ctx.h`)

### Server-Side Context

Top-level aggregation of all server-side editor subsystems:

```
editor_ctx_t
├── edit_io_thread_t      — TCP I/O thread
├── edit_cmd_ring_t       — Commands: I/O thread -> tick thread
├── edit_cmd_ring_t       — Responses: tick thread -> I/O thread
├── edit_dispatch_t       — Command dispatch table
├── edit_undo_stack_t     — Undo/redo stack
├── edit_selection_t      — Entity selection set
├── edit_entity_store_t   — Entity storage
├── mesh_edit_t           — Mesh editing subsystem
├── edit_cmd_ctx_t        — Handler context (pointers into above)
└── editor_ctx_config_t   — Configuration
```

Configuration defaults: TCP port 9100, 4096 entities, 4096 undo depth, 1024 ring slots, 8192 bytes per ring payload, 32768 byte dispatch arena.

Lifecycle:
- `editor_ctx_init()` — allocate everything, register handlers, start I/O thread
- `editor_ctx_shutdown()` — stop I/O thread, destroy all subsystems
- `editor_tick_drain()` — per-tick: drain ring -> dispatch -> push responses
- `editor_ctx_set_bridge()` — attach physics bridge
- `editor_ctx_set_physics()` — attach physics sim control

---

## 15. Threading Model

```
I/O Thread                     Main Tick Thread              Script Thread
  │                                │                            │
  ├─ TCP accept / read             ├─ editor_tick_drain()       ├─ Read snapshots
  ├─ Parse newline-delimited JSON  ├─ Pop from cmd_ring         ├─ Run scripts
  ├─ Push to cmd_ring  ──────────→ ├─ edit_dispatch_exec()      ├─ Write update blob
  ├─ Pop from resp_ring ←───────── ├─ Push to resp_ring         ├─ swap(blob)
  └─ TCP write response            ├─ Physics bridge calls      │
                                   ├─ Undo recording            │
                                   ├─ script_rebase_apply() ←───┘
                                   └─ Autosave check
```

- Command ring: lock-free SPSC (I/O thread -> tick thread)
- Response ring: lock-free SPSC (tick thread -> I/O thread)
- Script update blob: double-buffered with atomic ready flag
- Entity store, selection, undo: tick-thread only
- Scene editor: single-threaded (SDL2 main loop)

---

## 16. JSON Protocol

### Wire Format

Newline-delimited JSON over TCP.

**Command:** `{"id":1,"cmd":"spawn","args":{"type":"box","pos":[0,5,0]}}\n`

**Success response:** `{"id":1,"ok":true,"result":{"entity_id":0}}\n`

**Error response:** `{"id":1,"ok":false,"error":"store_full"}\n`

### Built-in JSON Library

Custom zero-dependency JSON parser/writer (`json_parse.h`):
- Arena-based allocation for parsed values
- Types: object, array, string, number, boolean, null
- Writer builds JSON strings with proper escaping
- Accessor helpers for typed value extraction

---

## 17. Client Integration

### Client State

- `client_state_dispatch.c` — dispatches server state changes to renderer
- `client_state_push.c` — pushes entity/selection state to render thread
- `client_state_socket.h` — client-side TCP socket state
- `client_editor_input.c` — client-side editor keybinding layer

### Client Cursor

- `client_cursor.c` — cursor state management
- `client_cursor_render.c` — 3D cursor rendering

### Client Mesh Rendering

- `client_mesh_render.h` — mesh entity rendering on client side
