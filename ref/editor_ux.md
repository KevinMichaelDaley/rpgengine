# Level Editor UX Specification

## 1. Philosophy

The editor UX is built on three principles:

1. **Terminal-native** — the primary interface is a curses-style terminal with
   a command-line. The 3D viewport (client) is a passive display.
2. **Keyboard-first** — every operation has a keyboard shortcut. Mouse is
   supported but never required.
3. **Command-driven** — all actions are commands. Keybindings map to commands.
   Scripts emit commands. The MCP server sends commands. There is one path.

This is modeled on the Vim/Blender/Emacs tradition: a dense, learnable
interface that rewards expertise with speed.

---

## 2. Terminal Layout

```
┌─ FERRUM EDITOR ─────────────────────────── level_01.json ──── 127.0.0.1:9100 ─┐
│ Cursor: (10.00, 0.00, 5.00)  Grid: 1.0m  Snap: ON  Mode: OBJECT              │
├───────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│  [entity_042] box 2×2×2 at (10, 0, 5) mass=0 static                           │
│  [entity_043] prefab:stone_pillar at (15, 0, 8)                                │
│  > Spawned 2 entities from script gen_courtyard.lua                            │
│  > Texture stone_wall_01 synthesized (1024×1024, 3 maps)                       │
│  > Saved level_01.json (847 entities)                                          │
│                                                                                 │
│                                                                                 │
│                                                                                 │
│                                                                                 │
│                                                                                 │
│                                                                                 │
├───────────────────────────────────────────────────────────────────────────────────┤
│ :spawn box 2 2 2                                                    [TAB: 3]   │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### Regions

| Region | Purpose |
|--------|---------|
| **Status bar** (top) | Server connection, cursor position, grid size, snap state, edit mode |
| **Log area** (middle) | Scrollable output: command results, script output, events, errors |
| **Command-line** (bottom) | Input with tab-completion. Shows completion count. |

### Color Scheme

- Status bar: inverse video (white on dark blue)
- Log: default terminal colors; errors in red; warnings in yellow
- Command-line: bold input; grey completion ghosts
- Entity references: cyan
- Numbers: green
- Paths: underlined

---

## 3. Command-Line Interface

### 3.1 Input Modes

The editor has several input modes:

| Mode | Description |
|------|-------------|
| **Normal** | Keyboard shortcuts active. Keys map to commands (e.g., `x` = delete, `g` = grab/move). Vim-style numeric prefixes supported (e.g., `5l` = move 5 units). |
| **Command** | Command-line focused. Type commands directly. Enter to execute, Escape to cancel. |
| **REPL** | Lua REPL mode. `lua>` prompt for interactive scripting. Exit with `exit()` or Ctrl+D. |
| **Grab** | Entity grab mode. Cursor keys move the grabbed entity. Enter to confirm, Escape to cancel. |
| **Context** | Context menu overlay. Only listed shortcut keys active. Escape to dismiss. |

Entering `:` from Normal mode switches to Command mode (like Vim).
Pressing Escape from any mode returns to Normal mode.

### 3.2 Tab Completion

Tab completion works on all arguments:

```
:spawn <TAB>
  box  sphere  capsule  mesh  prefab  light  trigger

:spawn prefab assets/<TAB>
  assets/prefabs/
  assets/meshes/

:spawn prefab assets/prefabs/<TAB>
  stone_pillar  wooden_beam  torch_holder  barrel

:select all where <TAB>
  physics.mass  physics.shape  render.mesh  render.material  position.y
```

**Completion sources:**
- Command names → built-in vocabulary
- Asset paths → server asset registry (`complete assets/ <prefix>`)
- Entity IDs → server entity list
- Component names → registered component schemas
- Script functions → script runtime introspection

Tab cycles forward; Shift+Tab cycles backward. A popup shows all candidates
when there are ≤ 20 matches.

**Async tab-completion:** for server-sourced completions (asset paths, entity
IDs, component names), the request is asynchronous. While waiting for the
server response, the command-line shows a `[...]` loading indicator after
the cursor. If the user types more characters before the response arrives,
the stale response is discarded and a new request is issued automatically.

### 3.3 Browse Result References

The `browse` command numbers its results. These numbers can be used as
shorthand in subsequent commands using `#N` syntax:

```
:browse assets/meshes/
  [1] pillar.glb    [2] wall_section.glb    [3] barrel.glb

:spawn mesh #2                # equivalent to: spawn mesh assets/meshes/wall_section.glb
:material set wall albedo #1  # equivalent to: material set wall albedo assets/meshes/pillar.glb
```

References are valid until the next `browse` command replaces them.

### 3.3 Command History

- Up/Down arrows browse history (per-session, saved to `~/.ferrum_editor_history`)
- Ctrl+R does reverse incremental search
- `!n` repeats command number n
- `!!` repeats last command

---

## 4. 3D Cursor

The 3D cursor is the spatial anchor for editor operations. "Spawn at cursor"
means "spawn at the 3D cursor position."

### 4.1 Cursor Appearance

In the 3D viewport (client window):
- Wireframe crosshair (three perpendicular lines through the point)
- Colored per axis: X=red, Y=green, Z=blue
- Small sphere at center (yellow when snapped, white when free)
- Grid plane highlight under cursor (subtle tint on the grid cell)

### 4.2 Keyboard Movement

All cursor movement keys work in **Normal mode**:

| Key | Action |
|-----|--------|
| `h` / `←` | Move cursor -X (one grid unit) |
| `l` / `→` | Move cursor +X |
| `k` / `↑` | Move cursor -Z (forward, away from camera default) |
| `j` / `↓` | Move cursor +Z |
| `u` / PgUp | Move cursor +Y (up) |
| `d` / PgDn | Move cursor -Y (down) |
| `H` (shift) | Move cursor -X by 10 grid units |
| `L` (shift) | Move cursor +X by 10 |
| `K` (shift) | Move cursor -Z by 10 |
| `J` (shift) | Move cursor +Z by 10 |
| `U` (shift) | Move cursor +Y by 10 |
| `D` (shift) | Move cursor -Y by 10 |
| `[` | Halve grid size (0.5, 0.25, 0.125, ...) |
| `]` | Double grid size (1, 2, 4, 8, ...) |
| `.` | Move cursor to origin (0, 0, 0) |
| `'` | Move cursor to selected entity |
| `"` | Move selected entity to cursor |

Numeric prefix works like Vim: `5l` moves cursor +5X grid units.

### 4.3 Mouse Movement

- **Left click** on viewport: raycast to geometry, place cursor at hit point
  (snapped to grid if snap enabled)
- **Middle drag**: orbit camera around cursor
- **Scroll**: zoom toward/away from cursor
- **Right click**: open context menu in TUI (see §4.5)

### 4.4 Cursor Commands

```
:cursor 10 0 5          # set cursor to absolute position
:cursor +0 +1 +0        # relative move (up 1 unit)
:grid 0.5               # set grid size to 0.5 units
:snap on|off|toggle      # grid snapping
:camera front            # align camera to -Z looking at cursor
:camera right            # align camera to +X looking at cursor
:camera top              # align camera to -Y looking at cursor
:camera ortho            # toggle orthographic projection
:camera pos 10 5 -10     # set camera position explicitly
```

Camera commands are sent to the client via the client state socket (the
server does not manage camera state — it is purely a view concern).

### 4.5 Context Menu

Right-clicking in the viewport sends a `context_menu` event to the
controller via the client state socket, along with the clicked position
and entity (if any). The controller displays a modal overlay in the TUI:

```
┌─ Context ──────────┐
│ [s] Spawn here     │
│ [p] Properties     │
│ [d] Delete         │
│ [g] Grab           │
│ [m] Assign material│
│ [Esc] Cancel       │
└────────────────────┘
```

The context menu is a modal mode: only the listed keys are active.
Pressing any listed key executes the command and returns to Normal mode.
Escape dismisses the menu without action.

---

## 5. Entity Operations

### 5.1 Normal Mode Shortcuts

| Key | Command | Description |
|-----|---------|-------------|
| `a` | spawn | Spawn menu (sub-keys: `b`=box, `s`=sphere, `p`=prefab, `l`=light) |
| `x` | delete | Delete selected entities |
| `g` | grab | Enter grab mode (move selection with cursor keys, Enter to confirm, Escape to cancel) |
| `r` | rotate | Enter rotate mode (axis sub-key: `x`/`y`/`z`, then type degrees) |
| `s` | scale | Enter scale mode (type factor, Enter to confirm) |
| `c` | clone | Duplicate selection at cursor |
| `p` | properties | Show properties of selected entity in log area |
| `i` | inspect | Show detailed component dump |
| `z` | undo | Undo last operation |
| `Z` | redo | Redo |
| `Space` | select | Toggle select entity under cursor |
| `A` | select all | Select all entities |
| `Ctrl+A` | deselect all | Clear selection |
| `/` | search | Search entities by name/component (incremental) |

### 5.2 Spawn Workflow

1. Press `a` (spawn menu appears in status bar: `[b]ox [s]phere [p]refab [l]ight [m]esh`)
2. Press sub-key (e.g., `b` for box)
3. Type dimensions or accept defaults: `2 2 2` Enter
4. Entity spawns at cursor position
5. Entity is automatically selected

Or via command: `:spawn box 2 2 2`

### 5.3 Grab (Move) Workflow

1. Select entity (Space on it, or `:select entity_042`)
2. Press `g` (grab mode)
3. Use cursor keys to move — entity follows cursor in real-time
4. Press Enter to confirm, Escape to cancel (revert to original position)
5. Axis constraint: press `x`/`y`/`z` after `g` to lock to axis

**Latency note:** the client does not wait for the server during grab mode.
It provisionally repositions the entity locally for zero-latency visual
feedback. Only the final confirmed position is sent to the server as a
`move` command (see design §4.5). If the server rejects the move (e.g.,
out of bounds), the entity snaps back to its last authoritative position.

### 5.4 Multi-Select Operations

All operations work on the selection set. When multiple entities are selected:
- **Move**: all entities move by the same delta
- **Rotate**: all entities rotate around the cursor (group center)
- **Scale**: all entities scale relative to cursor
- **Delete**: all selected entities removed
- **Clone**: all duplicated, offset by one grid unit

---

## 6. Asset Browser

### 6.1 Tab-Completion Browser

The primary asset browsing mechanism is tab-completion in the command-line:

```
:spawn mesh assets/meshes/<TAB>
  pillar.glb      wall_section.glb     barrel.glb
  crate_small.glb crate_large.glb      torch.glb
```

### 6.2 Browse Command

For more structured browsing:

```
:browse                          # list project root
:browse assets/meshes/           # list meshes
:browse assets/textures/ --sort size  # sort by size
:browse --filter *.glb           # filter by extension
```

Output appears in the log area. Each entry is numbered for quick reference:

```
[1] pillar.glb         (245 KB, mesh, 3 materials)
[2] wall_section.glb   (128 KB, mesh, 1 material)
[3] barrel.glb         (64 KB, mesh, 2 materials)

:spawn mesh #2          # spawn wall_section.glb
```

### 6.3 Asset Preview

When browsing assets, the client shows a preview:
- **Mesh**: rendered floating near cursor with slow rotation
- **Texture**: displayed on a quad near cursor
- **Material**: applied to a sphere near cursor
- **Prefab**: full prefab rendered at cursor position (ghosted / semi-transparent)

Preview is triggered by highlighting an asset in tab-completion or browse results.

---

## 7. Texture Synthesis Workflow

### 7.1 Interactive Synthesis

```
:texsynth new stone_wall 512 512
> Created texture workspace 'stone_wall' (512×512)

:texsynth layer base voronoi cells=32 jitter=0.8
> Added layer 'base' (voronoi)

:texsynth layer cracks fractal octaves=6 lacunarity=2.1
> Added layer 'cracks' (fractal)

:texsynth blend multiply base cracks 0.7
> Blended base × cracks (factor 0.7)

:texsynth colorize base 0.6,0.55,0.45 0.4,0.35,0.3
> Colorized 'base' with gradient

:texsynth preview
> Preview updated in viewport (near cursor)

:texsynth bake stone_wall assets/meshes/wall.glb --uv 0 --res 1024
> Baked 'stone_wall' to UV map → assets/textures/stone_wall_albedo.png
> Baked normal map → assets/textures/stone_wall_normal.png
> Baked roughness → assets/textures/stone_wall_roughness.png
```

### 7.2 Script-Based Synthesis

More commonly, textures are defined in scripts:

```
:run scripts/textures/stone_wall.lua
> Synthesized stone_wall (512×512, 3 maps) in 0.34s
```

The script produces textures directly and registers them as assets.

### 7.3 Live Preview

During synthesis (interactive or scripted), intermediate results are:
- Displayed in the viewport as a quad near the cursor
- Updated in real-time as parameters change
- Optionally split-view (albedo / normal / roughness side by side)

---

## 8. Scripting Workflow

### 8.1 Running Scripts

```
:run scripts/gen_forest.lua              # run script file
:run scripts/gen_forest.lua seed=42      # with arguments
:eval print("hello from lua")            # evaluate inline expression
:repl                                    # enter interactive REPL mode
```

### 8.2 REPL Mode

In REPL mode, the command-line becomes a Lua prompt. The server detects
incomplete input (e.g., an unclosed `function` block) using `luaL_loadstring()`
and returns `"status": "incomplete"`. The controller then shows a `...>`
continuation prompt.

```
lua> for i = 1, 10 do
...>   spawn_box(cursor() + vec3(i * 2, 0, 0), vec3(1, 1, 1))
...> end
> Spawned 10 boxes

lua> local t = texsynth.new(256, 256)
lua> t:layer("base", "perlin", {scale=4})
lua> t:bake("test_tex", "assets/meshes/floor.glb")

lua> exit()    -- or Ctrl+D to leave REPL
```

Multi-line accumulation: the controller accumulates lines locally while
in continuation mode. When the server returns `"status": "ok"` or
`"status": "error"`, the accumulated input is cleared and the prompt
returns to `lua>`.

**Error display:** syntax errors appear in red in the log area, with the
offending line number highlighted. Runtime errors show a traceback.

### 8.3 Script API

Scripts see a global API:

```lua
-- Entity operations
spawn_box(pos, size)                → entity_id
spawn_sphere(pos, radius)           → entity_id
spawn_prefab(pos, path)             → entity_id
spawn_mesh(pos, mesh_path)          → entity_id
destroy(entity_id)
move(entity_id, delta)
set_pos(entity_id, pos)
set_rot(entity_id, quat)
select(entity_id)
deselect_all()

-- Cursor
cursor()                            → vec3
set_cursor(pos)
grid_size()                         → number

-- Query
find_all()                          → {entity_id...}
find_where(component, field, op, value) → {entity_id...}
get_component(entity_id, name)      → table

-- Math
vec3(x, y, z)                       → vec3
quat(x, y, z, w)                    → quat
quat_from_euler(rx, ry, rz)         → quat
noise.perlin(x, y, params)          → number
noise.simplex(x, y, params)         → number

-- Texture
texsynth.new(w, h)                  → texture_workspace
ws:layer(name, type, params)
ws:blend(op, a, b, factor)
ws:colorize(layer, lo_color, hi_color)
ws:normal_from_height(layer, strength)
ws:bake(name, mesh_path, opts)
ws:save(path)

-- Utility
print(...)                          -- output to log
sleep(seconds)                      -- yield (coroutine)
undo()
redo()
```

---

## 9. Keybinding System

### 9.1 Binding Syntax

```
:bind <key-sequence> <command>
:unbind <key-sequence>
:bindings                           # list all bindings
:bindings save editor.conf          # save to file
:bindings load editor.conf          # load from file
```

Key sequence syntax:
- Single key: `x`, `a`, `space`, `enter`, `tab`, `escape`
- Modifier: `ctrl+x`, `alt+x`, `shift+x`
- Sequence: `g x` (press g, then x — like Vim leader keys)
- Function keys: `f1` through `f12`

### 9.2 Default Bindings

```ini
# Movement
bind h         cursor_move -1 0 0
bind l         cursor_move +1 0 0
bind k         cursor_move 0 0 -1
bind j         cursor_move 0 0 +1
bind u         cursor_move 0 +1 0
bind d         cursor_move 0 -1 0

# Entity ops
bind a         spawn_menu
bind x         delete_selected
bind g         grab_mode
bind r         rotate_mode
bind s         scale_mode
bind c         clone_selected
bind z         undo
bind shift+z   redo
bind space     toggle_select
bind shift+a   select_all
bind ctrl+a    deselect_all

# Grid
bind [         grid_smaller
bind ]         grid_larger
bind .         cursor_origin
bind '         cursor_to_selection
bind "         selection_to_cursor

# Modes
bind :         command_mode
bind escape    normal_mode
bind /         search_mode

# Camera
bind 1         camera_front
bind 3         camera_right
bind 7         camera_top
bind 5         camera_ortho_toggle
bind 0         camera_reset
```

---

## 10. MCP Server Interface

### 10.1 Overview

The MCP server allows AI agents to drive the editor using the same command
vocabulary as a human user. It exposes:

- **Tools** — commands that mutate world state (spawn, delete, move, etc.)
- **Resources** — queries for world state (entity list, properties, cursor, etc.)

### 10.2 Example Tool Definitions

```json
{
  "name": "spawn_entity",
  "description": "Spawn an entity at the given position",
  "parameters": {
    "type": {"type": "string", "enum": ["box", "sphere", "mesh", "prefab", "light"]},
    "position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
    "size": {"type": "array", "items": {"type": "number"}},
    "asset_path": {"type": "string"}
  }
}
```

### 10.3 Example Resources

```json
{
  "name": "world_state",
  "description": "Current world entities and their properties",
  "uri": "editor://world/entities"
}

{
  "name": "cursor_state",
  "description": "Current 3D cursor position and grid settings",
  "uri": "editor://cursor"
}

{
  "name": "asset_catalog",
  "description": "List of available assets",
  "uri": "editor://assets/{path}"
}
```

### 10.4 AI Workflow Example

An AI agent connected via MCP might:

1. Query `editor://world/entities` to understand current level
2. Query `editor://assets/prefabs/` to see available prefabs
3. Call `spawn_entity` multiple times to place objects
4. Call `run_script` to execute a generation script
5. Query the result and iterate

This enables fully autonomous level generation with AI oversight.

---

## 11. Workflow Examples

### 11.1 Build a Room

```
:grid 1                           # 1m grid
:spawn box 10 3 0.2               # back wall
:cursor +5 0 +5                   # move to front-right
:spawn box 0.2 3 10               # right wall
:cursor -10 0 0                   # move to front-left
:spawn box 0.2 3 10               # left wall
:cursor +5 0 -5                   # move to front center
:spawn box 10 3 0.2               # front wall (with gap for door)
:cursor 0 0 0                     # back to origin
:spawn box 10 0.1 10              # floor
```

### 11.2 Procedural Forest

```
:run scripts/gen_forest.lua seed=42 density=0.3 area=100
> Spawned 847 trees in 1.2s
> Texture autumn_bark synthesized (512×512)
> Texture leaf_cluster synthesized (256×256)
```

### 11.3 Texture a Wall

```
:select wall_back
:texsynth new brick 1024 1024
:texsynth layer base voronoi cells=64 jitter=0.3
:texsynth layer mortar voronoi cells=64 jitter=0.3 mode=border width=0.05
:texsynth colorize base 0.7,0.3,0.2 0.5,0.2,0.15
:texsynth colorize mortar 0.8,0.8,0.75 0.7,0.7,0.65
:texsynth blend overlay base mortar 1.0
:texsynth bake brick assets/meshes/wall.glb --uv 0 --res 1024
:material set wall_back albedo assets/textures/brick_albedo.png
```
