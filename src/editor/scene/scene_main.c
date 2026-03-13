/**
 * @file scene_main.c
 * @brief Scene editor entry point, SDL2 init, main loop.
 *
 * Creates an SDL2 window with an OpenGL 4.6 core context, initializes
 * the panel layout, server connection, entity store, and editor
 * subsystems, then runs the main loop with interactive Clay UI.
 */

#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_frame.h"
#include "ferrum/editor/scene/scene_input.h"
#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/ui/glad_gl_loader.h"
#include "ferrum/editor/ui/clay_theme.h"

#include <SDL2/SDL.h>
#include <glad/glad.h>

#include "clay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Default configuration ---- */

#define DEFAULT_WINDOW_W     1280
#define DEFAULT_WINDOW_H     720
#define DEFAULT_WINDOW_TITLE "Scene Editor"
#define DEFAULT_ARENA_SIZE   (4u * 1024u * 1024u) /* 4 MB */
#define DEFAULT_SERVER_HOST  "127.0.0.1"
#define DEFAULT_SERVER_PORT  9100
#define DEFAULT_ENTITY_CAP   4096

/* ---- Clay text measurement callback ---- */

/**
 * @brief Measure text dimensions for Clay layout.
 *
 * Uses the font glyph metrics from the Clay backend's font set.
 * The user_data pointer is a clay_backend_t*.
 */
static Clay_Dimensions measure_text_callback(Clay_StringSlice text,
                                              Clay_TextElementConfig *config,
                                              void *user_data) {
    clay_backend_t *be = (clay_backend_t *)user_data;
    int font_id = config->fontId;
    if (font_id >= CLAY_FONT_COUNT) font_id = 0;

    float base_size = be->fonts.font_sizes[font_id];
    float scale = (base_size > 0.0f)
                  ? (float)config->fontSize / base_size : 1.0f;

    float w = 0.0f;
    for (int32_t i = 0; i < text.length; i++) {
        char ch = text.chars[i];
        if (ch >= 32 && ch < 127) {
            w += be->fonts.glyphs[font_id][(int)ch].advance_x * scale;
        } else {
            w += be->fonts.glyphs[font_id][' '].advance_x * scale;
        }
        if (i > 0) w += (float)config->letterSpacing;
    }
    float h = (float)config->fontSize;
    if (config->lineHeight > 0) h = (float)config->lineHeight;
    return (Clay_Dimensions){w, h};
}

/* ---- Internal helpers ---- */

/**
 * @brief Resolve zero-initialized config fields to defaults.
 */
static scene_editor_config_t resolve_config(const scene_editor_config_t *cfg) {
    scene_editor_config_t out = *cfg;
    if (out.window_w <= 0) out.window_w = DEFAULT_WINDOW_W;
    if (out.window_h <= 0) out.window_h = DEFAULT_WINDOW_H;
    if (!out.window_title) out.window_title = DEFAULT_WINDOW_TITLE;
    if (out.arena_size == 0) out.arena_size = DEFAULT_ARENA_SIZE;
    if (!out.server_host) out.server_host = DEFAULT_SERVER_HOST;
    if (out.server_port == 0) out.server_port = DEFAULT_SERVER_PORT;
    if (out.ui_scale <= 0.0f) out.ui_scale = 2.0f;
    return out;
}

/**
 * @brief Build the complete Clay UI layout for the current frame.
 *
 * Dispatches to panel-specific UI builders for interactive content.
 */
static void build_clay_layout(scene_editor_t *ed, int ww, int wh) {
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)ww, (float)wh});
    /* Use mouse_clicked to ensure Clay sees the press even if the
     * button was released within the same frame's event batch. */
    bool pointer_down = ed->ui.mouse_down || ed->ui.mouse_clicked;
    /* Mouse coordinates from SDL are in window (physical) pixels;
     * convert to logical pixels for Clay when UI is scaled. */
    float mx = ed->ui.mouse_x;
    float my = ed->ui.mouse_y;
    if (ed->clay_be.ui_scale > 1.0f) {
        mx /= ed->clay_be.ui_scale;
        my /= ed->clay_be.ui_scale;
    }
    Clay_SetPointerState(
        (Clay_Vector2){mx, my},
        pointer_down);
    Clay_UpdateScrollContainers(true,
        (Clay_Vector2){0, ed->ui.scroll_delta_y}, 1.0f / 60.0f);
    ed->ui.scroll_delta_y = 0.0f;
    Clay_BeginLayout();

    /* Root container spanning the full window. */
    CLAY(CLAY_ID("Root"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED((float)ww),
                       CLAY_SIZING_FIXED((float)wh)},
        },
        .backgroundColor = {THEME_BG_VIEWPORT_R, THEME_BG_VIEWPORT_G,
                             THEME_BG_VIEWPORT_B, THEME_BG_VIEWPORT_A},
    }) {
        /* Build each visible panel with its interactive content. */
        for (int i = 0; i < PANEL_COUNT; ++i) {
            if (!panel_layout_is_visible(&ed->layout, (panel_id_t)i))
                continue;
            panel_rect_t r = panel_layout_get_rect(&ed->layout,
                                                    (panel_id_t)i);
            if (r.w <= 0 || r.h <= 0) continue;

            switch ((panel_id_t)i) {
            case PANEL_OUTLINER:
                scene_ui_build_outliner(ed, &r);
                break;
            case PANEL_VIEWPORT:
                scene_ui_build_viewport(ed, &r);
                break;
            case PANEL_INSPECTOR:
                scene_ui_build_inspector(ed, &r);
                break;
            case PANEL_TUI:
                scene_ui_build_tui(ed, &r);
                break;
            default:
                break;
            }
        }
    }
}

/**
 * @brief Render one frame using Clay UI.
 *
 * Builds the Clay layout, ends layout to produce render commands,
 * and dispatches them through the clay backend renderer.
 */
static void render_frame(scene_editor_t *ed) {
    /* Use drawable size (physical pixels) for GL viewport. */
    int dw, dh;
    SDL_GL_GetDrawableSize(ed->window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Render 3D scene into viewport FBO (before Clay UI overlay).
     * This changes glViewport to the FBO size, so restore it after. */
    viewport_render_draw_scene(ed);
    glViewport(0, 0, dw, dh);

    /* Update backend with physical drawable size. */
    clay_backend_resize(&ed->clay_be, dw, dh);

    /* Build Clay layout in logical pixels (physical / ui_scale).
     * The panel layout must match so panel rects fit the Clay area. */
    float sc = ed->clay_be.ui_scale;
    if (sc < 1.0f) sc = 1.0f;
    int lw = (int)((float)dw / sc);
    int lh = (int)((float)dh / sc);
    panel_layout_resize(&ed->layout, lw, lh);
    build_clay_layout(ed, lw, lh);
    Clay_RenderCommandArray cmds = Clay_EndLayout();
    clay_backend_render(&ed->clay_be, cmds);

    SDL_GL_SwapWindow(ed->window);
}

/* ---- Public API ---- */

bool scene_editor_init(scene_editor_t *ed, const scene_editor_config_t *config) {
    if (ed->initialized) return false;
    memset(ed, 0, sizeof(*ed));

    ed->config = resolve_config(config);
    ed->dragging_divider = DIVIDER_NONE;

    /* Initialize SDL2 */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "scene_editor: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    /* Request OpenGL 4.6 core profile */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    /* Create window */
    ed->window = SDL_CreateWindow(
        ed->config.window_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ed->config.window_w, ed->config.window_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!ed->window) {
        fprintf(stderr, "scene_editor: SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return false;
    }

    /* Create GL context */
    ed->gl_ctx = SDL_GL_CreateContext(ed->window);
    if (!ed->gl_ctx) {
        fprintf(stderr, "scene_editor: SDL_GL_CreateContext failed: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(ed->window, ed->gl_ctx);

    /* Load GL function pointers via GLAD */
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "scene_editor: GLAD loader failed\n");
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Enable vsync */
    SDL_GL_SetSwapInterval(1);

    /* Initialize arena */
    ed->arena_buf = malloc(ed->config.arena_size);
    if (!ed->arena_buf) {
        fprintf(stderr, "scene_editor: arena allocation failed\n");
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }
    arena_init(&ed->arena, ed->arena_buf, ed->config.arena_size);

    /* Initialize panel layout */
    panel_layout_init(&ed->layout, ed->config.window_w, ed->config.window_h);

    /* Initialize snap state */
    snap_state_init(&ed->snap);

    /* Initialize entity store */
    if (!edit_entity_store_init(&ed->entities, DEFAULT_ENTITY_CAP)) {
        fprintf(stderr, "scene_editor: entity store init failed\n");
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Initialize selection */
    if (!edit_selection_init(&ed->selection)) {
        fprintf(stderr, "scene_editor: selection init failed\n");
        edit_entity_store_destroy(&ed->entities);
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Initialize server connection */
    scene_conn_config_t conn_cfg = {
        .host     = ed->config.server_host,
        .tcp_port = ed->config.server_port,
        .udp_port = 0,
    };
    if (!scene_connection_init(&ed->connection, &conn_cfg)) {
        fprintf(stderr, "scene_editor: connection init failed\n");
        edit_selection_destroy(&ed->selection);
        edit_entity_store_destroy(&ed->entities);
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Initialize sync state */
    if (!scene_sync_init(&ed->sync, NULL)) {
        fprintf(stderr, "scene_editor: sync init failed\n");
        scene_connection_destroy(&ed->connection);
        edit_selection_destroy(&ed->selection);
        edit_entity_store_destroy(&ed->entities);
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Initialize Clay UI library. */
    uint32_t clay_size = Clay_MinMemorySize();
    ed->clay_mem = malloc(clay_size);
    if (!ed->clay_mem) {
        fprintf(stderr, "scene_editor: Clay memory allocation failed\n");
        scene_sync_destroy(&ed->sync);
        scene_connection_destroy(&ed->connection);
        edit_selection_destroy(&ed->selection);
        edit_entity_store_destroy(&ed->entities);
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }
    Clay_Arena clay_arena = Clay_CreateArenaWithCapacityAndMemory(
        clay_size, ed->clay_mem);
    Clay_Dimensions clay_dims = {(float)ed->config.window_w,
                                  (float)ed->config.window_h};
    Clay_ErrorHandler clay_err = {0};
    ed->clay_ctx = Clay_Initialize(clay_arena, clay_dims, clay_err);
    if (!ed->clay_ctx) {
        fprintf(stderr, "scene_editor: Clay_Initialize failed\n");
        free(ed->clay_mem);
        scene_sync_destroy(&ed->sync);
        scene_connection_destroy(&ed->connection);
        edit_selection_destroy(&ed->selection);
        edit_entity_store_destroy(&ed->entities);
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Initialize Clay backend renderer. */
    gl_loader_t gl_loader = glad_gl_loader_get();
    clay_backend_config_t be_cfg = {
        .window_w = ed->config.window_w,
        .window_h = ed->config.window_h,
        .loader   = gl_loader,
    };
    if (!clay_backend_init(&ed->clay_be, &be_cfg)) {
        fprintf(stderr, "scene_editor: Clay backend init failed\n");
        free(ed->clay_mem);
        scene_sync_destroy(&ed->sync);
        scene_connection_destroy(&ed->connection);
        edit_selection_destroy(&ed->selection);
        edit_entity_store_destroy(&ed->entities);
        free(ed->arena_buf);
        SDL_GL_DeleteContext(ed->gl_ctx);
        SDL_DestroyWindow(ed->window);
        SDL_Quit();
        return false;
    }

    /* Apply UI scale from config. */
    ed->clay_be.ui_scale = ed->config.ui_scale;

    /* Register the text measurement callback. */
    Clay_SetMeasureTextFunction(measure_text_callback, &ed->clay_be);

    /* Initialize viewport 3D renderer. */
    {
        viewport_render_config_t vp_cfg = {
            .initial_width  = ed->config.window_w,
            .initial_height = ed->config.window_h,
            .loader         = gl_loader,
        };
        if (!viewport_render_init(&ed->viewport, &vp_cfg)) {
            fprintf(stderr, "scene_editor: viewport render init failed\n");
            /* Non-fatal: editor still usable without 3D rendering. */
        }
    }

    /* Try to connect to editor server (non-fatal if it fails). */
    if (scene_connection_connect(&ed->connection)) {
        ed->connected = true;
        printf("Connected to editor server at %s:%u\n",
               ed->config.server_host, ed->config.server_port);
        /* Request initial entity list. */
        scene_frame_request_entity_list(ed);
    } else {
        ed->connected = false;
        printf("Could not connect to editor server at %s:%u (running offline)\n",
               ed->config.server_host, ed->config.server_port);
    }

    ed->running = true;
    ed->initialized = true;

    printf("Scene editor initialized: %dx%d, GL %s\n",
           ed->config.window_w, ed->config.window_h,
           glGetString(GL_VERSION));

    return true;
}

void scene_editor_shutdown(scene_editor_t *ed) {
    if (!ed->initialized) return;

    viewport_render_destroy_primitives();
    viewport_render_destroy(&ed->viewport);

    scene_sync_destroy(&ed->sync);
    scene_connection_destroy(&ed->connection);
    edit_selection_destroy(&ed->selection);
    edit_entity_store_destroy(&ed->entities);

    clay_backend_destroy(&ed->clay_be);
    free(ed->clay_mem);

    if (ed->gl_ctx) {
        SDL_GL_DeleteContext(ed->gl_ctx);
    }
    if (ed->window) {
        SDL_DestroyWindow(ed->window);
    }
    free(ed->arena_buf);
    SDL_Quit();

    memset(ed, 0, sizeof(*ed));
}

void scene_editor_frame(scene_editor_t *ed) {
    /* Poll events — updates mouse state via scene_input_process. */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        scene_input_process(ed, &event);
    }

    /* Pump server connection and process responses. */
    if (ed->connected) {
        scene_frame_pump(ed);
    }

    /* Preserve any action set during event polling (e.g. TUI command
     * from Enter key) so it survives the layout reset below. */
    scene_ui_action_t pre_layout_action = ed->ui.action;

    /* Reset UI action before layout (Clay_OnHover sets it). */
    ed->ui.action = UI_ACTION_NONE;

    /* Render — builds Clay layout, which sets UI actions via hover callbacks. */
    render_frame(ed);

    /* Post-layout click detection using Clay_PointerOver.
     * This is more reliable than Clay_OnHover + PRESSED_THIS_FRAME
     * because it responds immediately on the click frame. */
    if (ed->ui.mouse_clicked && ed->ui.action == UI_ACTION_NONE) {
        if (Clay_PointerOver(CLAY_ID("Outliner_BtnBox"))) {
            ed->ui.action = UI_ACTION_SPAWN_BOX;
        } else if (Clay_PointerOver(CLAY_ID("Outliner_BtnSphere"))) {
            ed->ui.action = UI_ACTION_SPAWN_SPHERE;
        } else if (Clay_PointerOver(CLAY_ID("Outliner_BtnCapsule"))) {
            ed->ui.action = UI_ACTION_SPAWN_CAPSULE;
        } else if (Clay_PointerOver(CLAY_ID("Outliner_BtnTranslate"))) {
            ed->ui.action = UI_ACTION_MODE_TRANSLATE;
        } else if (Clay_PointerOver(CLAY_ID("Outliner_BtnRotate"))) {
            ed->ui.action = UI_ACTION_MODE_ROTATE;
        } else if (Clay_PointerOver(CLAY_ID("Outliner_BtnScale"))) {
            ed->ui.action = UI_ACTION_MODE_SCALE;
        }
    }

    /* Restore pre-layout action if nothing was set during layout. */
    if (ed->ui.action == UI_ACTION_NONE && pre_layout_action != UI_ACTION_NONE) {
        ed->ui.action = pre_layout_action;
    }

    /* Dispatch any UI action that was set during layout. */
    if (ed->ui.action != UI_ACTION_NONE) {
        scene_frame_dispatch_action(ed);
    }

    /* Clear per-frame click flag and track previous mouse state. */
    ed->ui.mouse_clicked = false;
    ed->ui.mouse_was_down = ed->ui.mouse_down;
}

void scene_editor_run(scene_editor_t *ed) {
    while (ed->running) {
        scene_editor_frame(ed);
    }
}
