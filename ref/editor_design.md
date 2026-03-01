# Level Editor Implementation Design

## 1. Module Layout

```
src/editor/
├── protocol/
│   ├── edit_io_thread.c       # Dedicated editor I/O thread (TCP accept/read/write)
│   ├── edit_io_thread.h       # (internal)
│   ├── edit_cmd_ring.c        # Lock-free SPSC command ring (I/O thread → tick loop)
│   ├── edit_cmd_ring.h
│   ├── edit_parse.c           # JSON command parser
│   ├── edit_parse.h
│   ├── edit_dispatch.c        # command → handler routing (runs on main tick thread)
│   └── edit_dispatch.h
├── commands/
│   ├── cmd_spawn.c            # spawn command family
│   ├── cmd_transform.c        # move, rotate, scale
│   ├── cmd_select.c           # select, deselect, query
│   ├── cmd_delete.c           # delete + clone
│   ├── cmd_clone.c            # clone (entity duplication)
│   ├── cmd_level.c            # save, load, new
│   ├── cmd_asset.c            # browse, import, complete
│   ├── cmd_cursor.c           # cursor position, grid (forwarded to client)
│   ├── cmd_camera.c           # camera mode/position (forwarded to client)
│   ├── cmd_material.c         # material assignment
│   ├── cmd_script.c           # run, eval, repl
│   ├── cmd_texture.c          # texsynth commands
│   ├── cmd_inspect.c          # properties, inspect
│   └── cmd_search.c           # entity search by name/component
├── assets/
│   ├── asset_registry.c       # catalog + lookup
│   ├── asset_registry.h
│   ├── asset_watch.c          # inotify filesystem watcher for hot-reload
│   ├── asset_watch.h
│   ├── asset_import.c         # external file import
│   ├── asset_download.c       # TCP asset transfer (server side, on editor I/O thread)
│   └── asset_download.h
├── undo/
│   ├── undo_stack.c           # command pattern undo/redo
│   └── undo_stack.h
├── script/
│   ├── script_runtime.c       # Script thread + Lua state (dedicated pthread)
│   ├── script_runtime.h
│   ├── script_env.c           # script_env_t setup, snapshot copy, update swap
│   ├── script_sandbox.c       # Lua sandbox (strip os/io/ffi/debug)
│   ├── script_native.c        # Native script function registry + dispatch
│   ├── script_rebase.c        # Rebase script entity updates onto tick state
│   ├── script_api_entity.c    # entity manipulation bindings (Lua ↔ env)
│   ├── script_api_math.c      # vec3, quat, noise bindings
│   ├── script_api_texture.c   # texsynth bindings
│   └── script_api_cursor.c    # cursor/grid bindings
├── texsynth/
│   ├── texsynth_workspace.c   # workspace lifecycle
│   ├── texsynth_workspace.h
│   ├── texsynth_noise.c       # perlin, simplex, voronoi, fractal
│   ├── texsynth_noise.h
│   ├── texsynth_blend.c       # blend modes
│   ├── texsynth_bake.c        # UV-space rasterization
│   └── texsynth_bake.h
├── cursor/
│   ├── editor_cursor.c        # 3D cursor state + grid logic (client side)
│   └── editor_cursor.h
├── level/
│   ├── level_serialize.c      # JSON save/load
│   └── level_serialize.h
├── json/
│   ├── json_parse.c           # Minimal JSON parser (no external dependency)
│   └── json_parse.h
├── controller/
│   ├── ctrl_main.c            # controller entry point
│   ├── ctrl_tui.c             # terminal UI (raw termios)
│   ├── ctrl_tui.h
│   ├── ctrl_input.c           # input parsing, keybindings, Vim-style state machine
│   ├── ctrl_input.h
│   ├── ctrl_complete.c        # tab-completion engine
│   ├── ctrl_complete.h
│   ├── ctrl_history.c         # command history (with file persistence)
│   ├── ctrl_history.h
│   ├── ctrl_connection.c      # TCP connection to server + client
│   ├── ctrl_connection.h
│   ├── ctrl_browse.c          # Browse result cache + #N references
│   └── ctrl_browse.h
├── client/
│   ├── client_state_socket.c  # TCP listener for controller connection (client side)
│   ├── client_state_socket.h
│   ├── client_selection.c     # Selection state + highlight rendering
│   ├── client_selection.h
│   ├── client_preview.c       # Asset preview rendering (mesh/texture/material)
│   ├── client_preview.h
│   ├── client_editor_input.c  # Mouse raycast, click-select, box-select
│   ├── client_editor_input.h
│   ├── client_editor_camera.c # Camera modes (front/right/top/ortho)
│   └── client_editor_camera.h
├── mcp/
│   ├── mcp_server.c           # MCP protocol handler
│   ├── mcp_server.h
│   ├── mcp_tools.c            # tool definitions + dispatch
│   └── mcp_resources.c        # resource queries
└── editor.h                   # top-level editor context + init/shutdown

include/ferrum/editor/
├── editor.h                   # public: editor_ctx_t, init, shutdown
├── editor_commands.h          # public: command registry + dispatch
├── editor_cursor.h            # public: cursor state query
├── asset_registry.h           # public: asset catalog query
├── texsynth.h                 # public: texture synthesis API
└── script.h                   # public: script runtime control
```

### Header Ownership (2-Type Rule Compliance)

| Header | Types |
|--------|-------|
| `editor.h` | `editor_ctx_t`, `editor_config_t` |
| `editor_commands.h` | `editor_cmd_t`, `editor_cmd_result_t` |
| `editor_cursor.h` | `editor_cursor_t`, `editor_grid_t` |
| `asset_registry.h` | `asset_entry_t`, `asset_query_t` |
| `texsynth.h` | `texsynth_workspace_t`, `texsynth_layer_t` |
| `script.h` | `script_runtime_t`, `script_env_t` |

---

## 2. Edit Protocol Details

### 2.1 Wire Format

TCP, newline-delimited UTF-8 JSON. Each message is a single line terminated
by `\n`. This is chosen over binary for:
- Easy debugging (netcat, telnet)
- Easy scripting (any language with TCP + JSON)
- Easy MCP bridging (MCP is already JSON-RPC)

Maximum message size: 1 MB (rejects larger). Large query responses (e.g.,
entity lists for levels with hundreds of entities) are paginated via
`offset`/`limit` parameters rather than sending everything in one message.

### 2.2 Message Types

**Request (controller → server):**
```json
{"id": 42, "cmd": "spawn", "args": {"type": "box", "size": [2,2,2], "pos": [10,0,5]}}
```

Fields:
- `id` (u32) — correlates response to request. Monotonically increasing.
- `cmd` (string) — command name from the command vocabulary.
- `args` (object) — command-specific arguments.

**Response (server → controller):**
```json
{"id": 42, "ok": true, "result": {"entity": "ent_042", "body_id": 42}}
```

```json
{"id": 43, "ok": false, "error": "Unknown asset: assets/meshes/missing.glb"}
```

**Event (server → controller, unsolicited):**
```json
{"event": "entity_spawned", "entity": "ent_042", "data": {"pos": [10,0,5], "type": "box"}}
```

```json
{"event": "asset_changed", "path": "assets/textures/stone_wall_albedo.png", "action": "created"}
```

Events have no `id` field. The controller must handle them asynchronously.

### 2.3 Server-Side Implementation

The edit socket runs on a **dedicated editor I/O thread** — not on fiber
workers (which must not touch sockets per the engine threading contract).
Commands are bridged to the main tick thread via a lock-free SPSC ring.

```c
/* Editor I/O thread — owns all TCP sockets */
static void *edit_io_thread_(void *user) {
    editor_ctx_t *ctx = user;
    int listen_fd = net_tcp_listen(ctx->edit_port);
    int asset_fd  = net_tcp_listen(ctx->asset_port);

    /* Non-blocking + epoll for multiplexing */
    int epfd = epoll_create1(0);
    epoll_add_(epfd, listen_fd, EPOLLIN);
    epoll_add_(epfd, asset_fd, EPOLLIN);

    struct epoll_event events[16];
    while (ctx->running) {
        int n = epoll_wait(epfd, events, 16, 100 /* ms */);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                /* Accept new controller connection */
                int client_fd = accept(listen_fd, NULL, NULL);
                net_tcp_set_nonblocking(client_fd);
                net_tcp_set_nodelay(client_fd);
                epoll_add_(epfd, client_fd, EPOLLIN);
            } else if (fd == asset_fd) {
                /* Accept new asset download connection */
                int dl_fd = accept(asset_fd, NULL, NULL);
                asset_download_handle_(ctx, dl_fd);  /* blocking OK, dedicated thread */
            } else if (events[i].events & EPOLLIN) {
                /* Read from controller connection */
                edit_io_read_line_(ctx, fd);  /* → enqueue to cmd_ring */
            }
        }

        /* Drain response ring → send TCP responses */
        edit_io_drain_responses_(ctx);
    }
    return NULL;
}

/* Command ring: I/O thread → main tick thread */
typedef struct edit_cmd_ring {
    _Atomic uint32_t head;   /* written by I/O thread */
    _Atomic uint32_t tail;   /* read by tick thread */
    edit_cmd_entry_t entries[EDIT_CMD_RING_SIZE];  /* power-of-2 */
} edit_cmd_ring_t;

/* Main tick loop drains the ring between physics ticks */
static void edit_drain_commands_(editor_ctx_t *ctx) {
    edit_cmd_entry_t entry;
    while (edit_cmd_ring_pop(&ctx->cmd_ring, &entry)) {
        editor_cmd_result_t result = edit_dispatch(ctx, &entry.cmd);
        edit_resp_ring_push(&ctx->resp_ring, entry.cmd.id, &result);
    }
}
```

The epoll-based I/O thread handles TCP accept, read, and write for both the
edit protocol and asset download connections. This keeps all socket I/O on one
thread, consistent with the engine's architecture.

### 2.4 Command Dispatch

Commands are registered in a static table:

```c
typedef struct editor_cmd_handler {
    const char *name;
    editor_cmd_result_t (*fn)(editor_ctx_t *ctx, const json_value_t *args);
    const char *help;
    const char *completion_hint;  /* for tab-completion */
} editor_cmd_handler_t;

static const editor_cmd_handler_t g_handlers[] = {
    {"spawn",      cmd_spawn,      "Spawn entity at position", "<type> [args...]"},
    {"delete",     cmd_delete,     "Delete selected entities", ""},
    {"clone",      cmd_clone,      "Duplicate selection",      "[offset]"},
    {"move",       cmd_move,       "Move selection by delta",  "<dx> <dy> <dz>"},
    {"rotate",     cmd_rotate,     "Rotate selection",         "<rx> <ry> <rz>"},
    {"scale",      cmd_scale,      "Scale selection",          "<sx> <sy> <sz>"},
    {"select",     cmd_select,     "Select entities",          "<id|all|none|where ...>"},
    {"cursor",     cmd_cursor,     "Set cursor position",      "<x> <y> <z>"},
    {"grid",       cmd_grid,       "Set grid size",            "<size>"},
    {"snap",       cmd_snap,       "Toggle grid snap",         "<on|off|toggle>"},
    {"camera",     cmd_camera,     "Set camera mode/position", "<front|right|top|ortho|pos ...>"},
    {"save",       cmd_save,       "Save level to file",       "<path>"},
    {"load",       cmd_load,       "Load level from file",     "<path>"},
    {"browse",     cmd_browse,     "Browse assets",            "[path] [--filter ...]"},
    {"complete",   cmd_complete,   "Tab-completion query",     "<prefix>"},
    {"material",   cmd_material,   "Assign material to entity","<set|get> <entity> <slot> <path>"},
    {"run",        cmd_run,        "Run script file",          "<path> [args...]"},
    {"eval",       cmd_eval,       "Evaluate script expression", "<expr>"},
    {"repl",       cmd_repl,       "Enter/exit Lua REPL mode", ""},
    {"texsynth",   cmd_texsynth,   "Texture synthesis",        "<sub-cmd> [args...]"},
    {"undo",       cmd_undo,       "Undo last operation",      ""},
    {"redo",       cmd_redo,       "Redo last undone operation",""},
    {"bind",       cmd_bind,       "Bind key to command",      "<key> <command>"},
    {"properties", cmd_properties, "Show entity properties",   "[entity_id]"},
    {"inspect",    cmd_inspect,    "Detailed component dump",  "[entity_id]"},
    {"search",     cmd_search,     "Search entities",          "<pattern>"},
    {NULL, NULL, NULL, NULL}
};
```

### 2.5 Completion Protocol

The `complete` command returns candidates for tab-completion:

**Request:**
```json
{"id": 99, "cmd": "complete", "args": {"context": "spawn prefab assets/prefabs/st"}}
```

**Response:**
```json
{"id": 99, "ok": true, "result": {
  "candidates": ["stone_pillar", "stone_wall", "stone_arch"],
  "prefix": "assets/prefabs/st",
  "type": "asset_path"
}}
```

---

## 3. Asset Download Protocol

### 3.1 Architecture

The asset downloader is a separate TCP listener on the server. Clients connect
when they need to fetch an asset referenced by a spawn or material assignment.

```
Server                                    Client
  │                                         │
  │ ◄── TCP connect to asset_port ──────── │
  │                                         │
  │ ◄── REQ: path_len(u16) + path(utf8) ── │
  │                                         │
  │ ── RESP: status(u8) + len(u32) ──────► │
  │ ── DATA: chunk(64KB) ... ────────────► │
  │ ── DATA: chunk(remaining) ───────────► │
  │                                         │
  │ ◄── REQ: next asset ... ────────────── │
```

### 3.2 Wire Format

```
Request:
  path_len  : u16 LE        (max 1024)
  path      : utf8 bytes

Response header:
  status    : u8             (0 = OK, 1 = not found, 2 = error)
  total_len : u32 LE         (file size in bytes, 0 if error)

Response data (only if status == 0):
  data      : raw bytes      (total_len bytes, streamed)
```

No chunking headers — the client knows `total_len` and reads until it has all
bytes. The TCP stream provides reliable ordered delivery.

### 3.3 Client-Side Cache

The client maintains a local asset cache directory:
```
~/.ferrum_cache/
├── assets/
│   ├── meshes/
│   │   └── pillar.glb
│   └── textures/
│       └── stone_wall_albedo.png
└── cache.json              # path → hash → local file mapping
```

Cache invalidation: server sends an `asset_changed` event (via game channel or
edit channel). Client re-downloads if the asset is in use.

### 3.4 Server-Side Implementation

Asset download connections are accepted by the editor I/O thread (see §2.3).
The actual file transfer is handled inline on the I/O thread since it is pure
sequential I/O (no computation, no fiber interaction needed):

```c
/* Called from editor I/O thread when a new asset download connection arrives */
static void asset_download_handle_(editor_ctx_t *ctx, int client_fd) {
    net_tcp_set_nonblocking(client_fd);  /* but we'll do blocking reads here */

    for (;;) {
        uint16_t path_len;
        if (tcp_read_exact_(client_fd, &path_len, 2) != 2) break;
        path_len = le16toh(path_len);
        if (path_len > 1024) break;

        char path[1025];
        if (tcp_read_exact_(client_fd, path, path_len) != path_len) break;
        path[path_len] = '\0';

        /* Resolve asset on disk */
        char full_path[PATH_MAX];
        if (!asset_registry_resolve(ctx->registry, path, full_path)) {
            uint8_t status = 1;  /* not found */
            tcp_write_all_(client_fd, &status, 1);
            continue;
        }

        /* Send file (status + length + data) */
        asset_send_file_(client_fd, full_path);
    }
    close(client_fd);
}
```

Note: for large asset transfers that might block the I/O thread too long,
a future optimization is to spawn a short-lived pthread per download
connection. For Phase 1 (single editor, small assets), inline I/O suffices.
```

---

## 4. 3D Cursor Implementation

### 4.1 Cursor State

The cursor lives on the **client**. Its state:

```c
typedef struct editor_cursor {
    vec3_t position;        /* world-space position */
    float grid_size;        /* current grid unit (meters) */
    bool snap_enabled;      /* snap to grid? */
    bool visible;           /* render cursor? */
} editor_cursor_t;
```

### 4.2 Cursor Synchronization

The controller needs to know the cursor position (for "spawn at cursor").
The client needs to receive cursor commands from the controller.
The client must push events to the controller (mouse clicks, box select, etc.).

**Option chosen: controller ↔ client direct TCP link (bidirectional).**

The client listens on a small TCP port (the "client state socket"). The
controller connects and can:
- Query cursor position, camera state, selection set
- Send cursor movement commands
- Send camera commands (front/right/top/ortho/position)
- Send selection commands (click-equivalent)

The client **pushes events** to the controller for viewport interactions:
- `cursor_moved` — after mouse-click raycast places cursor
- `entity_clicked` — mouse click on entity
- `context_menu` — right-click (controller shows context menu in TUI)
- `box_select` — drag-select completed, list of selected entities

This avoids routing cursor state through the server (which doesn't need it)
and eliminates polling latency for mouse-driven interactions.

```
Controller ──── TCP (client state, bidirectional) ────► Client
    │                                                      │
    │                                                      │  (renders cursor, handles mouse)
    │                                                      │  (pushes click/select events)
    └──── TCP (edit protocol) ────────────────────────► Server
                                                           │
                                                           │  (processes entity commands)
```

### 4.3 Grid Snapping

```c
static vec3_t snap_to_grid_(vec3_t pos, float grid_size) {
    return (vec3_t){
        roundf(pos.x / grid_size) * grid_size,
        roundf(pos.y / grid_size) * grid_size,
        roundf(pos.z / grid_size) * grid_size
    };
}
```

Grid sizes are powers of 2 (or powers of 10, configurable): 0.125, 0.25, 0.5,
1, 2, 4, 8 meters.

### 4.4 Cursor Rendering

The client renders the cursor as:
1. Three axis-colored lines (length = 2 × grid_size)
2. A small yellow sphere at the intersection
3. A subtle grid-cell highlight on the XZ plane

Uses the existing debug line rendering path (no new shaders needed).

### 4.5 Grab Mode (Client-Side Provisional Positioning)

When the user enters grab mode (`g` key), the selected entity must visually
track the cursor in real-time. Since the server is authoritative, a naive
roundtrip (send move → server processes → snapshot → client renders) would
add 50-250ms of latency — unacceptable for interactive placement.

**Solution: client-side provisional positioning.**

In grab mode, the client:
1. Stores the entity's server-authoritative position as `grab_origin`
2. Locally overrides the entity's rendered position to match cursor movement
3. Does NOT send continuous move commands to the server
4. On confirm (Enter/click), sends a single `move` command with the final delta
5. On cancel (Escape), snaps the entity back to `grab_origin`

```c
typedef struct editor_grab_state {
    bool active;                /* currently in grab mode? */
    uint32_t entity_id;         /* entity being grabbed */
    vec3_t grab_origin;         /* position when grab started */
    vec3_t grab_offset;         /* cursor_pos - grab_origin at grab start */
    uint8_t axis_constraint;    /* 0=free, 1=X, 2=Y, 4=Z (bitmask) */
} editor_grab_state_t;
```

The controller sends `grab_begin` to the client (via client state socket),
and the client enters grab mode locally. During grab, cursor movement is
purely local — no server traffic. Only the final `move` command goes through
the edit protocol to the server.

This gives zero-latency visual feedback during placement while maintaining
server authority for the final position.

---

## 5. Undo System

### 5.1 Command Pattern

Every mutating operation produces an `undo_entry_t`. Undo entries are recorded
at **drain time** (when the command actually executes on the main tick thread),
not at enqueue time. This ensures the undo stack reflects committed state.

```c
typedef struct undo_entry {
    editor_cmd_t forward;       /* the command that was executed */
    editor_cmd_t inverse;       /* the command that reverses it */
    uint32_t group_id;          /* for multi-command groups */
    void *snapshot_data;        /* for delete undo: entity snapshot (allocated from undo arena) */
    uint32_t snapshot_size;
} undo_entry_t;

typedef struct undo_stack {
    undo_entry_t *entries;      /* ring buffer */
    uint32_t capacity;          /* max entries (default 4096) */
    uint32_t top;               /* next write position */
    uint32_t undo_cursor;       /* current position for undo */
    arena_t snapshot_arena;     /* dedicated arena for entity snapshots (16 MB budget) */
} undo_stack_t;
```

**Memory strategy:**
- The undo stack uses a **dedicated arena** (`snapshot_arena`, 16 MB default)
  for entity snapshot data (needed for delete-undo). This arena is separate
  from both frame arenas (too short-lived) and level arenas (wrong lifetime).
- When the ring buffer wraps and overwrites an old entry, its snapshot data
  is freed from the arena. If the arena fills, the oldest entries are
  force-evicted until space is available.
- The entire undo stack (including arena) is freed on editor disconnect.
  Undo does not persist across sessions (spec §9).

### 5.2 Inverse Commands

| Forward | Inverse |
|---------|---------|
| `spawn(type, pos, ...)` → ent_042 | `delete(ent_042)` |
| `delete(ent_042)` | `spawn(snapshot of ent_042)` |
| `move(ent_042, delta)` | `move(ent_042, -delta)` |
| `rotate(ent_042, angles)` | `rotate(ent_042, -angles)` |
| `set_component(ent, comp, new)` | `set_component(ent, comp, old)` |

Delete captures a full snapshot of the entity (position, rotation, all
components, flags) so undo can reconstruct it exactly.

### 5.3 Group Undo

Script operations that spawn multiple entities are grouped:

```c
editor_undo_begin_group(ctx);
for (int i = 0; i < 100; i++) {
    editor_spawn_entity(ctx, &descs[i]);  /* each records an undo entry */
}
editor_undo_end_group(ctx);

/* Single undo reverses all 100 spawns */
editor_undo(ctx);
```

---

## 6. Script Runtime

### 6.1 Architecture Overview

The script runtime executes on a **dedicated script thread** (not the main
tick thread, not on fibers). It reads entity state from a read-only snapshot,
executes Lua or native C code through a unified interface, and produces an
array of **entity updates** that get rebased on top of physics and native
game logic updates during the main tick's commit phase.

This design achieves three goals:
1. **Decoupled execution**: scripts never block physics or networking
2. **Safe concurrency**: scripts read a frozen snapshot, not live state
3. **Native parity**: C code using the same `script_env_t` interface runs
   at full speed without LuaJIT overhead — useful for shipping game logic
   that was prototyped in Lua

### 6.2 Core Types

```c
/* A single entity delta produced by script execution */
typedef struct script_entity_update {
    uint32_t entity_id;
    uint32_t flags;             /* bitmask: SCRIPT_UPD_POS, _ROT, _SCALE, etc. */
    float pos[3];
    float rot[3];
    float scale[3];
    /* Additional fields as needed (materials, mesh ops, etc.) */
} script_entity_update_t;

/* Double-buffered update array: script writes to back, tick reads from front */
typedef struct script_update_buffer {
    script_entity_update_t *updates[2]; /* [0]=front (tick reads), [1]=back (script writes) */
    uint32_t count[2];
    uint32_t capacity;
    _Alignas(64) atomic_uint ready;     /* set by script thread after swap */
} script_update_buffer_t;

/* Read-only entity snapshot consumed by the script thread */
typedef struct script_entity_snapshot {
    uint32_t entity_id;
    uint8_t  active;
    uint8_t  type;              /* shape type enum */
    char     name[256];
    float    pos[3];
    float    rot[3];
    float    scale[3];
} script_entity_snapshot_t;

typedef struct script_entity_view {
    const script_entity_snapshot_t *entities;
    uint32_t count;
    uint32_t capacity;
} script_entity_view_t;

/* Unified environment exposed to both Lua and native scripts */
typedef struct script_env {
    /* Read-only entity state (snapshot from last tick) */
    script_entity_view_t entities;

    /* Write: entity updates produced this tick */
    script_entity_update_t *updates;
    uint32_t update_count;
    uint32_t update_capacity;

    /* Write: edit commands (spawn, delete, group, etc.) */
    edit_cmd_ring_t *cmd_ring;  /* SPSC ring → main tick thread */

    /* Context: cursor, selection, aliases (read-only snapshot) */
    float cursor_pos[3];
    float cursor_rot[3];
    uint32_t selection_ids[64];
    uint32_t selection_count;

    /* Back-pointer for internal use */
    struct script_runtime *runtime;
} script_env_t;

typedef struct script_runtime {
    /* Thread */
    pthread_t thread;
    atomic_bool running;
    atomic_bool request_stop;

    /* LuaJIT (NULL when running native-only) */
    lua_State *L;

    /* Double-buffered update exchange */
    script_update_buffer_t update_buf;

    /* Entity snapshot (written by tick thread, read by script thread) */
    script_entity_snapshot_t *snapshot;
    uint32_t snapshot_count;
    uint32_t snapshot_capacity;
    _Alignas(64) atomic_uint snapshot_seq; /* incremented by tick after write */
    uint32_t last_consumed_seq;            /* script thread's last-read seq */

    /* Edit commands from script → main tick */
    edit_cmd_ring_t cmd_ring;

    /* Unified environment (owned by script thread) */
    script_env_t env;

    /* Budget */
    uint32_t instruction_budget; /* default 100K per tick */
    bool continuation_pending;

    /* Memory */
    void *arena;                /* 8 MB arena for Lua allocator */
    size_t arena_size;
} script_runtime_t;
```

### 6.3 Execution Model: Threaded Double-Buffer

The script thread runs a loop synchronized to the server tick rate:

```
Main tick thread                    Script thread
────────────────                    ─────────────
Stage 1: command drain
  ├── drain I/O cmd ring
  ├── drain script cmd ring  ←───── script submits edit cmds
  ├── apply script entity    ←───── read front update buffer
  │   updates (rebase)
  ├── snapshot entities ─────────→ write entity snapshot
  │   (copy active entities        (atomic seq bump)
  │    to snapshot array)
  └── signal snapshot ready

Stage 2-N: physics, networking

                                    ┌── wait for new snapshot seq
                                    ├── copy snapshot → env.entities
                                    ├── execute script (Lua or native)
                                    │   ├── read env.entities (frozen)
                                    │   ├── write env.updates[]
                                    │   └── push edit cmds to ring
                                    ├── swap update back→front
                                    └── loop
```

**Rebasing**: when the main tick thread reads the script's entity updates,
it applies them on top of the current authoritative state. If physics moved
body #7 to (1,2,3) and the script says "set body #7 to (4,5,6)", the script
wins for the fields it updated. If the script only updated rotation, position
is left at the physics result. The `flags` bitmask in `script_entity_update_t`
controls which fields are applied.

**Ordering**: edit commands submitted via `cmd_ring` (spawn, delete, group,
etc.) are drained in Stage 1 alongside I/O commands. Entity updates from the
`update_buf` are applied after all commands are drained, so spawned entities
are visible in the next snapshot.

### 6.4 Native Code Path

The `script_env_t` interface is backend-agnostic. A native C function with
this signature can run instead of (or alongside) Lua:

```c
/* Native script function — same access pattern as Lua */
typedef void (*script_native_fn)(script_env_t *env, void *userdata);

/* Register a native tick function (runs every tick on script thread) */
void script_runtime_register_native(script_runtime_t *rt,
                                     script_native_fn fn,
                                     void *userdata);
```

Example native script:

```c
/* Native: rotate all selected entities 1° per tick */
static void rotate_selected(script_env_t *env, void *ud) {
    (void)ud;
    for (uint32_t i = 0; i < env->selection_count; i++) {
        uint32_t id = env->selection_ids[i];
        /* Find entity in snapshot */
        for (uint32_t j = 0; j < env->entities.count; j++) {
            if (env->entities.entities[j].entity_id == id) {
                script_entity_update_t *u = &env->updates[env->update_count++];
                u->entity_id = id;
                u->flags = SCRIPT_UPD_ROT;
                u->rot[0] = env->entities.entities[j].rot[0];
                u->rot[1] = env->entities.entities[j].rot[1] + 1.0f;
                u->rot[2] = env->entities.entities[j].rot[2];
                break;
            }
        }
    }
}
```

When shipping, Lua scripts can be "compiled" to native functions that use
the same `script_env_t` reads/writes. The runtime switches transparently.

### 6.5 LuaJIT Integration

LuaJIT 2.1 is embedded as a static library (built from `extern/luajit/`).
It provides Lua 5.1 semantics with a JIT compiler. The Lua state lives
entirely on the script thread — no cross-thread Lua access.

Lua scripts access entities through the `script_env_t` via registered C
functions:

```c
/* Lua: local entities = get_entities() → table of entity snapshots */
/* Lua: update_entity(id, {pos={x,y,z}}) → queue update */
/* Lua: submit_command("move", {entity_id=5, pos={1,2,3}}) → edit cmd */
```

**Instruction budget** is still enforced via `lua_sethook` for multi-frame
scripts. When budget is exhausted, the hook sets a flag and returns —
the script thread's loop picks up from the coroutine next tick.

**Multi-frame scripts** use Lua coroutines. The script thread's loop calls
`lua_resume()` each tick, passing the updated `script_env_t`. When the
coroutine yields (voluntarily or via budget), the loop swaps buffers and
waits for the next snapshot.

### 6.6 REPL Continuation Detection

When the controller sends `eval` or `repl` input, the server must detect
whether the Lua code is syntactically incomplete (e.g., `function foo()`
without `end`). The server uses `luaL_loadstring()` to attempt compilation:

```c
bool script_is_complete(script_runtime_t *rt, const char *input) {
    int status = luaL_loadstring(rt->L, input);
    if (status == LUA_ERRSYNTAX) {
        const char *msg = lua_tostring(rt->L, -1);
        /* Lua syntax errors for incomplete input end with "<eof>" */
        bool incomplete = (strstr(msg, "<eof>") != NULL);
        lua_pop(rt->L, 1);
        return !incomplete;
    }
    lua_pop(rt->L, 1);  /* pop compiled chunk */
    return true;  /* complete (valid or other error) */
}
```

Note: `script_is_complete` is called on the script thread (it touches `rt->L`).
The I/O thread enqueues the eval request; the script thread checks completeness
and either executes or returns `"status": "incomplete"`.

### 6.7 Safety

- Scripts run in a sandboxed Lua state (no `os.execute`, `io`, `loadlib`)
- FFI is DISABLED — scripts cannot access arbitrary memory
- Memory limit enforced via custom allocator (arena-backed, 8 MB default)
- Instruction count limit prevents runaway scripts (via `lua_sethook`)
- Entity reads are from a frozen snapshot (no race with physics)
- Entity writes go through the update buffer (rebased by tick thread)
- Edit commands go through SPSC ring (same mechanism as I/O commands)
- The script thread never touches live entity store directly

---

## 7. Texture Synthesis Engine

### 7.1 Workspace

A texture workspace is a collection of named layers at a fixed resolution:

```c
typedef struct texsynth_workspace {
    uint32_t width, height;
    uint32_t layer_count;
    texsynth_layer_t layers[TEXSYNTH_MAX_LAYERS];  /* max 32 */
    float *output_albedo;       /* RGBA float buffer */
    float *output_normal;       /* RGB float buffer */
    float *output_roughness;    /* R float buffer */
} texsynth_workspace_t;

typedef struct texsynth_layer {
    char name[64];
    float *data;                /* single-channel float [0,1] */
    uint32_t width, height;
} texsynth_layer_t;
```

### 7.2 Noise Generators

All generators write to a float buffer [0,1]:

```c
void texsynth_perlin(float *out, uint32_t w, uint32_t h,
                     const texsynth_noise_params_t *params);
void texsynth_simplex(float *out, uint32_t w, uint32_t h,
                      const texsynth_noise_params_t *params);
void texsynth_voronoi(float *out, uint32_t w, uint32_t h,
                      const texsynth_voronoi_params_t *params);
void texsynth_fractal(float *out, uint32_t w, uint32_t h,
                      const texsynth_fractal_params_t *params);
```

Parameters include scale, octaves, lacunarity, persistence, seed.

### 7.3 UV Baking

Bake rasterizes texture functions into UV space:

```c
typedef struct texsynth_bake_desc {
    const texsynth_workspace_t *workspace;
    const char *mesh_path;          /* glTF mesh to read UVs from */
    uint32_t uv_set;                /* UV set index (0 or 1) */
    uint32_t resolution;            /* output texture resolution */
    const char *output_prefix;      /* e.g., "assets/textures/stone_wall" */
} texsynth_bake_desc_t;

bool texsynth_bake(editor_ctx_t *ctx, const texsynth_bake_desc_t *desc);
```

Bake algorithm:
1. Load mesh, extract UV coordinates for the given UV set
2. For each triangle in UV space:
   a. Rasterize triangle to output texture pixels
   b. For each pixel, compute the 3D world-space position via barycentric interpolation
   c. Evaluate the texture function at that world-space position
   d. Write to output buffer
3. Dilate edges (extend colors past UV island borders to prevent seam artifacts)
4. Write to PNG/KTX2

This runs on the job system — triangle rasterization can be parallelized across
job workers.

---

## 8. MCP Server Implementation

### 8.1 Protocol

MCP uses **JSON-RPC 2.0** over a dedicated **TCP socket**. The controller
listens on a configurable MCP port (default 9300). AI agents connect over
TCP, enabling the agent to run on a different machine from the controller,
client, and server.

The controller process acts as the MCP server. It translates MCP tool calls
into edit protocol commands and MCP resource reads into state queries.

### 8.2 Architecture

```
AI Agent (Claude/etc)                          (can be on any machine)
    │
    │  JSON-RPC 2.0 over TCP (port 9300)
    │
    ▼
MCP Server (inside controller process)         (can be on any machine)
    │
    ├── Tool call → edit protocol command → Server  (can be on any machine)
    │                                         │
    │                                         ▼
    │                               (executes command)
    │                                         │
    │ ◄── response ──────────────────────────┘
    │
    ├── Resource read → client state query → Client  (can be on any machine)
    │                                         │
    │ ◄── cursor/camera/selection ───────────┘
    │
    └── Resource read → asset catalog query → Server
                                               │
        ◄── asset list ───────────────────────┘
```

All four processes (server, client, controller, AI agent) communicate over
TCP and can run on separate machines. The controller needs network addresses
for both the server and client, and the AI agent needs the controller's MCP
address. Typical distributed setup:

```
# On machine A (headless, beefy):
editor_server --port 9100

# On machine B (has GPU + display):
editor_client --server a.local:9100 --state-port 9200

# On machine C (terminal):
editor_ctrl --server a.local:9100 --client b.local:9200 --mcp-port 9300

# On machine D (AI workstation):
ai_agent --mcp c.local:9300
```

### 8.3 MCP TCP Listener

The MCP listener runs on the controller's poll loop (same as keyboard and
other socket I/O — the controller is single-threaded with `poll()`):

```c
typedef struct mcp_server {
    int listen_fd;              /* TCP listen socket for MCP */
    int client_fd;              /* connected AI agent (-1 if none) */
    char recv_buf[MCP_BUF_SIZE]; /* partial JSON-RPC message buffer */
    uint32_t recv_len;
    uint16_t port;              /* MCP listen port */
} mcp_server_t;
```

Messages are newline-delimited JSON-RPC 2.0. The controller reads lines
from the MCP socket, parses them, dispatches to tool/resource handlers,
and writes JSON-RPC responses back on the same socket.

### 8.4 Tool Mapping

Each editor command maps to an MCP tool:

```c
static const mcp_tool_def_t g_tools[] = {
    {
        .name = "spawn_entity",
        .description = "Spawn a new entity in the world",
        .handler = mcp_tool_spawn_,
        .schema = "{"
            "\"type\": {\"type\":\"string\",\"enum\":[\"box\",\"sphere\",\"mesh\",\"prefab\"]},"
            "\"position\": {\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
            "\"size\": {\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
            "\"asset\": {\"type\":\"string\"}"
        "}"
    },
    {
        .name = "delete_entities",
        .description = "Delete the currently selected entities",
        .handler = mcp_tool_delete_,
        .schema = "{}"
    },
    {
        .name = "run_script",
        .description = "Execute a Lua script file",
        .handler = mcp_tool_run_script_,
        .schema = "{\"path\":{\"type\":\"string\"},\"args\":{\"type\":\"object\"}}"
    },
    /* ... more tools ... */
    {NULL, NULL, NULL, NULL}
};
```

### 8.5 Resource Mapping

```c
static const mcp_resource_def_t g_resources[] = {
    {
        .uri_pattern = "editor://world/entities",
        .description = "List all entities with their properties",
        .handler = mcp_resource_entities_,
    },
    {
        .uri_pattern = "editor://world/entity/{id}",
        .description = "Get properties of a specific entity",
        .handler = mcp_resource_entity_,
    },
    {
        .uri_pattern = "editor://cursor",
        .description = "Current 3D cursor position and grid settings",
        .handler = mcp_resource_cursor_,
    },
    {
        .uri_pattern = "editor://assets/{path}",
        .description = "Browse asset catalog",
        .handler = mcp_resource_assets_,
    },
    {NULL, NULL, NULL}
};
```

---

## 9. Controller TUI Implementation

### 9.1 Terminal Setup

Raw termios mode (not ncurses — fewer dependencies, more control):

```c
static void tui_enter_raw_mode_(ctrl_tui_t *tui) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &tui->orig_termios);
    raw = tui->orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;  /* 100ms timeout for read */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
```

### 9.2 Event Loop

The controller runs a single-threaded event loop using `poll()`:

```c
static void ctrl_main_loop_(ctrl_ctx_t *ctx) {
    struct pollfd fds[3] = {
        {.fd = STDIN_FILENO,      .events = POLLIN},  /* keyboard */
        {.fd = ctx->server_fd,    .events = POLLIN},  /* server events */
        {.fd = ctx->client_fd,    .events = POLLIN},  /* client state */
    };

    while (ctx->running) {
        int n = poll(fds, 3, 100 /* ms */);

        if (fds[0].revents & POLLIN)
            ctrl_handle_keyboard_(ctx);

        if (fds[1].revents & POLLIN)
            ctrl_handle_server_event_(ctx);

        if (fds[2].revents & POLLIN)
            ctrl_handle_client_state_(ctx);

        ctrl_tui_redraw_(ctx);
    }
}
```

### 9.3 Input State Machine (Vim-Style Numeric Prefix)

The controller supports Vim-style numeric prefixes for repeat counts:

```c
typedef enum ctrl_input_mode {
    CTRL_MODE_NORMAL,       /* default: keybindings active */
    CTRL_MODE_COMMAND,      /* typing in command line (:) */
    CTRL_MODE_REPL,         /* Lua REPL mode */
    CTRL_MODE_GRAB,         /* entity grab mode (mouse/keys move entity) */
    CTRL_MODE_CONTEXT,      /* context menu overlay */
} ctrl_input_mode_t;

typedef struct ctrl_input_state {
    ctrl_input_mode_t mode;
    uint32_t numeric_prefix;    /* accumulates digits: "5" then "k" = move 5 */
    bool has_prefix;            /* true if any digits entered */
    char pending_key;           /* for two-key combos: g then g */
} ctrl_input_state_t;
```

**Normal mode dispatch:** when a digit is pressed, accumulate into
`numeric_prefix`. When a non-digit key arrives, dispatch the bound
command with the repeat count. Example: `5k` moves cursor up 5 grid units.

If no prefix is given, the repeat count defaults to 1.

### 9.4 Rendering

The TUI renders using ANSI escape sequences:
- `\033[H` — cursor home
- `\033[2J` — clear screen
- `\033[y;xH` — move cursor
- `\033[7m` — inverse video (status bar)
- `\033[1m` — bold
- `\033[31m` — red (errors)
- `\033[33m` — yellow (warnings)
- `\033[36m` — cyan (entity refs)

Double-buffered: build the full screen in a buffer, then write in one
`write()` call to avoid flicker.

### 9.5 Tab Completion Engine

```c
typedef struct ctrl_complete {
    char candidates[MAX_CANDIDATES][MAX_CANDIDATE_LEN];
    uint32_t count;
    uint32_t selected;          /* currently highlighted candidate */
    char prefix[256];           /* the prefix being completed */
    bool active;                /* completion popup visible? */
    bool loading;               /* waiting for server response? */
    uint32_t request_id;        /* correlate response to request */
} ctrl_complete_t;

/* On Tab press: */
static void ctrl_trigger_complete_(ctrl_ctx_t *ctx) {
    /* Extract the current word being typed */
    char prefix[256];
    ctrl_extract_word_at_cursor_(ctx, prefix, sizeof(prefix));

    /* Determine completion context (command name? asset path? entity?) */
    complete_context_t cctx = ctrl_classify_context_(ctx);

    if (cctx == COMPLETE_COMMAND) {
        /* Complete from built-in command list (local, instant) */
        ctrl_complete_commands_(ctx, prefix);
    } else {
        /* Ask server for completions (async) */
        ctx->complete.loading = true;
        ctx->complete.request_id = ctx->next_request_id++;
        ctrl_send_complete_request_(ctx, prefix, ctx->complete.request_id);
        /* TUI shows "..." loading indicator until response arrives */
        /* Response arrives via server_fd; handler checks request_id
         * to discard stale responses (user typed more since request) */
    }
}
```

**Stale response handling:** if the user types more characters after Tab,
a new completion request is issued. When the old response arrives, its
`request_id` won't match the latest request and is silently discarded.

### 9.6 Browse Results Caching

The `browse` command returns numbered results (e.g., `[1] stone_pillar [2]
stone_wall`). These are cached in `ctrl_browse_t` so the user can reference
them by number:

```c
typedef struct ctrl_browse {
    char results[MAX_BROWSE_RESULTS][MAX_ASSET_PATH];
    uint32_t count;
    bool valid;                 /* true if results are from a recent browse */
} ctrl_browse_t;
```

Usage: `spawn #2` expands to `spawn stone_wall` using the cached browse list.

---

## 10. Build Integration

### 10.1 Makefile Targets

```makefile
# Editor server (server + editor extensions + LuaJIT)
build/editor_server: $(SERVER_OBJS) $(EDITOR_SERVER_OBJS) $(LUAJIT_LIB)
	$(CC) $(CFLAGS) -DEDITOR_ENABLE -DLUAJIT_ENABLE -o $@ $^ $(LUAJIT_LDFLAGS) $(LDFLAGS)

# Editor client (client + editor mode)
build/editor_client: $(CLIENT_OBJS) $(EDITOR_CLIENT_OBJS)
	$(CC) $(CFLAGS) -DEDITOR_ENABLE -o $@ $^ $(LDFLAGS)

# Controller (standalone TUI process)
build/editor_ctrl: $(CTRL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

### 10.2 LuaJIT Integration

LuaJIT 2.1 is built from source as a git submodule (in `extern/luajit/`).
This avoids system dependency issues and keeps the build self-contained.

```makefile
LUAJIT_DIR = extern/luajit/src
LUAJIT_LIB = $(LUAJIT_DIR)/libluajit.a

$(LUAJIT_LIB):
	$(MAKE) -C $(LUAJIT_DIR) BUILDMODE=static CC=$(CC)

# Link against libluajit.a + libm + libdl (order matters: -lm after -lluajit)
LUAJIT_LDFLAGS = -L$(LUAJIT_DIR) -lluajit -ldl -lm
```

---

## 11. Threading and Synchronization

### 11.1 Server-Side Threading

```
Main tick thread
  │
  ├── Stage 1 (command drain):
  │     ├── Drain SPSC command ring (edit commands from I/O thread)
  │     ├── Drain SPSC command ring (edit commands from script thread)
  │     ├── Execute entity mutations (spawn, delete, move, etc.)
  │     ├── Apply script entity updates (rebase on top of physics)
  │     ├── Record undo entries
  │     └── Snapshot entities → script thread's snapshot buffer
  │
  ├── Stage 2-N: Physics, networking, etc. (existing pipeline)
  │
  ├── Physics job system (N workers, existing)
  │     └── phys_world_tick_parallel()
  │
  └── Net pump thread (existing)
        └── UDP recv loop

Editor I/O thread (dedicated pthread)
  │
  ├── epoll-based event loop
  ├── Accepts edit protocol connections (TCP)
  ├── Accepts asset download connections (TCP)
  ├── Reads JSON commands from controllers → enqueue into SPSC command ring
  ├── Reads response ring → sends JSON responses to controllers
  └── Handles asset file transfers inline

Script thread (dedicated pthread)
  │
  ├── Waits for new entity snapshot (atomic seq check)
  ├── Copies snapshot into script_env_t (read-only view)
  ├── Executes Lua scripts and/or native tick functions
  │     ├── Reads entity state from frozen snapshot
  │     ├── Writes entity updates to back buffer
  │     └── Pushes edit commands to SPSC ring → main tick
  ├── Swaps update buffer (back→front, atomic ready flag)
  └── Loops
```

**Key invariants:**
- Only the I/O thread touches TCP sockets (matches engine rule: "only I/O
  thread touches sockets")
- Only the main tick thread mutates world state (commands drained in Stage 1)
- Script thread never touches live entity store — reads snapshot, writes updates
- Lua state is only accessed from the script thread (never main tick, never I/O)
- Entity updates from scripts are rebased on top of physics results
- SPSC rings are the sole synchronization mechanism (lock-free)

### 11.2 Synchronization

No locks in the hot path. Cross-thread communication:

| Producer | Consumer | Mechanism |
|----------|----------|-----------|
| I/O thread | Main tick thread | SPSC command ring (lock-free) |
| Main tick thread | I/O thread | SPSC response ring (lock-free) |
| Script thread | Main tick thread | SPSC command ring (lock-free) |
| Script thread | Main tick thread | Double-buffered entity update array (atomic swap) |
| Main tick thread | Script thread | Entity snapshot array (atomic sequence number) |

All rings are bounded (capacity 1024). The double-buffered update array and
snapshot array use atomic sequence numbers for synchronization — no locks,
no condition variables in the hot path.

### 11.3 Client-Side Threading

```
Main thread (SDL + GL)
  │
  ├── Renders cursor, gizmos, grid, selection highlights (NEW)
  ├── Processes client state socket I/O (NEW)
  │     ├── Receives cursor/camera/selection commands from controller
  │     ├── Pushes events to controller (click, box_select, context_menu)
  │     └── Non-blocking TCP reads in SDL event loop
  │
  └── Net IO thread (existing)
        └── UDP recv + RUDP reassembly
```

The client state socket (TCP) is polled in the main loop alongside SDL
events using non-blocking reads. No additional thread needed — edit commands
arrive at <100/sec and never block rendering.

---

## 12. Phased Implementation Plan

### Phase 1: Foundation (Core Loop)
- [ ] Editor I/O thread (epoll, TCP listener, SPSC command ring)
- [ ] JSON parser (json_parse.c, minimal internal implementation)
- [ ] Command dispatch framework
- [ ] Basic commands: spawn box/sphere, delete, move, cursor set
- [ ] Selection system (multi-select, query select, click-to-select)
- [ ] Undo/redo stack (with dedicated snapshot arena)
- [ ] Controller TUI (status bar + log + command-line)
- [ ] Controller ↔ server TCP connection
- [ ] 3D cursor rendering on client
- [ ] Client state socket (bidirectional: cursor query + push events)
- [ ] Level save/load (JSON format)

### Phase 2: Asset System
- [ ] Asset registry (catalog + listing + search)
- [ ] Asset downloader (TCP transfer via I/O thread)
- [ ] Client asset cache
- [ ] Tab-completion for asset paths (async, with stale handling)
- [ ] Browse command (with #N reference caching)
- [ ] Material assignment commands
- [ ] Clone command

### Phase 3: Scripting
- [x] LuaJIT 2.1 integration (extern/luajit/ submodule, LUAJIT=1 build flag)
- [ ] Script runtime core (dedicated thread, double-buffered entity updates)
- [ ] Script environment (script_env_t, entity snapshot, update buffer, cmd ring)
- [ ] Script sandbox (strip os/io/ffi/debug from Lua state)
- [ ] Script rebase (apply entity updates on top of physics/game state)
- [ ] Native script function registry (C functions with same script_env_t interface)
- [ ] Entity manipulation API bindings (Lua ↔ script_env_t)
- [ ] Math/vec3/quat bindings
- [ ] run/eval commands
- [ ] REPL mode (with server-side continuation detection)
- [ ] Undo grouping for scripts (begin_group/end_group)

### Phase 4: Texture Synthesis
- [ ] Noise generators (perlin, simplex, voronoi, fractal)
- [ ] Blend modes
- [ ] UV bake engine
- [ ] texsynth commands
- [ ] Lua texture API

### Phase 5: Polish & MCP
- [ ] MCP server in controller (JSON-RPC 2.0 over TCP, port 9300)
- [ ] Full keybinding system (bind/unbind, save/load keymaps)
- [ ] Grab mode (client-side provisional positioning for real-time feel)
- [ ] Grid/snap refinement
- [ ] Prefab system
- [ ] Camera commands (front/right/top/ortho/position)

### Phase 6: Advanced
- [ ] Gizmo rendering (translate/rotate/scale handles)
- [ ] Entity property editor (in TUI)
- [ ] Hot-reload for scripts and assets (asset_watch.c, inotify)
- [ ] Context menu mode in TUI
- [ ] Inspect command (detailed component dump)
