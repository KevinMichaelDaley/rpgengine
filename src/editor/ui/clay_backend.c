/**
 * @file clay_backend.c
 * @brief Clay UI backend lifecycle: init, destroy, resize.
 *
 * Creates renderer resources (shader, VAO, VBO) and resolves GL function
 * pointers needed for 2D UI rendering. Uses the renderer module
 * (shader_program_t, vao_t, vbo_t) for all resource management.
 *
 * Non-static functions: 3 (init, destroy, resize).
 */

#include "ferrum/editor/ui/clay_backend.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/vao_attribute.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Load a GL function pointer from the loader into a struct field.
 *
 * Uses memcpy to avoid the ISO C pedantic warning about void* to
 * function pointer conversion (same pattern as renderer module).
 */
#define LOAD_GL_PROC(field, loader_ptr, name)                 \
    do {                                                       \
        void *raw_ = (loader_ptr)->get_proc_address(           \
            (name), (loader_ptr)->user_data);                  \
        memcpy(&(field), &raw_, sizeof(field));                \
    } while (0)

/* ---- Shader sources ---- */

/** Vertex shader: 2D quad with per-vertex color and UV. */
static const char *const UI_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec4 a_color;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "out vec4 v_color;\n"
    "out vec2 v_uv;\n"
    "uniform mat4 u_projection;\n"
    "void main() {\n"
    "    gl_Position = u_projection * vec4(a_pos, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_uv = a_uv;\n"
    "}\n";

/**
 * Fragment shader: solid color or single-channel texture (font atlas).
 * When u_use_texture != 0, samples the red channel as alpha.
 */
static const char *const UI_FRAG_SRC =
    "#version 330 core\n"
    "in vec4 v_color;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "uniform int u_use_texture;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    if (u_use_texture != 0) {\n"
    "        float a = texture(u_texture, v_uv).r;\n"
    "        frag_color = vec4(v_color.rgb, v_color.a * a);\n"
    "    } else {\n"
    "        frag_color = v_color;\n"
    "    }\n"
    "}\n";

/* ---- Vertex layout ---- */

/** Stride in bytes for the UI vertex format: 2 pos + 4 color + 2 uv. */
#define UI_VERTEX_STRIDE (8u * sizeof(float))

/**
 * @brief Resolve GL function pointers not covered by renderer wrappers.
 *
 * These are used for draw calls and state management during rendering.
 */
static bool resolve_gl_functions(clay_backend_t *b,
                                 const gl_loader_t *loader) {
    LOAD_GL_PROC(b->glDrawArrays,    loader, "glDrawArrays");
    LOAD_GL_PROC(b->glEnable,        loader, "glEnable");
    LOAD_GL_PROC(b->glDisable,       loader, "glDisable");
    LOAD_GL_PROC(b->glBlendFunc,     loader, "glBlendFunc");
    LOAD_GL_PROC(b->glScissor,       loader, "glScissor");
    LOAD_GL_PROC(b->glActiveTexture, loader, "glActiveTexture");
    LOAD_GL_PROC(b->glBindTexture,   loader, "glBindTexture");

    /* glDrawArrays is the minimum needed for rendering. */
    return b->glDrawArrays != NULL;
}

/* ---- Public API ---- */

bool clay_backend_init(clay_backend_t *backend,
                       const clay_backend_config_t *config) {
    if (backend->initialized) return false;
    memset(backend, 0, sizeof(*backend));

    backend->window_w = config->window_w;
    backend->window_h = config->window_h;
    backend->loader   = config->loader;

    /* Headless mode: no GL loader means no rendering resources. */
    if (!config->loader.get_proc_address) {
        backend->initialized = true;
        return true;
    }

    const gl_loader_t *loader = &config->loader;

    /* Resolve additional GL functions. */
    if (!resolve_gl_functions(backend, loader)) {
        fprintf(stderr, "clay_backend: failed to resolve GL functions\n");
        return false;
    }

    /* Compile and link the UI shader. */
    char log_buf[512];
    shader_program_status_t ss = shader_program_create(
        &backend->shader, loader, UI_VERT_SRC, UI_FRAG_SRC,
        log_buf, sizeof(log_buf));
    if (ss != SHADER_PROGRAM_OK) {
        fprintf(stderr, "clay_backend: shader compile error: %s\n", log_buf);
        return false;
    }

    /* Look up uniform locations. */
    shader_program_bind(&backend->shader);
    backend->u_projection = backend->shader.glGetUniformLocation(
        backend->shader.handle, "u_projection");
    backend->u_use_texture = backend->shader.glGetUniformLocation(
        backend->shader.handle, "u_use_texture");
    backend->u_texture = backend->shader.glGetUniformLocation(
        backend->shader.handle, "u_texture");

    /* Create VBO for dynamic vertex data. */
    vbo_status_t vs = vbo_create(&backend->vbo, loader);
    if (vs != VBO_OK) {
        fprintf(stderr, "clay_backend: VBO creation failed\n");
        shader_program_destroy(&backend->shader);
        return false;
    }

    /* Create VAO and bind vertex attributes. */
    vao_status_t vas = vao_create(&backend->vao, loader);
    if (vas != VAO_OK) {
        fprintf(stderr, "clay_backend: VAO creation failed\n");
        vbo_destroy(&backend->vbo);
        shader_program_destroy(&backend->shader);
        return false;
    }

    /* Vertex layout: position(2f) + color(4f) + uv(2f) = 32 bytes. */
    vao_attribute_t attrs[3] = {
        {0, 2, GL_FLOAT, GL_FALSE, 0,                  0}, /* position */
        {1, 4, GL_FLOAT, GL_FALSE, 2 * sizeof(float),  0}, /* color */
        {2, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),  0}, /* uv */
    };
    vao_bind_attributes(&backend->vao, &backend->vbo, attrs, 3,
                        UI_VERTEX_STRIDE);

    /* Initialize the font glyph atlas. */
    clay_font_set_init(&backend->fonts, loader);

    backend->initialized = true;
    return true;
}

void clay_backend_destroy(clay_backend_t *backend) {
    if (!backend->initialized) return;

    /* Only destroy GL resources if we created them. */
    if (backend->loader.get_proc_address) {
        clay_font_set_destroy(&backend->fonts);
        vao_destroy(&backend->vao);
        vbo_destroy(&backend->vbo);
        shader_program_destroy(&backend->shader);
    }

    memset(backend, 0, sizeof(*backend));
}

void clay_backend_resize(clay_backend_t *backend, int w, int h) {
    backend->window_w = w;
    backend->window_h = h;
}
