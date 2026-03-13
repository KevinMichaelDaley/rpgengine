/**
 * @file scene_viewport_render.c
 * @brief Viewport renderer lifecycle: FBO, shader, mesh, pipeline init.
 *
 * Uses the existing renderer infrastructure:
 *   - render_pipeline_t for pass management
 *   - mesh_registry_t for mesh storage (all entity types)
 *   - shader_program_t + shader_uniform_cache_t for uniforms
 *   - vao_t / vbo_t for grid geometry
 *
 * Non-static functions (4 / 4 limit):
 *   viewport_render_init
 *   viewport_render_destroy
 *   viewport_render_resize
 *   viewport_render_get_texture
 */

#include "ferrum/editor/scene/scene_viewport_render.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/vao_attribute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Default entity mesh cache capacity. */
#define ENTITY_MESH_CACHE_CAP 4096

/* ---- GL proc loader macro (matches clay_backend.c pattern) ---- */

/**
 * @brief Load a GL function pointer from the loader into a struct field.
 *
 * Uses memcpy to avoid the ISO C pedantic warning about void* to
 * function pointer conversion (same pattern as clay_backend.c).
 */
#define LOAD_GL_PROC(field, loader_ptr, name)                 \
    do {                                                       \
        void *raw_ = (loader_ptr)->get_proc_address(           \
            (name), (loader_ptr)->user_data);                  \
        memcpy(&(field), &raw_, sizeof(field));                \
    } while (0)

/* ---- Shader sources ---- */

/** Simple Blinn-Phong vertex shader for entity rendering. */
static const char *const ENTITY_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec3 in_normal;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec3 v_world_pos;\n"
    "out vec3 v_normal;\n"
    "void main() {\n"
    "    vec4 world = u_model * vec4(in_position, 1.0);\n"
    "    v_world_pos = world.xyz;\n"
    "    v_normal = mat3(u_model) * in_normal;\n"
    "    gl_Position = u_projection * u_view * world;\n"
    "}\n";

/** Simple Blinn-Phong fragment shader. */
static const char *const ENTITY_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "uniform vec3 u_color;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_eye_pos;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 l = normalize(u_light_dir);\n"
    "    float diff = max(dot(n, l), 0.0);\n"
    "    vec3 v = normalize(u_eye_pos - v_world_pos);\n"
    "    vec3 h = normalize(l + v);\n"
    "    float spec = pow(max(dot(n, h), 0.0), 32.0);\n"
    "    vec3 ambient = 0.15 * u_color;\n"
    "    vec3 result = ambient + diff * u_color + 0.3 * spec * vec3(1.0);\n"
    "    frag_color = vec4(result, 1.0);\n"
    "}\n";

/** Grid vertex shader (position + color per vertex). */
static const char *const GRID_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec3 in_color;\n"
    "uniform mat4 u_vp;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    v_color = in_color;\n"
    "    gl_Position = u_vp * vec4(in_position, 1.0);\n"
    "}\n";

/** Grid fragment shader. */
static const char *const GRID_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = vec4(v_color, 1.0);\n"
    "}\n";

/* ---- Grid generation ---- */

/** Grid extends from -GRID_HALF to +GRID_HALF in X and Z. */
#define GRID_HALF  20
/** Grid line spacing in world units. */
#define GRID_STEP  1
/** Number of grid lines per axis. */
#define GRID_LINES_PER_AXIS ((2 * GRID_HALF / GRID_STEP) + 1)
/** Total grid lines (both X-parallel and Z-parallel). */
#define GRID_LINE_COUNT (GRID_LINES_PER_AXIS * 2)
/** Floats per vertex: 3 position + 3 color. */
#define GRID_FLOATS_PER_VERT 6
/** Grid vertex stride in bytes. */
#define GRID_STRIDE (GRID_FLOATS_PER_VERT * sizeof(float))
/** 2 vertices per line segment. */
#define GRID_TOTAL_VERTS (GRID_LINE_COUNT * 2)

/**
 * @brief Generate grid line vertex data into a buffer.
 *
 * Each vertex has 6 floats: position xyz + color rgb.
 * The origin axis lines are brighter than other grid lines.
 *
 * @param verts  Output buffer (must hold GRID_TOTAL_VERTS * 6 floats).
 * @return Number of vertices written.
 */
static int generate_grid(float *verts) {
    int vi = 0;
    float half = (float)GRID_HALF;

    for (int i = -GRID_HALF; i <= GRID_HALF; i += GRID_STEP) {
        float fi = (float)i;
        /* Axis lines (through origin) are brighter. */
        float cr, cg, cb;
        if (i == 0) {
            cr = 0.5f; cg = 0.5f; cb = 0.5f;
        } else {
            cr = 0.2f; cg = 0.2f; cb = 0.2f;
        }

        /* Line along Z (constant X). */
        verts[vi * GRID_FLOATS_PER_VERT + 0] = fi;
        verts[vi * GRID_FLOATS_PER_VERT + 1] = 0.0f;
        verts[vi * GRID_FLOATS_PER_VERT + 2] = -half;
        verts[vi * GRID_FLOATS_PER_VERT + 3] = cr;
        verts[vi * GRID_FLOATS_PER_VERT + 4] = cg;
        verts[vi * GRID_FLOATS_PER_VERT + 5] = cb;
        vi++;
        verts[vi * GRID_FLOATS_PER_VERT + 0] = fi;
        verts[vi * GRID_FLOATS_PER_VERT + 1] = 0.0f;
        verts[vi * GRID_FLOATS_PER_VERT + 2] = half;
        verts[vi * GRID_FLOATS_PER_VERT + 3] = cr;
        verts[vi * GRID_FLOATS_PER_VERT + 4] = cg;
        verts[vi * GRID_FLOATS_PER_VERT + 5] = cb;
        vi++;

        /* Line along X (constant Z). */
        verts[vi * GRID_FLOATS_PER_VERT + 0] = -half;
        verts[vi * GRID_FLOATS_PER_VERT + 1] = 0.0f;
        verts[vi * GRID_FLOATS_PER_VERT + 2] = fi;
        verts[vi * GRID_FLOATS_PER_VERT + 3] = cr;
        verts[vi * GRID_FLOATS_PER_VERT + 4] = cg;
        verts[vi * GRID_FLOATS_PER_VERT + 5] = cb;
        vi++;
        verts[vi * GRID_FLOATS_PER_VERT + 0] = half;
        verts[vi * GRID_FLOATS_PER_VERT + 1] = 0.0f;
        verts[vi * GRID_FLOATS_PER_VERT + 2] = fi;
        verts[vi * GRID_FLOATS_PER_VERT + 3] = cr;
        verts[vi * GRID_FLOATS_PER_VERT + 4] = cg;
        verts[vi * GRID_FLOATS_PER_VERT + 5] = cb;
        vi++;
    }

    return vi;
}

/* ---- GL function resolution ---- */

/**
 * @brief Resolve GL function pointers needed for FBO and draw operations.
 */
static bool resolve_gl_functions(viewport_render_state_t *state) {
    const gl_loader_t *l = &state->loader;

    LOAD_GL_PROC(state->glGenFramebuffers,        l, "glGenFramebuffers");
    LOAD_GL_PROC(state->glDeleteFramebuffers,      l, "glDeleteFramebuffers");
    LOAD_GL_PROC(state->glBindFramebuffer,         l, "glBindFramebuffer");
    LOAD_GL_PROC(state->glFramebufferTexture2D,    l, "glFramebufferTexture2D");
    LOAD_GL_PROC(state->glFramebufferRenderbuffer, l, "glFramebufferRenderbuffer");
    LOAD_GL_PROC(state->glCheckFramebufferStatus,  l, "glCheckFramebufferStatus");
    LOAD_GL_PROC(state->glGenRenderbuffers,        l, "glGenRenderbuffers");
    LOAD_GL_PROC(state->glDeleteRenderbuffers,     l, "glDeleteRenderbuffers");
    LOAD_GL_PROC(state->glBindRenderbuffer,        l, "glBindRenderbuffer");
    LOAD_GL_PROC(state->glRenderbufferStorage,     l, "glRenderbufferStorage");
    LOAD_GL_PROC(state->glGenTextures,             l, "glGenTextures");
    LOAD_GL_PROC(state->glDeleteTextures,          l, "glDeleteTextures");
    LOAD_GL_PROC(state->glBindTexture,             l, "glBindTexture");
    LOAD_GL_PROC(state->glTexImage2D,              l, "glTexImage2D");
    LOAD_GL_PROC(state->glTexParameteri,           l, "glTexParameteri");
    LOAD_GL_PROC(state->glViewport,                l, "glViewport");
    LOAD_GL_PROC(state->glClearColor,              l, "glClearColor");
    LOAD_GL_PROC(state->glClear,                   l, "glClear");
    LOAD_GL_PROC(state->glEnable,                  l, "glEnable");
    LOAD_GL_PROC(state->glDisable,                 l, "glDisable");
    LOAD_GL_PROC(state->glCullFace,                l, "glCullFace");
    LOAD_GL_PROC(state->glDrawArrays,              l, "glDrawArrays");

    /* Minimum check — glBindFramebuffer is essential for FBO rendering. */
    return state->glBindFramebuffer != NULL;
}

/* ---- FBO helpers ---- */

/**
 * @brief Create the FBO with color texture and depth renderbuffer.
 */
static bool create_fbo(viewport_render_state_t *state, int w, int h) {
    /* Create color texture attachment. */
    state->glGenTextures(1, &state->color_tex);
    state->glBindTexture(GL_TEXTURE_2D, state->color_tex);
    state->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Create depth renderbuffer attachment. */
    state->glGenRenderbuffers(1, &state->depth_rbo);
    state->glBindRenderbuffer(GL_RENDERBUFFER, state->depth_rbo);
    state->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

    /* Create and configure FBO. */
    state->glGenFramebuffers(1, &state->fbo);
    state->glBindFramebuffer(GL_FRAMEBUFFER, state->fbo);
    state->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, state->color_tex, 0);
    state->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, state->depth_rbo);

    uint32_t status = state->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    state->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "viewport_render: FBO incomplete (status=0x%X)\n",
                status);
        return false;
    }

    state->fbo_width = w;
    state->fbo_height = h;
    return true;
}

/**
 * @brief Delete the FBO, color texture, and depth renderbuffer.
 */
static void destroy_fbo(viewport_render_state_t *state) {
    if (state->fbo) {
        state->glDeleteFramebuffers(1, &state->fbo);
        state->fbo = 0;
    }
    if (state->color_tex) {
        state->glDeleteTextures(1, &state->color_tex);
        state->color_tex = 0;
    }
    if (state->depth_rbo) {
        state->glDeleteRenderbuffers(1, &state->depth_rbo);
        state->depth_rbo = 0;
    }
}

/**
 * @brief Create the grid VAO/VBO using renderer wrappers.
 */
static bool create_grid(viewport_render_state_t *state) {
    float verts[GRID_TOTAL_VERTS * GRID_FLOATS_PER_VERT];
    int vert_count = generate_grid(verts);
    state->grid_vertex_count = vert_count;

    const gl_loader_t *loader = &state->loader;

    /* Create VBO and upload grid vertex data. */
    if (vbo_create(&state->grid_vbo, loader) != VBO_OK) {
        fprintf(stderr, "viewport_render: grid VBO creation failed\n");
        return false;
    }
    vbo_upload(&state->grid_vbo, GL_ARRAY_BUFFER, verts,
               (size_t)(vert_count * GRID_FLOATS_PER_VERT) * sizeof(float),
               GL_STATIC_DRAW);

    /* Create VAO and bind grid vertex attributes. */
    if (vao_create(&state->grid_vao, loader) != VAO_OK) {
        fprintf(stderr, "viewport_render: grid VAO creation failed\n");
        vbo_destroy(&state->grid_vbo);
        return false;
    }

    /* Attribute layout: position(vec3) + color(vec3) = 24 bytes stride. */
    vao_attribute_t attrs[2] = {
        {0, 3, GL_FLOAT, GL_FALSE, 0,                  0}, /* position */
        {1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),  0}, /* color */
    };
    vao_bind_attributes(&state->grid_vao, &state->grid_vbo, attrs, 2,
                        GRID_STRIDE);

    return true;
}


/* ---- Public API ---- */

bool viewport_render_init(viewport_render_state_t *state,
                           const viewport_render_config_t *config) {
    if (!state || !config) return false;
    if (state->initialized) return false;

    memset(state, 0, sizeof(*state));
    state->loader = config->loader;

    /* Resolve GL function pointers for FBO operations. */
    if (!resolve_gl_functions(state)) {
        fprintf(stderr, "viewport_render: failed to resolve GL functions\n");
        return false;
    }

    /* Create the off-screen FBO. */
    int w = config->initial_width > 0 ? config->initial_width : 640;
    int h = config->initial_height > 0 ? config->initial_height : 480;
    if (!create_fbo(state, w, h)) {
        return false;
    }

    /* Initialize the render pipeline (9 passes, 256 draw commands each). */
    if (render_pipeline_init(&state->pipeline, 256) != RENDER_PIPELINE_OK) {
        fprintf(stderr, "viewport_render: pipeline init failed\n");
        destroy_fbo(state);
        return false;
    }

    /* Compile the entity shader (Blinn-Phong). */
    {
        char log[512];
        shader_program_status_t ss = shader_program_create(
            &state->shader, &config->loader, ENTITY_VERT_SRC, ENTITY_FRAG_SRC,
            log, sizeof(log));
        if (ss != SHADER_PROGRAM_OK) {
            fprintf(stderr, "viewport_render: entity shader error: %s\n", log);
            render_pipeline_destroy(&state->pipeline);
            destroy_fbo(state);
            return false;
        }
        shader_uniform_cache_init(&state->uniforms, &state->shader);
    }

    /* Compile the grid shader (unlit lines). */
    {
        char log[512];
        shader_program_status_t ss = shader_program_create(
            &state->grid_shader, &config->loader, GRID_VERT_SRC, GRID_FRAG_SRC,
            log, sizeof(log));
        if (ss != SHADER_PROGRAM_OK) {
            fprintf(stderr, "viewport_render: grid shader error: %s\n", log);
            shader_program_destroy(&state->shader);
            render_pipeline_destroy(&state->pipeline);
            destroy_fbo(state);
            return false;
        }
        shader_uniform_cache_init(&state->grid_uniforms, &state->grid_shader);
    }

    /* Create grid geometry using renderer VAO/VBO wrappers. */
    if (!create_grid(state)) {
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }

    /* Initialize mesh registry for dynamic/loaded meshes.
     * Capacity 256 covers typical editor scenes. */
    if (mesh_registry_init(&state->meshes, 256, &config->loader) != 0) {
        fprintf(stderr, "viewport_render: mesh registry init failed\n");
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }

    /* Allocate entity mesh cache (maps entity_id → mesh_handle).
     * Initialized with UINT32_MAX index as sentinel ("no mesh"). */
    state->entity_mesh_cache = malloc(ENTITY_MESH_CACHE_CAP
                                       * sizeof(mesh_handle_t));
    if (!state->entity_mesh_cache) {
        fprintf(stderr, "viewport_render: entity mesh cache alloc failed\n");
        mesh_registry_destroy(&state->meshes);
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }
    state->entity_mesh_cache_cap = ENTITY_MESH_CACHE_CAP;
    /* Fill with sentinel: index=UINT32_MAX means "no mesh loaded". */
    for (uint32_t i = 0; i < ENTITY_MESH_CACHE_CAP; ++i) {
        state->entity_mesh_cache[i].index = UINT32_MAX;
        state->entity_mesh_cache[i].generation = 0;
    }

    /* Initialize camera with default orbit position. */
    editor_camera_init(&state->camera);

    state->initialized = true;
    printf("viewport_render: initialized (%dx%d FBO)\n", w, h);
    return true;
}

void viewport_render_destroy(viewport_render_state_t *state) {
    if (!state || !state->initialized) return;

    /* Free entity mesh cache (meshes themselves freed by registry destroy). */
    free(state->entity_mesh_cache);
    state->entity_mesh_cache = NULL;
    state->entity_mesh_cache_cap = 0;

    mesh_registry_destroy(&state->meshes);

    vao_destroy(&state->grid_vao);
    vbo_destroy(&state->grid_vbo);

    shader_program_destroy(&state->grid_shader);
    shader_program_destroy(&state->shader);

    render_pipeline_destroy(&state->pipeline);
    destroy_fbo(state);

    memset(state, 0, sizeof(*state));
}

void viewport_render_resize(viewport_render_state_t *state,
                             int width, int height) {
    if (!state || !state->initialized) return;
    if (width <= 0 || height <= 0) return;
    if (width == state->fbo_width && height == state->fbo_height) return;

    destroy_fbo(state);
    create_fbo(state, width, height);
}

uint32_t viewport_render_get_texture(const viewport_render_state_t *state) {
    if (!state || !state->initialized) return 0;
    return state->color_tex;
}
