# Level Editor Implementation Design

## 1. Module Layout

```
src/editor/
├── protocol/
│   ├── edit_socket.c          # TCP listener + per-connection fiber
│   ├── edit_socket.h          # (internal)
│   ├── edit_parse.c           # JSON command parser
│   ├── edit_parse.h
│   ├── edit_dispatch.c        # command → handler routing
│   └── edit_dispatch.h
├── commands/
│   ├── cmd_spawn.c            # spawn command family
│   ├── cmd_transform.c        # move, rotate, scale
│   ├── cmd_select.c           # select, deselect, query
│   ├── cmd_delete.c           # delete, clone
│   ├── cmd_level.c            # save, load, new
│   ├── cmd_asset.c            # browse, import, complete
│   ├── cmd_cursor.c           # cursor position, grid
│   ├── cmd_script.c           # run, eval, repl
│   └── cmd_texture.c          # texsynth commands
├── assets/
│   ├── asset_registry.c       # catalog + lookup + hot-reload
│   ├── asset_registry.h
│   ├── asset_import.c         # external file import
│   ├── asset_download.c       # TCP asset transfer (server side)
│   └── asset_download.h
├── undo/
│   ├── undo_stack.c           # command pattern undo/redo
│   └── undo_stack.h
├── script/
│   ├── script_runtime.c       # Lua state management + fiber
│   ├── script_runtime.h
│   ├── script_api_entity.c    # entity manipulation bindings
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
│   ├── editor_cursor.c        # 3D cursor state + grid logic
│   └── editor_cursor.h
├── level/
│   ├── level_serialize.c      # JSON save/load
│   └── level_serialize.h
├── controller/
│   ├── ctrl_main.c            # controller entry point
│   ├── ctrl_tui.c             # terminal UI (raw termios)
│   ├── ctrl_tui.h
│   ├── ctrl_input.c           # input parsing + keybindings
│   ├── ctrl_input.h
│   ├── ctrl_complete.c        # tab-completion engine
│   ├── ctrl_complete.h
│   ├── ctrl_history.c         # command history
│   ├── ctrl_history.h
│   ├── ctrl_connection.c      # TCP connection to server + client
│   └── ctrl_connection.h
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
| `script.h` | `script_runtime_t`, `script_result_t` |

---

## 2. Edit Protocol Details

### 2.1 Wire Format

TCP, newline-delimited UTF-8 JSON. Each message is a single line terminated
by `\n`. This is chosen over binary for:
- Easy debugging (netcat, telnet)
- Easy scripting (any language with TCP + JSON)
- Easy MCP bridging (MCP is already JSON-RPC)

Maximum message size: 64 KB (rejects larger).

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

The edit socket runs as a fiber on the networking job system:

```c
/* Lifecycle */
static void edit_socket_fiber_(void *user) {
    editor_ctx_t *ctx = user;
    int listen_fd = net_tcp_listen(ctx->edit_port);

    for (;;) {
        /* Accept blocks the fiber, not the OS thread.
         * Use a pollable fd + fiber yield. */
        int client_fd = edit_socket_accept_yield_(ctx, listen_fd);
        if (client_fd < 0) continue;

        /* Spawn a child fiber per controller connection */
        job_dispatch(ctx->net_sys, edit_client_fiber_,
                     &(edit_client_args_t){ctx, client_fd}, 0, NULL);
    }
}

static void edit_client_fiber_(void *user) {
    edit_client_args_t *args = user;
    char line_buf[65536];

    for (;;) {
        /* Read one line (fiber yields on EAGAIN) */
        int len = edit_read_line_yield_(args->ctx, args->fd, line_buf, sizeof(line_buf));
        if (len <= 0) break;  /* disconnect */

        /* Parse + dispatch */
        editor_cmd_t cmd;
        if (edit_parse_command(line_buf, len, &cmd)) {
            editor_cmd_result_t result = edit_dispatch(args->ctx, &cmd);
            edit_send_response_(args->fd, cmd.id, &result);
        } else {
            edit_send_error_(args->fd, 0, "Parse error");
        }
    }
    close(args->fd);
}
```

Key design choice: the edit socket fiber uses the **same** `job_system_t` as
the network runtime. This means edit commands can safely access the same data
structures (with appropriate locking) without cross-thread synchronization
issues.

### 2.4 Command Dispatch

Commands are registered in a static table:

```c
typedef struct editor_cmd_handler {
    const char *name;
    editor_cmd_result_t (*fn)(editor_ctx_t *ctx, const cJSON *args);
    const char *help;
    const char *completion_hint;  /* for tab-completion */
} editor_cmd_handler_t;

static const editor_cmd_handler_t g_handlers[] = {
    {"spawn",      cmd_spawn,      "Spawn entity at position", "<type> [args...]"},
    {"delete",     cmd_delete,     "Delete selected entities", ""},
    {"move",       cmd_move,       "Move selection by delta",  "<dx> <dy> <dz>"},
    {"rotate",     cmd_rotate,     "Rotate selection",         "<rx> <ry> <rz>"},
    {"scale",      cmd_scale,      "Scale selection",          "<sx> <sy> <sz>"},
    {"select",     cmd_select,     "Select entities",          "<id|all|none|where ...>"},
    {"cursor",     cmd_cursor,     "Set cursor position",      "<x> <y> <z>"},
    {"grid",       cmd_grid,       "Set grid size",            "<size>"},
    {"snap",       cmd_snap,       "Toggle grid snap",         "<on|off|toggle>"},
    {"save",       cmd_save,       "Save level to file",       "<path>"},
    {"load",       cmd_load,       "Load level from file",     "<path>"},
    {"browse",     cmd_browse,     "Browse assets",            "[path] [--filter ...]"},
    {"complete",   cmd_complete,   "Tab-completion query",     "<prefix>"},
    {"run",        cmd_run,        "Run script file",          "<path> [args...]"},
    {"eval",       cmd_eval,       "Evaluate script expression", "<expr>"},
    {"texsynth",   cmd_texsynth,   "Texture synthesis",        "<sub-cmd> [args...]"},
    {"undo",       cmd_undo,       "Undo last operation",      ""},
    {"redo",       cmd_redo,       "Redo last undone operation",""},
    {"bind",       cmd_bind,       "Bind key to command",      "<key> <command>"},
    {"properties", cmd_properties, "Show entity properties",   "[entity_id]"},
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

The asset downloader fiber:

```c
static void asset_download_fiber_(void *user) {
    editor_ctx_t *ctx = user;
    int listen_fd = net_tcp_listen(ctx->asset_port);

    for (;;) {
        int client_fd = edit_socket_accept_yield_(ctx, listen_fd);
        if (client_fd < 0) continue;

        /* Handle requests sequentially per connection */
        job_dispatch(ctx->net_sys, asset_client_fiber_,
                     &(asset_client_args_t){ctx, client_fd}, 0, NULL);
    }
}

static void asset_client_fiber_(void *user) {
    asset_client_args_t *args = user;

    for (;;) {
        uint16_t path_len;
        if (tcp_read_exact_yield_(args->fd, &path_len, 2) != 2) break;
        path_len = le16toh(path_len);
        if (path_len > 1024) break;

        char path[1025];
        if (tcp_read_exact_yield_(args->fd, path, path_len) != path_len) break;
        path[path_len] = '\0';

        /* Resolve asset on disk */
        char full_path[PATH_MAX];
        if (!asset_registry_resolve(args->ctx->registry, path, full_path)) {
            uint8_t status = 1;  /* not found */
            tcp_write_all_(args->fd, &status, 1);
            continue;
        }

        /* Send file */
        asset_send_file_(args->fd, full_path);
    }
    close(args->fd);
}
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

**Option chosen: controller ↔ client direct TCP link.**

The client listens on a small TCP port (the "client state socket"). The
controller connects and can:
- Query cursor position, camera state, selection set
- Send cursor movement commands
- Send selection commands (click-equivalent)

This avoids routing cursor state through the server (which doesn't need it).

```
Controller ──── TCP (client state) ────► Client
    │                                       │
    │                                       │  (renders cursor)
    │                                       │
    └──── TCP (edit protocol) ────────► Server
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

---

## 5. Undo System

### 5.1 Command Pattern

Every mutating operation produces an `undo_entry_t`:

```c
typedef struct undo_entry {
    editor_cmd_t forward;       /* the command that was executed */
    editor_cmd_t inverse;       /* the command that reverses it */
    uint32_t group_id;          /* for multi-command groups */
} undo_entry_t;

typedef struct undo_stack {
    undo_entry_t *entries;      /* ring buffer */
    uint32_t capacity;
    uint32_t top;               /* next write position */
    uint32_t undo_cursor;       /* current position for undo */
} undo_stack_t;
```

### 5.2 Inverse Commands

| Forward | Inverse |
|---------|---------|
| `spawn(type, pos, ...)` → ent_042 | `delete(ent_042)` |
| `delete(ent_042)` | `spawn(snapshot of ent_042)` |
| `move(ent_042, delta)` | `move(ent_042, -delta)` |
| `rotate(ent_042, angles)` | `rotate(ent_042, -angles)` |
| `set_component(ent, comp, new)` | `set_component(ent, comp, old)` |

Delete captures a full snapshot of the entity so undo can reconstruct it.

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

### 6.1 Lua Integration

Lua 5.4 is embedded as a static library. The runtime:

```c
typedef struct script_runtime {
    lua_State *L;               /* Lua state */
    editor_ctx_t *editor;       /* back-pointer to editor context */
    bool running;               /* script currently executing? */
} script_runtime_t;
```

### 6.2 Fiber Execution

Long-running scripts execute on a fiber that yields cooperatively:

```c
static void script_fiber_(void *user) {
    script_runtime_t *rt = user;

    /* Resume the Lua coroutine */
    int status = lua_resume(rt->L, NULL, 0);

    if (status == LUA_YIELD) {
        /* Script called sleep() or a yielding operation.
         * Re-enqueue this fiber after the requested delay. */
        job_dispatch_delayed(rt->editor->net_sys, script_fiber_, rt,
                             rt->yield_delay_ms);
    } else if (status != LUA_OK) {
        /* Script error */
        const char *err = lua_tostring(rt->L, -1);
        editor_log_error(rt->editor, "Script error: %s", err);
    }
}
```

### 6.3 C → Lua API Binding Pattern

Each API function follows the same pattern:

```c
/* Lua: spawn_box(pos, size) → entity_id */
static int l_spawn_box_(lua_State *L) {
    script_runtime_t *rt = lua_touserdata(L, lua_upvalueindex(1));

    vec3_t pos = l_check_vec3_(L, 1);
    vec3_t size = l_check_vec3_(L, 2);

    spawn_desc_t desc = {
        .type = SPAWN_BOX,
        .position = pos,
        .size = size,
    };

    entity_t ent = editor_spawn_entity(rt->editor, &desc);
    lua_pushinteger(L, ent.index);
    return 1;
}
```

### 6.4 Safety

- Scripts run in a sandboxed Lua state (no `os.execute`, `io`, `loadlib`)
- Memory limit enforced via custom allocator (arena-backed)
- Instruction count limit prevents infinite loops (via `lua_sethook`)
- Scripts cannot directly access C pointers

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

MCP uses **JSON-RPC 2.0** over stdio (when the controller is launched as an
MCP subprocess) or over TCP (when running as a standalone MCP endpoint).

The controller process acts as the MCP server. It translates MCP tool calls
into edit protocol commands and MCP resource reads into state queries.

### 8.2 Architecture

```
AI Agent (Claude/etc)
    │
    │  JSON-RPC 2.0 (stdio or TCP)
    │
    ▼
MCP Server (inside controller process)
    │
    ├── Tool call → edit protocol command → Server
    │                                         │
    │                                         ▼
    │                               (executes command)
    │                                         │
    │ ◄── response ──────────────────────────┘
    │
    ├── Resource read → client state query → Client
    │                                         │
    │ ◄── cursor/camera/selection ───────────┘
    │
    └── Resource read → asset catalog query → Server
                                               │
        ◄── asset list ───────────────────────┘
```

### 8.3 Tool Mapping

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

### 8.4 Resource Mapping

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

### 9.3 Rendering

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

### 9.4 Tab Completion Engine

```c
typedef struct ctrl_complete {
    char candidates[MAX_CANDIDATES][MAX_CANDIDATE_LEN];
    uint32_t count;
    uint32_t selected;          /* currently highlighted candidate */
    char prefix[256];           /* the prefix being completed */
    bool active;                /* completion popup visible? */
} ctrl_complete_t;

/* On Tab press: */
static void ctrl_trigger_complete_(ctrl_ctx_t *ctx) {
    /* Extract the current word being typed */
    char prefix[256];
    ctrl_extract_word_at_cursor_(ctx, prefix, sizeof(prefix));

    /* Determine completion context (command name? asset path? entity?) */
    complete_context_t cctx = ctrl_classify_context_(ctx);

    if (cctx == COMPLETE_COMMAND) {
        /* Complete from built-in command list (local) */
        ctrl_complete_commands_(ctx, prefix);
    } else {
        /* Ask server for completions */
        ctrl_send_complete_request_(ctx, prefix);
        /* Response arrives asynchronously via server_fd */
    }
}
```

---

## 10. Build Integration

### 10.1 Makefile Targets

```makefile
# Editor server (server + editor extensions + Lua)
build/editor_server: $(SERVER_OBJS) $(EDITOR_SERVER_OBJS) $(LUA_OBJS)
	$(CC) $(CFLAGS) -DEDITOR_ENABLE -DLUA_ENABLE -o $@ $^ $(LDFLAGS)

# Editor client (client + editor mode)
build/editor_client: $(CLIENT_OBJS) $(EDITOR_CLIENT_OBJS)
	$(CC) $(CFLAGS) -DEDITOR_ENABLE -o $@ $^ $(LDFLAGS)

# Controller (standalone TUI process)
build/editor_ctrl: $(CTRL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

### 10.2 Lua Integration

Lua 5.4 is built from source as part of the project (in `third_party/lua/`).
This avoids system dependency issues and keeps the build self-contained.

```makefile
LUA_DIR = third_party/lua
LUA_SRCS = $(wildcard $(LUA_DIR)/*.c)
LUA_SRCS := $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, $(LUA_SRCS))
LUA_OBJS = $(LUA_SRCS:.c=.o)
```

---

## 11. Threading and Synchronization

### 11.1 Server-Side Threading

```
Main thread
  │
  ├── Physics job system (N workers)
  │     └── phys_world_tick_parallel()
  │
  ├── Net job system (1-2 workers)
  │     ├── per-client RUDP fibers (existing)
  │     ├── edit_socket_fiber_ (NEW)
  │     ├── edit_client_fiber_ (NEW, one per controller)
  │     └── asset_download_fiber_ (NEW)
  │
  └── Net pump thread (existing)
        └── UDP recv loop
```

The edit socket and asset download fibers run on the **net job system**, which
means they share the same OS thread(s) as client RUDP fibers. This is fine
because:
- Edit commands are low-frequency (< 100/sec)
- Asset downloads are IO-bound (TCP send)
- Neither is CPU-intensive

### 11.2 Locking

Entity manipulation from the edit socket must be synchronized with the
physics tick. Options:

1. **Queue-based (preferred):** edit commands enqueue into a thread-safe
   command ring. The main tick loop drains the ring between ticks. Zero locking
   in the hot path.

2. **Spinlock:** if immediate feedback is needed, use `job_spinlock_t` around
   entity creation/destruction. Must be very short-lived.

The queue approach aligns with the existing architecture (inbound topic drain
in Stage 1 of the server tick).

### 11.3 Client-Side Threading

```
Main thread (SDL + GL)
  │
  ├── Renders cursor, gizmos, grid (NEW)
  ├── Handles client state socket (NEW, polled in frame loop)
  │
  └── Net IO thread (existing)
        └── UDP recv + RUDP reassembly
```

The client state socket (TCP) is polled in the main loop alongside SDL events.
Non-blocking reads ensure no stalls.

---

## 12. Phased Implementation Plan

### Phase 1: Foundation
- [ ] Edit socket (TCP listener + JSON protocol)
- [ ] Command dispatch framework
- [ ] Basic commands: spawn box/sphere, delete, move, cursor set
- [ ] Undo/redo stack
- [ ] Controller TUI (status bar + log + command-line)
- [ ] Controller ↔ server TCP connection
- [ ] 3D cursor rendering on client
- [ ] Client state socket (cursor query)

### Phase 2: Asset System
- [ ] Asset registry (catalog + listing)
- [ ] Asset downloader (TCP transfer)
- [ ] Client asset cache
- [ ] Tab-completion for asset paths
- [ ] Browse command

### Phase 3: Scripting
- [ ] Lua 5.4 integration (third_party/lua/)
- [ ] Script runtime fiber
- [ ] Entity manipulation API bindings
- [ ] Math/vec3/quat bindings
- [ ] run/eval/repl commands

### Phase 4: Texture Synthesis
- [ ] Noise generators (perlin, simplex, voronoi, fractal)
- [ ] Blend modes
- [ ] UV bake engine
- [ ] texsynth commands
- [ ] Lua texture API

### Phase 5: Polish & MCP
- [ ] MCP server in controller
- [ ] Full keybinding system
- [ ] Selection system (multi-select, query select)
- [ ] Grid/snap refinement
- [ ] Level save/load (JSON)
- [ ] Prefab system

### Phase 6: Advanced
- [ ] Gizmo rendering (translate/rotate/scale handles)
- [ ] Entity property editor (in TUI)
- [ ] Hot-reload for scripts and assets
- [ ] Material assignment workflow
- [ ] Undo grouping for scripts
