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

#include "ferrum/memory/vm_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Default entity mesh cache capacity (1M entities, demand-paged). */
#define DEFAULT_ENTITY_CACHE_CAP (1024u * 1024u)

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

/** Simple Blinn-Phong fragment shader with selection tint. */
static const char *const ENTITY_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "uniform vec3 u_color;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform vec3 u_select_color;\n"
    "uniform float u_select_tint;\n"
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
    "    result = mix(result, u_select_color, u_select_tint);\n"
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
    LOAD_GL_PROC(state->glDepthMask,               l, "glDepthMask");
    LOAD_GL_PROC(state->glDrawArrays,              l, "glDrawArrays");
    LOAD_GL_PROC(state->glLineWidth,               l, "glLineWidth");
    LOAD_GL_PROC(state->glPolygonMode,             l, "glPolygonMode");
    LOAD_GL_PROC(state->glStencilFunc,             l, "glStencilFunc");
    LOAD_GL_PROC(state->glStencilOp,               l, "glStencilOp");
    LOAD_GL_PROC(state->glStencilMask,             l, "glStencilMask");
    LOAD_GL_PROC(state->glColorMask,               l, "glColorMask");

    /* MSAA functions. */
    LOAD_GL_PROC(state->glRenderbufferStorageMultisample, l,
                 "glRenderbufferStorageMultisample");
    LOAD_GL_PROC(state->glBlitFramebuffer, l, "glBlitFramebuffer");
    LOAD_GL_PROC(state->glGetIntegerv,     l, "glGetIntegerv");

    /* Minimum check — glBindFramebuffer is essential for FBO rendering. */
    return state->glBindFramebuffer != NULL;
}

/* ---- FBO helpers ---- */

/** Desired MSAA sample count (capped to GL_MAX_SAMPLES at runtime). */
#define MSAA_DESIRED_SAMPLES 16

/**
 * @brief Query the maximum supported MSAA sample count.
 */
static int query_max_samples(viewport_render_state_t *state) {
    if (!state->glGetIntegerv) return 1;
    int32_t max_samples = 1;
    state->glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    return (int)max_samples;
}

/**
 * @brief Create MSAA and resolve FBOs with color texture and depth.
 *
 * The MSAA FBO uses multisample renderbuffers for rendering.
 * The resolve FBO uses a regular texture for display in Clay UI.
 * After rendering, glBlitFramebuffer resolves MSAA → single-sample.
 */
static bool create_fbo(viewport_render_state_t *state, int w, int h) {
    bool has_msaa = state->glRenderbufferStorageMultisample &&
                    state->glBlitFramebuffer;
    int samples = 1;

    if (has_msaa) {
        int max_s = query_max_samples(state);
        samples = MSAA_DESIRED_SAMPLES;
        if (samples > max_s) samples = max_s;
        if (samples < 2) has_msaa = false;
    }
    state->msaa_samples = has_msaa ? samples : 1;

    /* ---- Resolve FBO (single-sample, texture for display) ---- */
    state->glGenTextures(1, &state->color_tex);
    state->glBindTexture(GL_TEXTURE_2D, state->color_tex);
    state->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    state->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    state->glGenRenderbuffers(1, &state->depth_rbo);
    state->glBindRenderbuffer(GL_RENDERBUFFER, state->depth_rbo);
    state->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

    state->glGenFramebuffers(1, &state->fbo);
    state->glBindFramebuffer(GL_FRAMEBUFFER, state->fbo);
    state->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, state->color_tex, 0);
    state->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, state->depth_rbo);

    uint32_t status = state->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "viewport_render: resolve FBO incomplete (0x%X)\n",
                status);
        state->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    /* ---- MSAA FBO (multisample renderbuffers for rendering) ---- */
    if (has_msaa) {
        state->glGenRenderbuffers(1, &state->msaa_color_rbo);
        state->glBindRenderbuffer(GL_RENDERBUFFER, state->msaa_color_rbo);
        state->glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                                GL_RGBA8, w, h);

        state->glGenRenderbuffers(1, &state->msaa_depth_rbo);
        state->glBindRenderbuffer(GL_RENDERBUFFER, state->msaa_depth_rbo);
        state->glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                                GL_DEPTH24_STENCIL8, w, h);

        state->glGenFramebuffers(1, &state->msaa_fbo);
        state->glBindFramebuffer(GL_FRAMEBUFFER, state->msaa_fbo);
        state->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_RENDERBUFFER,
                                          state->msaa_color_rbo);
        state->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                          GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER,
                                          state->msaa_depth_rbo);

        status = state->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr,
                    "viewport_render: MSAA FBO incomplete (0x%X), "
                    "falling back to no MSAA\n", status);
            state->glDeleteFramebuffers(1, &state->msaa_fbo);
            state->glDeleteRenderbuffers(1, &state->msaa_color_rbo);
            state->glDeleteRenderbuffers(1, &state->msaa_depth_rbo);
            state->msaa_fbo = 0;
            state->msaa_color_rbo = 0;
            state->msaa_depth_rbo = 0;
            state->msaa_samples = 1;
        }
    }

    state->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    state->fbo_width = w;
    state->fbo_height = h;
    return true;
}

/**
 * @brief Delete all FBO resources (MSAA + resolve).
 */
static void destroy_fbo(viewport_render_state_t *state) {
    /* MSAA resources. */
    if (state->msaa_fbo) {
        state->glDeleteFramebuffers(1, &state->msaa_fbo);
        state->msaa_fbo = 0;
    }
    if (state->msaa_color_rbo) {
        state->glDeleteRenderbuffers(1, &state->msaa_color_rbo);
        state->msaa_color_rbo = 0;
    }
    if (state->msaa_depth_rbo) {
        state->glDeleteRenderbuffers(1, &state->msaa_depth_rbo);
        state->msaa_depth_rbo = 0;
    }
    /* Resolve resources. */
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

    /* Compile additional shaders (matcap, flat). */
    if (!viewport_render_init_extra_shaders(state)) {
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
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

    /* Create separate overlay VBO/VAO for gizmo, cursor, box select.
     * These are re-uploaded each frame and must not clobber the grid. */
    {
        const gl_loader_t *loader = &state->loader;
        if (vbo_create(&state->overlay_vbo, loader) != VBO_OK) {
            fprintf(stderr, "viewport_render: overlay VBO creation failed\n");
            vao_destroy(&state->grid_vao);
            vbo_destroy(&state->grid_vbo);
            shader_program_destroy(&state->grid_shader);
            shader_program_destroy(&state->shader);
            render_pipeline_destroy(&state->pipeline);
            destroy_fbo(state);
            return false;
        }
        if (vao_create(&state->overlay_vao, loader) != VAO_OK) {
            fprintf(stderr, "viewport_render: overlay VAO creation failed\n");
            vbo_destroy(&state->overlay_vbo);
            vao_destroy(&state->grid_vao);
            vbo_destroy(&state->grid_vbo);
            shader_program_destroy(&state->grid_shader);
            shader_program_destroy(&state->shader);
            render_pipeline_destroy(&state->pipeline);
            destroy_fbo(state);
            return false;
        }
    }

    /* Initialize mesh registry for dynamic/loaded meshes.
     * Capacity 256 covers typical editor scenes. */
    if (mesh_registry_init(&state->meshes, 256, &state->loader) != 0) {
        fprintf(stderr, "viewport_render: mesh registry init failed\n");
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }

    /* Resolve entity cache capacity (config → default). */
    uint32_t cache_cap = config->entity_cache_cap > 0
                         ? config->entity_cache_cap
                         : DEFAULT_ENTITY_CACHE_CAP;
    size_t cache_bytes = (size_t)cache_cap * sizeof(mesh_handle_t);

    /* Reserve entity mesh cache via demand-paged virtual memory.
     * Physical pages are committed only on first write, so 1M+ slots
     * cost almost nothing until actually used. Zero-filled memory
     * serves as "no mesh" sentinel since mesh_registry starts
     * generations at 1, making {0,0} always invalid. */
    state->entity_mesh_cache = (mesh_handle_t *)vm_reserve(cache_bytes);
    if (!state->entity_mesh_cache) {
        fprintf(stderr, "viewport_render: entity mesh cache vm_reserve failed\n");
        mesh_registry_destroy(&state->meshes);
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }
    state->entity_mesh_cache_cap = cache_cap;

    /* Reserve per-entity mesh type array (parallel, same capacity).
     * Zero-filled = VIEWPORT_MESH_NONE for all slots. */
    size_t type_bytes = (size_t)cache_cap * sizeof(viewport_mesh_type_t);
    state->entity_mesh_types = (viewport_mesh_type_t *)vm_reserve(type_bytes);
    if (!state->entity_mesh_types) {
        fprintf(stderr, "viewport_render: entity_mesh_types vm_reserve failed\n");
        vm_release(state->entity_mesh_cache, cache_bytes);
        mesh_registry_destroy(&state->meshes);
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }

    /* Reserve skeletal mesh pointer array (parallel, same capacity).
     * Zero-filled = NULL for all slots. */
    size_t skel_bytes = (size_t)cache_cap * sizeof(struct skeletal_mesh *);
    state->skeletal_mesh_cache = (struct skeletal_mesh **)vm_reserve(skel_bytes);
    if (!state->skeletal_mesh_cache) {
        fprintf(stderr, "viewport_render: skeletal_mesh_cache vm_reserve failed\n");
        vm_release(state->entity_mesh_types, type_bytes);
        vm_release(state->entity_mesh_cache, cache_bytes);
        mesh_registry_destroy(&state->meshes);
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }

    /* Reserve collision mesh cache (parallel, same capacity). */
    state->collision_mesh_cache = (mesh_handle_t *)vm_reserve(cache_bytes);
    if (!state->collision_mesh_cache) {
        fprintf(stderr, "viewport_render: collision mesh cache vm_reserve failed\n");
        vm_release(state->skeletal_mesh_cache, skel_bytes);
        vm_release(state->entity_mesh_types, type_bytes);
        vm_release(state->entity_mesh_cache, cache_bytes);
        mesh_registry_destroy(&state->meshes);
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }
    state->collision_mesh_cache_cap = cache_cap;

    /* Initialize snap mesh cache for surface snap raycasting. */
    snap_mesh_cache_init(&state->snap_meshes, cache_cap);

    /* Initialize decompose result cache for physics body creation. */
    snap_decompose_cache_init(&state->decompose_cache, cache_cap);

    /* Register primitive meshes (box, sphere, capsule, plane). */
    if (!viewport_render_init_primitives(state)) {
        fprintf(stderr, "viewport_render: primitive mesh init failed\n");
        vm_release(state->collision_mesh_cache, cache_bytes);
        vm_release(state->entity_mesh_cache, cache_bytes);
        mesh_registry_destroy(&state->meshes);
        vao_destroy(&state->overlay_vao);
        vbo_destroy(&state->overlay_vbo);
        vao_destroy(&state->grid_vao);
        vbo_destroy(&state->grid_vbo);
        shader_program_destroy(&state->grid_shader);
        viewport_render_destroy_extra_shaders(state);
        shader_program_destroy(&state->shader);
        render_pipeline_destroy(&state->pipeline);
        destroy_fbo(state);
        return false;
    }

    /* Initialize camera with default orbit position. */
    editor_camera_init(&state->camera);

    state->initialized = true;
    printf("viewport_render: initialized (%dx%d FBO, %dx MSAA)\n",
           w, h, state->msaa_samples);
    return true;
}

void viewport_render_destroy(viewport_render_state_t *state) {
    if (!state || !state->initialized) return;

    /* Free snap mesh cache (CPU-side geometry for surface snap). */
    snap_mesh_cache_destroy(&state->snap_meshes);

    /* Free decompose result cache. */
    snap_decompose_cache_destroy(&state->decompose_cache);

    /* Release entity mesh cache (vm_reserve'd, demand-paged). */
    if (state->entity_mesh_cache) {
        vm_release(state->entity_mesh_cache,
                   (size_t)state->entity_mesh_cache_cap * sizeof(mesh_handle_t));
    }
    state->entity_mesh_cache = NULL;
    state->entity_mesh_cache_cap = 0;

    /* Release per-entity mesh type array. */
    if (state->entity_mesh_types) {
        vm_release(state->entity_mesh_types,
                   (size_t)state->entity_mesh_cache_cap
                   * sizeof(viewport_mesh_type_t));
    }
    state->entity_mesh_types = NULL;

    /* Destroy and release skeletal mesh cache.
     * Each non-NULL entry is a heap-allocated skeletal_mesh_t. */
    if (state->skeletal_mesh_cache) {
        /* Note: individual skeletal meshes should be destroyed by
         * viewport_render_unload_entity_mesh before we get here.
         * The vm_release handles the pointer array memory. */
        vm_release(state->skeletal_mesh_cache,
                   (size_t)state->entity_mesh_cache_cap
                   * sizeof(struct skeletal_mesh *));
    }
    state->skeletal_mesh_cache = NULL;

    /* Release collision mesh cache (vm_reserve'd, demand-paged). */
    if (state->collision_mesh_cache) {
        vm_release(state->collision_mesh_cache,
                   (size_t)state->collision_mesh_cache_cap * sizeof(mesh_handle_t));
    }
    state->collision_mesh_cache = NULL;
    state->collision_mesh_cache_cap = 0;

    mesh_registry_destroy(&state->meshes);

    vao_destroy(&state->overlay_vao);
    vbo_destroy(&state->overlay_vbo);
    vao_destroy(&state->grid_vao);
    vbo_destroy(&state->grid_vbo);

    shader_program_destroy(&state->grid_shader);
    viewport_render_destroy_extra_shaders(state);
    viewport_skinning_destroy(state);
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
