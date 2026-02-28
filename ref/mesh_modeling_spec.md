# Mesh Modeling Mode Specification

## Overview

This document specifies a **Mesh Modeling Mode** for the Ferrum RPG editor — a static-mesh-based geometry editing system inspired by Unreal Engine's Scythe, Source 2 Hammer, and Quake/Half-Life brush editors. The system operates within the existing three-process architecture (Server-Model / Client-View / Controller-TUI), extending the command protocol with mesh manipulation primitives.

The mesh modeling mode treats geometry as **editable static meshes** rather than brush-based CSG. This provides the tactile immediacy of TrenchBroom's brush editing with the flexibility of modern static mesh workflows.

---

## Architecture Integration

### Server-Side: Mesh Geometry Engine

The server gains a new subsystem `mesh_edit_t` that manages indexed VAO (Vertex Array Object) data for editable geometry:

```
┌─────────────────────────────────────────────────────────────────┐
│                      MESH EDIT SUBSYSTEM                         │
├─────────────────────────────────────────────────────────────────┤
│  mesh_slot_t slots[MESH_MAX_EDITABLE];                          │
│  ├── slot 0: "@active" (primary work mesh)                      │
│  ├── slot 1-7: "@scratch" (clipboard, stamps, primitives)       │
│  └── slot 8-15: "@temp" (preview ghosts, extrusion guides)      │
├─────────────────────────────────────────────────────────────────┤
│  Geometry Buffers (indexed triangle meshes):                    │
│  ├── positions  (vec3, attrib 0)                                │
│  ├── normals    (vec3, attrib 1)                                │
│  ├── tangents   (vec4, attrib 2)                                │
│  ├── uvs[2]     (vec2, attrib 3-4)                              │
│  ├── colors     (vec4, attrib 5) -- vertex painting             │
│  └── indices    (u32)                                           │
├─────────────────────────────────────────────────────────────────┤
│  Selection State:                                               │
│  ├── sel_vertices  (dynamic bitset)                             │
│  ├── sel_edges     (dynamic bitset, edge indices)               │
│  ├── sel_faces     (dynamic bitset, triangle indices)           │
│  ├── sel_polygroups (u16 per-face group IDs)                    │
│  └── active_slot   (which mesh is being edited)                 │
└─────────────────────────────────────────────────────────────────┘
```

**Key Design Decision:** Mesh data lives server-side. The server performs all geometric operations (extrude, subdivide, etc.) and regenerates the indexed VAO. The client receives **complete mesh snapshots** over the asset download TCP channel, similar to how textures/meshes are currently transferred. This keeps the client thin and the server authoritative.

---

## Selection Modes

Mesh editing supports five selection topologies, accessible via the `mode` command:

| Mode | Target | Description |
|------|--------|-------------|
| `vertex` | Individual vertices | Direct vertex manipulation |
| `edge` | Edges (vertex pairs) | Edge loops, boundary edges |
| `face` | Individual triangles | Per-triangle operations |
| `polygroup` | Face groups | Logical mesh islands (smooth groups) |
| `object` | Entire mesh | Whole-mesh transforms |

**Command:** `mode {"type": "vertex|edge|face|polygroup|object"}`

---

## Command Vocabulary

### Selection Commands (Extended from Existing)

All existing selection commands (`select`, `select_near`, `select_regex`, `deselect`, etc.) gain a `scope` parameter to operate on mesh elements rather than entities.

**Existing entity selection (unchanged):**
```json
{"cmd": "select", "args": {"name": "pillar_01"}}
```

**New mesh element selection:**
```json
{"cmd": "select", "args": {"scope": "mesh", "type": "face", "indices": [0,1,2,5,8]}}
{"cmd": "select", "args": {"scope": "mesh", "type": "polygroup", "id": 3}}
{"cmd": "select", "args": {"scope": "mesh", "type": "edge", "boundary": true}}
{"cmd": "select", "args": {"scope": "mesh", "type": "face", "planar": true, "normal": [0,1,0], "threshold": 5.0}}
```

| Command | Args | Description |
|---------|------|-------------|
| `select` | `scope`, `type`, various filters | Add to selection |
| `deselect` | `scope`, `type` | Remove from selection |
| `select_all` | `scope` | Select all elements in current mode |
| `select_none` | `scope` | Clear selection |
| `invert_selection` | `scope` | Invert current selection |
| `select_ring` | `edge_index` | Select edge ring (parallel edges) |
| `select_loop` | `edge_index` | Select edge loop (continuous ring) |
| `select_flood` | `face_index` | Flood fill select connected faces |
| `select_similar` | `normal`, `area`, `polygroup` | Select by similarity |
| `grow_selection` | `scope`, `steps` | Expand selection by N rings |
| `shrink_selection` | `scope`, `steps` | Contract selection |

---

### Geometry Creation Commands

| Command | Args | Description | Scythe Equiv |
|---------|------|-------------|--------------|
| `mesh_create` | `type`, `params` | Create primitive mesh in @active | Block Tool |
| `mesh_create_box` | `size`, `segments`, `pos` | Axis-aligned box | Add Block |
| `mesh_create_cylinder` | `radius`, `height`, `segments`, `axis` | Cylinder primitive | — |
| `mesh_create_sphere` | `radius`, `segments` | UV sphere | — |
| `mesh_create_plane` | `size`, `segments`, `axis` | Grid plane | — |
| `mesh_create_from_brush` | `planes[]` | Create from brush planes (TrenchBroom style) | — |

**Example:**
```json
{"cmd": "mesh_create_box", "args": {"size": [64, 128, 64], "segments": [2,4,2], "pos": [0,64,0]}}
{"cmd": "mesh_create_from_brush", "args": {"planes": [
    {"normal": [0,1,0], "dist": 128},
    {"normal": [0,-1,0], "dist": 0},
    {"normal": [1,0,0], "dist": 64},
    {"normal": [-1,0,0], "dist": 64},
    {"normal": [0,0,1], "dist": 64},
    {"normal": [0,0,-1], "dist": 64}
]}}
```

---

### Transform Commands

| Command | Args | Description | Scythe Equiv |
|---------|------|-------------|--------------|
| `move` | `offset` or `to`, `scope` | Translate selection | Gizmo drag |
| `rotate` | `axis`, `angle`, `pivot` | Rotate selection | Gizmo rotate |
| `scale` | `factors` or `to_size`, `pivot` | Scale selection | Gizmo scale |
| `transform_uv` | `offset`, `scale`, `rotate` | Transform UVs of selected faces | Transform Texture |
| `align_to_grid` | `scope` | Snap vertices to grid | Vertices to Grid |

**Note:** These extend existing entity transform commands with `scope: "mesh"` to operate on selected vertices/edges/faces.

---

### Topological Editing Commands (Core Modeling)

| Command | Args | Description | Scythe Equiv |
|---------|------|-------------|--------------|
| `extrude` | `distance`, `direction` (or `normal`), `segments` | Extrude faces along normal | Extrude |
| `extrude_individual` | `distance` | Extrude each face independently | — |
| `extrude_along_curve` | `curve_points[]`, `segments` | Extrude along a path | — |
| `inset` | `amount`, `depth` | Inset faces (beveled extrude) | Inset |
| `outset` | `amount` | Push faces outward | Outset |
| `bevel` | `amount`, `segments`, `profile` | Chamfer/bevel edges or vertices | Bevel |
| `bridge` | `edge_indices[]` or `face_indices[]` | Bridge between selections | Bridge |
| `connect` | `edge_indices[]` | Connect edges with loop cut | Connect |
| `subdivide` | `method`: "catmull-clark", "loop", "linear", `levels` | Subdivide mesh | — |
| `merge` | `target`: "center", "cursor", "last", "first", `threshold` | Weld/weld selected | Merge Faces |
| `collapse` | `type`: "edge", "face" | Collapse selection to center | — |
| `detach` | `keep_original` | Detach selection as new mesh | Detach |
| `split` | — | Split shared vertices at selection | — |
| `flip_normals` | — | Reverse face winding | Invert |
| `recalculate_normals` | `method`: "flat", "smooth", "weighted" | Regenerate normals | Soft/Hard Normals |
| `triangulate` | `method`: "ear_clip", "delaunay" | Convert ngons to tris | — |
| `quadrangulate` | `angle_threshold` | Convert tris to quads where possible | — |

**Extrude Examples:**
```json
// Standard extrusion
{"cmd": "extrude", "args": {"distance": 16.0, "normal": true}}

// Individual face extrusion (keeps faces separate)
{"cmd": "extrude_individual", "args": {"distance": 8.0}}

// Inset with bevel
{"cmd": "inset", "args": {"amount": 4.0, "depth": 2.0}}

// Edge bevel (chamfer)
{"cmd": "bevel", "args": {"amount": 2.0, "segments": 2, "profile": "concave"}}
```

---

### UV Mapping Commands

| Command | Args | Description | Scythe Equiv |
|---------|------|-------------|--------------|
| `unwrap` | `method`: "planar", "box", "cylindrical", "spherical", `axis` | Project UVs | — |
| `unwrap_smart` | `angle_threshold`, `stretch_weight` | Auto unwrap to minimize stretch | — |
| `seam_mark` | `edge_indices[]` | Mark edges as UV seams | — |
| `seam_clear` | `edge_indices[]` | Unmark seam edges | — |
| `pack_uvs` | `padding`, `resolution` | Pack islands to 0-1 space | — |
| `align_uv_to_grid` | `grid_size` | Snap UVs to grid (texel align) | Align to Grid |
| `fit_uv` | `axis`: "x", "y", "both" | Scale UV to fit 0-1 | Fit to Height |
| `shift_uv` | `offset` | Translate UV coordinates | Shift |
| `rotate_uv` | `angle`, `pivot` | Rotate UV coordinates | Rotate |
| `scale_uv` | `factors`, `pivot` | Scale UV coordinates | Scale |
| `wrap_texture` | `source_face`, `target_faces[]` | Flow UVs across connected faces | Wrap Texture |

---

### Material Commands

| Command | Args | Description | Scythe Equiv |
|---------|------|-------------|--------------|
| `material_assign` | `material_path`, `face_indices[]` | Apply material to faces | Apply Material |
| `material_lift` | `face_index` | Sample material from face | Lift Material |
| `material_replace` | `old_path`, `new_path` | Replace all occurrences | — |
| `hotspot_apply` | `hotspot_name` | Apply texel density hotspot | Hotspot |

---

### Brush/CSG Commands (TrenchBroom Heritage)

For rapid blockout, support brush-style construction that generates meshes:

| Command | Args | Description | TrenchBroom Equiv |
|---------|------|-------------|-------------------|
| `clip` | `plane_point`, `plane_normal`, `keep`: "front", "back", "both" | Split mesh by plane | Clip Tool |
| `csg_hollow` | `thickness` | Hollow out solid mesh | Hollow |
| `csg_merge` | `target_entities[]` | Boolean union | CSG Merge |
| `csg_subtract` | `cutter_entity` | Boolean difference | CSG Subtract |
| `csg_intersect` | `target_entity` | Boolean intersection | — |

---

### Data Transfer Commands

| Command | Args | Description |
|---------|------|-------------|
| `mesh_import` | `path`, `slot` | Import external mesh file |
| `mesh_export` | `path`, `format` | Export current mesh |
| `mesh_copy` | `from_slot`, `to_slot` | Copy between slots |
| `mesh_paste` | `from_slot`, `merge_mode` | Paste into active |
| `mesh_clear` | `slot` | Empty mesh slot |
| `mesh_commit` | `entity_name`, `material_override` | Bake mesh to world entity |

---

## Client Rendering Requirements

The client receives mesh data as a **binary blob** over the asset download TCP connection when changes occur:

```
Server                          Client
  │                               │
  ├── mesh_vao_update msg ───────►│ (notify change)
  │                               │
  │◄────── TCP download req ──────│ (request VAO data)
  │                               │
  ├── VAO binary blob ───────────►│ (positions, normals, uvs, indices)
  │                               │
  └── sel_update msg ─────────────►│ (selection highlight indices)
```

**VAO Binary Format:**
```
[4 bytes]  magic: 'FVMA' (Ferrum VMesh Asset)
[4 bytes]  version: 1
[4 bytes]  vertex_count
[4 bytes]  index_count
[4 bytes]  flags (has_normals, has_tangents, has_uv2, has_colors)
[4 bytes]  polygroup_count

[vertex_count * 12 bytes] positions (vec3)
[vertex_count * 12 bytes] normals (if flag set)
[vertex_count * 16 bytes] tangents (if flag set)
[vertex_count * 8 bytes]  uv0 (if flag set)
[vertex_count * 8 bytes]  uv1 (if flag set)
[vertex_count * 16 bytes] colors (if flag set)

[index_count * 4 bytes] indices (u32)
[face_count * 2 bytes]  polygroup IDs per triangle
```

**Selection Rendering:**
- Selected vertices: 8-pixel colored billboards (gizmo color by axis)
- Selected edges: 4-pixel colored lines with depth bias
- Selected faces: tinted overlay (multiply blend) + 2px highlight border
- Active polygroup: subtle tint different from selection

---

## TUI/CLI Interface

The controller TUI provides a command line with contextual help, similar to Vim's command mode. Commands follow the existing pattern:

```
:mesh mode face                    -- switch to face selection
:select flood 42                   -- flood select from face 42
:extrude distance 16               -- extrude selected faces
:inset amount 4 depth 2            -- inset with bevel
:unwrap method box axis y          -- box unwrap
:material assign assets/mats/stone -- apply material
```

**Modal Keybindings (Vim-inspired):**

| Key | Mode | Action |
|-----|------|--------|
| `1` | Global | Vertex selection mode |
| `2` | Global | Edge selection mode |
| `3` | Global | Face selection mode |
| `4` | Global | Polygroup mode |
| `5` | Global | Object mode |
| `g` | Face | Grow selection |
| `G` | Face | Shrink selection |
| `x` | Edge | Select edge ring |
| `l` | Edge | Select edge loop |
| `e` | Face | Extrude (prompts for distance) |
| `i` | Face | Inset |
| `b` | Edge | Bevel |
| `c` | Edge | Connect (loop cut) |
| `u` | UV | Unwrap submenu |
| `Ctrl+t` | Global | Triangulate |
| `Ctrl+q` | Global | Quadrangulate |
| `Tab` | Global | Toggle wireframe |
| `~` | Global | Toggle x-ray selection |

---

## Implementation Phases

### Phase 1: Foundation (Weeks 1-2)
- [ ] `mesh_slot_t` server data structures
- [ ] VAO binary format and asset downloader integration
- [ ] Client mesh rendering (basic)
- [ ] `mesh_create_box` and `mode` commands
- [ ] Face/vertex/edge selection commands

### Phase 2: Core Modeling (Weeks 3-4)
- [ ] `extrude` and `extrude_individual`
- [ ] `inset`, `outset`, `bevel`
- [ ] `bridge`, `connect`
- [ ] `merge`, `collapse`
- [ ] `subdivide` (Catmull-Clark)

### Phase 3: UV & Materials (Weeks 5-6)
- [ ] `unwrap` methods (planar, box, cylindrical)
- [ ] UV transform commands
- [ ] Material assignment
- [ ] Texel density tools

### Phase 4: Polish (Week 7)
- [ ] `clip` and CSG commands
- [ ] TUI visual feedback (selection counts, mesh stats)
- [ ] Undo/redo integration for all mesh ops
- [ ] Scripting bindings (Lua API)

---

## Example Session

```bash
# Create a simple room with extruded details
:mesh_create_box size [256,128,256] segments [4,2,4]
:mode face
:select planar normal [0,1,0] threshold 1.0    # select floor
:material assign assets/mats/floor_concrete
:deselect_all

:select planar normal [0,0,1]                   # select front wall
:select flood 8                                 # flood to connected faces
:inset amount 8                                 # create door frame
:extrude distance -4                            # recess door
:deselect_all

:mode edge
:select loop 42                                 # select vertical edge
:bevel amount 4 segments 2                      # chamfer corner
:mode face
:select_similar normal [0,1,0]                  # select all top faces (ceilings)

:unwrap method box
:material assign assets/mats/ceiling_tile
:align_uv_to_grid grid 64

:mesh_commit entity_name "room_01"
:save
```

---

## References

- **Scythe Editor Guide**: https://scytheeditor.com/guide/
- **TrenchBroom Manual**: https://trenchbroom.github.io/manual/latest/
- **Source 2 Hammer**: https://developer.valvesoftware.com/wiki/Docs/Level_Design
- **RPG Editor Spec**: `ref/editor_spec.md`
- **RPG Editor Design**: `ref/editor_design.md`

---

## Design Philosophy

This specification bridges the gap between:

1. **Brush-based editors** (TrenchBroom, Hammer, Radiant) — fast for blockout, immediate, tactile
2. **Modern mesh tools** (Scythe, Blender edit mode) — powerful, non-destructive, flexible
3. **Distributed architecture** — server authoritative, client thin, networked from the ground up

The result is a level editor where you can rapidly block out spaces with box primitives, extrude architectural details, UV map with texel density in mind, and bake to optimized static meshes — all through a CLI-first, TUI-optional interface that respects the Ferrum RPG architecture.
