# Level Editor Architecture Specification

## 1. Overview

The level editor is a **Model-View-Controller** system distributed across three
processes that communicate over sockets:

```
┌─────────────────────┐         UDP (game)         ┌──────────────────────┐
│   SERVER (Model)    │◄──────────────────────────►│   CLIENT (View)      │
│                     │                             │                      │
│  - physics world    │    TCP (asset download)     │  - 3D cursor         │
│  - entity store     │◄───────────────────────────►│  - camera            │
│  - asset registry   │                             │  - asset preview     │
│  - script runtime   │                             │  - OpenGL 4.6        │
└─────────┬───────────┘                             └──────────────────────┘
          │
          │ TCP (edit protocol)
          │
┌─────────▼───────────┐
│  CONTROLLER (Editor) │
│                      │
│  - curses TUI        │
│  - command-line      │
│  - keybindings       │
│  - MCP server        │
│  - script REPL       │
└──────────────────────┘
```

### Process Roles

| Process    | Role   | Description |
|-----------|--------|-------------|
| Server     | Model  | Authoritative world state. Owns physics, entities, asset registry, script runtime. Persists the level to disk. |
| Client     | View   | Renders the world. In editor mode: free camera, 3D cursor overlay, grid, gizmos. No gameplay logic. |
| Controller | Editor | Terminal-based UI. Sends edit commands to server. Reads cursor/camera state from client. Hosts MCP server for AI agents. |

### Design Principles

1. **Procedural first** — every operation is a command; the GUI is a convenience
   layer over a command language. Levels can be built entirely via scripts.
2. **Server authoritative** — the server is the single source of truth. The
   controller and client are stateless views/input devices.
3. **Scriptable** — a lightweight embedded scripting language drives procedural
   generation, texture synthesis, and batch operations.
4. **AI-native** — the MCP server exposes the full command vocabulary so an AI
   agent can drive the editor with zero UI.
5. **Keyboard-first** — all operations are accessible via keyboard. Mouse is
   optional and supplementary.

---

## 2. Server Extensions for Editor Mode

The existing server (`fr_server_net_runtime_t`, `phys_world_t`, ECS) is extended
with the following editor-mode subsystems. These are conditionally compiled
(`EDITOR_ENABLE`) and have no impact on the game server path.

### 2.1 Edit Socket (TCP)

A TCP listener on a configurable port accepts controller connections. The
protocol is line-oriented (newline-delimited JSON or a compact text protocol)
for easy scripting and debugging.

**Why TCP, not UDP:** edit commands are low-volume, must be ordered and reliable,
and benefit from backpressure. The existing RUDP stream is designed for
real-time game traffic; editor commands have different requirements.

The edit socket runs on its own fiber in the networking job system, similar to
the existing per-client RUDP fibers.

### 2.2 Asset Registry

The server maintains an **asset registry** — a catalog of named assets (meshes,
textures, materials, prefabs, scripts) stored on disk in a project directory.

```
project/
├── assets/
│   ├── meshes/       # .glb, .obj
│   ├── textures/     # .png, .ktx2, synthesized
│   ├── materials/    # .mat (JSON)
│   ├── prefabs/      # .prefab (JSON + script ref)
│   └── scripts/      # .lua / .wren / .ed (editor scripts)
├── levels/
│   └── level_01.json # serialized entity list + world settings
└── project.json      # project metadata
```

Assets are referenced by **path relative to project root** (e.g.,
`assets/meshes/pillar.glb`). The registry supports:
- **Listing** with glob/prefix filtering (for tab-completion)
- **Hot-reload** via filesystem watch (inotify)
- **Import** from external files (copy + register)
- **Procedural creation** via scripts (texture synthesis, mesh generation)

### 2.3 Asset Downloader (TCP)

When the server has an asset that the client needs for rendering (e.g., a newly
synthesized texture or imported mesh), it transfers the asset over a dedicated
TCP connection — the **asset downloader**.

This is separate from the game UDP channel because:
- Assets can be large (megabytes)
- Transfer must be reliable and ordered
- Game traffic must not be blocked by asset transfers

The protocol:
1. Server notifies client of new/updated asset (reliable game channel or edit channel)
2. Client opens TCP connection to asset download port
3. Client requests asset by path
4. Server streams asset bytes with length prefix
5. Client stores in local cache, signals renderer to load

### 2.4 Entity Manipulation API

The server exposes a C API for entity manipulation that the edit socket handler
calls:

```c
// Create / destroy
entity_t     editor_spawn_entity(editor_ctx_t *ctx, const spawn_desc_t *desc);
void         editor_destroy_entity(editor_ctx_t *ctx, entity_t ent);

// Transform
void         editor_set_position(editor_ctx_t *ctx, entity_t ent, vec3_t pos);
void         editor_set_rotation(editor_ctx_t *ctx, entity_t ent, quat_t rot);
void         editor_set_scale(editor_ctx_t *ctx, entity_t ent, vec3_t scale);

// Components
void         editor_set_component(editor_ctx_t *ctx, entity_t ent,
                                  const char *comp_name, const void *data);
void         editor_remove_component(editor_ctx_t *ctx, entity_t ent,
                                     const char *comp_name);

// Queries
uint32_t     editor_query_entities(editor_ctx_t *ctx, const query_desc_t *q,
                                   entity_t *out, uint32_t max);

// Undo/redo
void         editor_undo(editor_ctx_t *ctx);
void         editor_redo(editor_ctx_t *ctx);
```

All mutations go through an **undo stack** (command pattern). Each command
records the inverse operation for undo.

### 2.5 Script Runtime

An embedded scripting language provides procedural generation and automation.
Candidate languages (in order of preference):

1. **Lua 5.4** — minimal, embeddable, fast, well-understood
2. **Wren** — class-based, embeddable, small
3. **Custom DSL** — if neither fits, a minimal expression language

The script runtime:
- Runs in a dedicated fiber (no blocking the physics tick)
- Has access to the full entity manipulation API
- Can register new commands (extending the command vocabulary)
- Provides texture synthesis primitives (noise, blend, warp, etc.)
- Supports coroutines for multi-frame operations (e.g., animated generation)

### 2.6 Level Serialization

Levels are serialized as JSON:
```json
{
  "version": 1,
  "world_settings": {
    "gravity": [0, -9.81, 0],
    "bounds": [[-100, -10, -100], [100, 100, 100]]
  },
  "entities": [
    {
      "id": "pillar_01",
      "prefab": "assets/prefabs/stone_pillar.prefab",
      "position": [10.0, 0.0, 5.0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1],
      "components": {
        "physics": { "mass": 0, "shape": "mesh", "collider": "assets/meshes/pillar_col.glb" },
        "render": { "mesh": "assets/meshes/pillar.glb", "material": "assets/materials/stone.mat" }
      }
    }
  ]
}
```

The server can save/load levels at any time. The controller provides
`save <path>` and `load <path>` commands.

---

## 3. Client Extensions for Editor Mode

### 3.1 Editor Mode Toggle

The client starts in **editor mode** when launched with `--editor` (or connects
to a server that advertises editor mode). In editor mode:

- Free-fly camera (no player entity)
- 3D cursor visible at all times
- Grid overlay on the XZ plane
- Gizmo rendering for selected entities
- No gameplay HUD

### 3.2 3D Cursor

The 3D cursor is a wireframe crosshair that exists in world space. It can be
moved:

- **Keyboard:** arrow keys / HJKL move on the grid plane; PgUp/PgDn move
  vertically. Grid snap is configurable.
- **Mouse:** click to place cursor at raycast hit point (snapped to grid if
  enabled).
- **Command:** `cursor <x> <y> <z>` sets position directly.

The cursor position is stored on the client and **replicated to the controller**
so commands like `spawn at cursor` work.

### 3.3 Selection

Entities can be selected by:
- Clicking on them
- Box select (drag)
- Command: `select <entity_id>`, `select all`, `select none`

Selected entities are highlighted (outline shader or tint).

### 3.4 Asset Preview

When browsing assets in the controller's command-line, the client shows a
preview of the highlighted asset (floating near the cursor or in a corner
viewport).

---

## 4. Controller (Terminal Editor Process)

### 4.1 Architecture

The controller is a standalone C program using a terminal UI library (raw
termios or ncurses). It connects to:

1. **Server edit socket (TCP)** — sends commands, receives responses and events
2. **Client state socket (TCP)** — reads cursor position, camera state, selection

Layout:
```
┌────────────────────────────────────────────────────────┐
│ [Status bar: server, cursor pos, grid size, mode]      │
├────────────────────────────────────────────────────────┤
│                                                        │
│  [Log / output area — scrollable]                      │
│                                                        │
│                                                        │
├────────────────────────────────────────────────────────┤
│ > command input with tab-completion                    │
└────────────────────────────────────────────────────────┘
```

### 4.2 Command Language

Commands are verb-first, space-separated:

```
spawn box 2 2 2                    # spawn a 2×2×2 box at cursor
spawn prefab assets/prefabs/tree   # spawn prefab at cursor
move 0 1 0                         # move selection up 1 unit
rotate 0 45 0                      # rotate selection 45° around Y
delete                             # delete selection
select all where physics.mass > 0  # query select
undo                               # undo last operation
redo                               # redo
save levels/test.json              # save level
load levels/test.json              # load level
run scripts/gen_forest.lua         # run script
texture synth stone01 512 512      # synthesize texture
bind ctrl+d "delete"               # bind key to command
```

### 4.3 Tab Completion

Tab completes:
- Command names
- Asset paths (fetched from server registry)
- Entity names/IDs
- Component names
- Script function names

The server provides a `complete <prefix>` query that returns matching
candidates.

### 4.4 Keybinding System

All keybindings are configurable via `bind <key> <command>`. Default bindings
are loaded from a config file (`editor.conf`).

### 4.5 MCP Server

The controller hosts an **MCP (Model Context Protocol) server** on a
configurable port. This allows an AI agent (e.g., Claude, GPT) to:

- Send any command from the command vocabulary
- Read world state (entity list, properties, cursor position)
- Browse assets (list, search, preview metadata)
- Run scripts
- Observe command output

The MCP server exposes **tools** that map 1:1 to the command vocabulary, plus
**resources** for world state queries.

---

## 5. Communication Protocols

### 5.1 Edit Protocol (Controller ↔ Server)

TCP, newline-delimited JSON messages.

**Request:**
```json
{"id": 1, "cmd": "spawn", "args": {"type": "box", "size": [2,2,2], "pos": [10,0,5]}}
```

**Response:**
```json
{"id": 1, "ok": true, "result": {"entity": "box_042"}}
```

**Event (server → controller):**
```json
{"event": "entity_changed", "entity": "box_042", "changes": {"position": [10,1,5]}}
```

### 5.2 Client State Protocol (Controller ↔ Client)

TCP, newline-delimited JSON. Read-only queries + cursor movement commands.

**Query:**
```json
{"query": "cursor"}
```
**Response:**
```json
{"cursor": [10.0, 0.0, 5.0], "grid_size": 1.0, "snap": true}
```

**Command (controller → client):**
```json
{"cmd": "set_cursor", "pos": [10, 0, 5]}
```

### 5.3 Asset Download Protocol (Client ↔ Server)

TCP, binary.

```
[REQ]  asset_path_len:u16 LE | asset_path:utf8
[RESP] status:u8 | total_len:u32 LE | data:bytes
```

Chunked transfer for large assets (chunk size = 64KB).

---

## 6. Texture Synthesis System

### 6.1 Script-Based (Not Node-Based)

Texture synthesis is driven by scripts, not a node graph. This aligns with the
"procedural first" principle and makes textures version-controllable and
AI-generatable.

Example script (Lua):
```lua
function stone_wall(w, h)
    local base = noise.voronoi(w, h, {cells=32, jitter=0.8})
    local cracks = noise.fractal(w, h, {octaves=6, lacunarity=2.1})
    local color = blend.multiply(
        colorize(base, {0.6, 0.55, 0.45}, {0.4, 0.35, 0.3}),
        remap(cracks, 0.7, 1.0)
    )
    local normal = normal_from_height(blend.add(base, cracks, 0.3), 2.0)
    return {
        albedo = color,
        normal = normal,
        roughness = remap(base, 0.6, 0.9)
    }
end
```

### 6.2 Bake-to-UV-Map

Synthesized textures can be baked onto a mesh's UV layout:

```
texture bake stone01 assets/meshes/wall.glb --uv-set 0 --resolution 1024
```

The server:
1. Loads the mesh UV layout
2. Rasterizes the texture function per texel in UV space
3. Writes the result to `assets/textures/stone01_albedo.png` (etc.)
4. Registers the new asset
5. Notifies clients for download

### 6.3 Synthesis Primitives

Available to scripts:
- `noise.perlin(w, h, params)` — Perlin noise
- `noise.simplex(w, h, params)` — Simplex noise
- `noise.voronoi(w, h, params)` — Voronoi / Worley
- `noise.fractal(w, h, params)` — fBm / ridged multi-fractal
- `blend.add/multiply/overlay/screen(a, b, factor)`
- `colorize(heightmap, color_lo, color_hi)` — gradient map
- `normal_from_height(heightmap, strength)` — Sobel filter
- `warp(image, displacement, strength)` — domain warping
- `remap(image, lo, hi)` — range remap
- `tile_check(image)` — verify seamless tiling

---

## 7. Integration with Existing Engine

### 7.1 Server-Side

| Existing Module | Editor Extension |
|----------------|------------------|
| `phys_world_t` | `editor_spawn_entity()` creates bodies; `editor_set_position()` teleports |
| `fr_server_net_runtime_t` | Edit socket fiber joins the net job system |
| ECS (`src/ecs/`) | Editor components: `editor_name_t`, `editor_prefab_ref_t` |
| Fiber job system | Script runtime fiber, edit socket fiber |
| Snapshot replication | New entities replicate normally after spawn |

### 7.2 Client-Side

| Existing Module | Editor Extension |
|----------------|------------------|
| SDL2 + OpenGL renderer | 3D cursor rendering, gizmos, grid overlay, selection highlight |
| Pose interpolator | Works as-is for editor-spawned entities |
| Video capture | Useful for recording editor sessions |
| Input handling | Editor mode keybindings (cursor movement, camera) |

### 7.3 New Modules

| Module | Location | Description |
|--------|----------|-------------|
| Edit protocol | `src/editor/protocol/` | TCP listener, command parse/dispatch |
| Asset registry | `src/editor/assets/` | Catalog, hot-reload, import |
| Asset downloader | `src/editor/download/` | TCP asset transfer |
| Script runtime | `src/editor/script/` | Lua embed, API bindings |
| Texture synthesis | `src/editor/texsynth/` | Noise generators, blending, bake |
| Undo system | `src/editor/undo/` | Command stack, inverse operations |
| Controller TUI | `src/editor/controller/` | Terminal UI, command-line, keybinds |
| MCP server | `src/editor/mcp/` | JSON-RPC over stdio/TCP for AI agents |
| Level serialization | `src/editor/level/` | JSON save/load |
| 3D cursor | `src/editor/cursor/` | Client-side cursor + sync |

---

## 8. Build Configuration

```makefile
# Editor-enabled server
make build/editor_server EDITOR=1

# Editor-mode client
make build/editor_client EDITOR=1

# Controller (always an editor tool)
make build/editor_ctrl

# All editor tools
make editor
```

Compile flags:
- `EDITOR_ENABLE` — gates editor code in server/client
- `LUA_ENABLE` — gates Lua runtime (optional; editor works without scripts
  but procedural features require it)

---

## 9. Non-Goals (Explicit Exclusions)

- **Visual scripting / node graph** — scripts are text, not graphs
- **Embedded GUI in the 3D viewport** — the terminal is the primary interface
- **Asset creation tools** (modeling, rigging) — use external tools; editor
  imports results
- **Multiplayer editing** — single editor connection per server (initially)
- **Undo across sessions** — undo stack is in-memory, cleared on disconnect
