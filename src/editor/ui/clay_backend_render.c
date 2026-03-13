/**
 * @file clay_backend_render.c
 * @brief Clay UI render command dispatch.
 *
 * Iterates the Clay_RenderCommandArray and draws each command using
 * the renderer module (shader_program_t, vao_t, vbo_t) and resolved
 * GL function pointers for draw calls and state management.
 *
 * Non-static functions: 1 (clay_backend_render).
 */

#include "ferrum/editor/ui/clay_backend.h"
#include "ferrum/renderer/gl_constants.h"

#include "clay.h"

#include <string.h>

/* ---- Constants ---- */

/** Maximum characters per text render command for vertex buffer sizing. */
#define MAX_TEXT_CHARS 256
/** Floats per vertex: 2 pos + 4 color + 2 uv. */
#define FLOATS_PER_VERT 8
/** Vertices per quad (two triangles). */
#define VERTS_PER_QUAD 6

/* ---- Orthographic projection ---- */

/**
 * @brief Build a column-major 4x4 orthographic projection matrix.
 *
 * Maps pixel coordinates to clip space with top-left origin:
 * (0,0) = top-left, (w,h) = bottom-right.
 */
static void build_ortho(float *m, int w, int h) {
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / (float)w;   /* X scale */
    m[5]  = -2.0f / (float)h;   /* Y scale (flip for top-left origin) */
    m[10] = -1.0f;               /* Z scale */
    m[12] = -1.0f;               /* X translate */
    m[13] =  1.0f;               /* Y translate */
    m[15] =  1.0f;               /* W */
}

/* ---- Quad vertex helpers ---- */

/**
 * @brief Write 6 vertices (2 triangles) for a colored quad.
 *
 * @param out   Output float array (must have room for 48 floats).
 * @param x,y   Top-left corner in pixels.
 * @param w,h   Width and height in pixels.
 * @param r,g,b,a Color components (0.0-1.0).
 * @param u0,v0 Top-left UV.
 * @param u1,v1 Bottom-right UV.
 */
static void write_quad(float *out,
                       float x, float y, float w, float h,
                       float r, float g, float b, float a,
                       float u0, float v0, float u1, float v1) {
    /* Triangle 1: top-left, top-right, bottom-left */
    float *v = out;
    /* Vertex 0: top-left */
    v[0] = x;     v[1] = y;     v[2] = r; v[3] = g; v[4] = b; v[5] = a;
    v[6] = u0;    v[7] = v0;
    /* Vertex 1: top-right */
    v += FLOATS_PER_VERT;
    v[0] = x + w; v[1] = y;     v[2] = r; v[3] = g; v[4] = b; v[5] = a;
    v[6] = u1;    v[7] = v0;
    /* Vertex 2: bottom-left */
    v += FLOATS_PER_VERT;
    v[0] = x;     v[1] = y + h; v[2] = r; v[3] = g; v[4] = b; v[5] = a;
    v[6] = u0;    v[7] = v1;
    /* Triangle 2: top-right, bottom-right, bottom-left */
    /* Vertex 3: top-right */
    v += FLOATS_PER_VERT;
    v[0] = x + w; v[1] = y;     v[2] = r; v[3] = g; v[4] = b; v[5] = a;
    v[6] = u1;    v[7] = v0;
    /* Vertex 4: bottom-right */
    v += FLOATS_PER_VERT;
    v[0] = x + w; v[1] = y + h; v[2] = r; v[3] = g; v[4] = b; v[5] = a;
    v[6] = u1;    v[7] = v1;
    /* Vertex 5: bottom-left */
    v += FLOATS_PER_VERT;
    v[0] = x;     v[1] = y + h; v[2] = r; v[3] = g; v[4] = b; v[5] = a;
    v[6] = u0;    v[7] = v1;
}

/* ---- Per-command rendering ---- */

/**
 * @brief Render a solid-color rectangle.
 */
static void render_rectangle(clay_backend_t *b,
                             const Clay_RenderCommand *cmd) {
    Clay_Color c = cmd->renderData.rectangle.backgroundColor;
    float r = c.r / 255.0f, g = c.g / 255.0f;
    float bl = c.b / 255.0f, a = c.a / 255.0f;

    float verts[VERTS_PER_QUAD * FLOATS_PER_VERT];
    write_quad(verts,
               cmd->boundingBox.x, cmd->boundingBox.y,
               cmd->boundingBox.width, cmd->boundingBox.height,
               r, g, bl, a,
               0.0f, 0.0f, 0.0f, 0.0f);

    b->shader.glUniform1i(b->u_use_texture, 0);
    vbo_upload(&b->vbo, GL_ARRAY_BUFFER, verts, sizeof(verts),
               GL_DYNAMIC_DRAW);
    b->glDrawArrays(GL_TRIANGLES, 0, VERTS_PER_QUAD);
}

/**
 * @brief Render a text string using the bitmap font atlas.
 */
static void render_text(clay_backend_t *b, const Clay_RenderCommand *cmd) {
    Clay_TextRenderData text = cmd->renderData.text;
    if (text.stringContents.length <= 0) return;

    int font_id = text.fontId;
    if (font_id >= CLAY_FONT_COUNT) font_id = 0;

    /* Scale factor: requested font size vs stored base size. */
    float base_size = b->fonts.font_sizes[font_id];
    float scale = (base_size > 0.0f) ? (float)text.fontSize / base_size : 1.0f;

    /* Color in 0-1 range. */
    float r  = text.textColor.r / 255.0f;
    float g  = text.textColor.g / 255.0f;
    float bl = text.textColor.b / 255.0f;
    float a  = text.textColor.a / 255.0f;

    /* Bind font atlas texture. */
    b->shader.glUniform1i(b->u_use_texture, 1);
    b->shader.glUniform1i(b->u_texture, 0);
    b->glActiveTexture(GL_TEXTURE0);
    b->glBindTexture(GL_TEXTURE_2D, b->fonts.atlas_texture);

    /* Build vertex data for all characters. */
    int len = text.stringContents.length;
    if (len > MAX_TEXT_CHARS) len = MAX_TEXT_CHARS;

    float verts[MAX_TEXT_CHARS * VERTS_PER_QUAD * FLOATS_PER_VERT];
    int vert_count = 0;
    float cursor_x = cmd->boundingBox.x;
    float baseline_y = cmd->boundingBox.y;

    for (int i = 0; i < len; i++) {
        char ch = text.stringContents.chars[i];
        if (ch < 32 || ch >= 127) {
            /* Unprintable: advance by a space-width. */
            cursor_x += b->fonts.glyphs[font_id][' '].advance_x * scale;
            continue;
        }

        const clay_glyph_t *gl = &b->fonts.glyphs[font_id][(int)ch];
        float gw = gl->width * scale;
        float gh = gl->height * scale;
        float gx = cursor_x + gl->bearing_x * scale;
        /* Top-left origin: baseline_y is the top of the text box. */
        float gy = baseline_y;

        float u0 = gl->uv_x;
        float v0 = gl->uv_y;
        float u1 = gl->uv_x + gl->uv_w;
        float v1 = gl->uv_y + gl->uv_h;

        write_quad(&verts[vert_count * FLOATS_PER_VERT],
                   gx, gy, gw, gh,
                   r, g, bl, a,
                   u0, v0, u1, v1);
        vert_count += VERTS_PER_QUAD;

        cursor_x += gl->advance_x * scale + (float)text.letterSpacing;
    }

    if (vert_count > 0) {
        vbo_upload(&b->vbo, GL_ARRAY_BUFFER, verts,
                   (size_t)(vert_count * FLOATS_PER_VERT) * sizeof(float),
                   GL_DYNAMIC_DRAW);
        b->glDrawArrays(GL_TRIANGLES, 0, vert_count);
    }
}

/**
 * @brief Render border lines around a bounding box.
 *
 * Draws up to 4 thin rectangles (top, bottom, left, right) based on
 * the border widths specified in the render command.
 */
static void render_border(clay_backend_t *b, const Clay_RenderCommand *cmd) {
    Clay_BorderRenderData border = cmd->renderData.border;
    float x = cmd->boundingBox.x;
    float y = cmd->boundingBox.y;
    float w = cmd->boundingBox.width;
    float h = cmd->boundingBox.height;

    float r  = border.color.r / 255.0f;
    float g  = border.color.g / 255.0f;
    float bl = border.color.b / 255.0f;
    float a  = border.color.a / 255.0f;

    b->shader.glUniform1i(b->u_use_texture, 0);

    /* Draw each border side as a thin rectangle. */
    /* Maximum 4 sides × 6 verts × 8 floats = 192 floats. */
    float verts[4 * VERTS_PER_QUAD * FLOATS_PER_VERT];
    int quad_count = 0;

    /* Top border */
    if (border.width.top > 0) {
        write_quad(&verts[quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT],
                   x, y, w, (float)border.width.top,
                   r, g, bl, a, 0, 0, 0, 0);
        quad_count++;
    }
    /* Bottom border */
    if (border.width.bottom > 0) {
        write_quad(&verts[quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT],
                   x, y + h - (float)border.width.bottom,
                   w, (float)border.width.bottom,
                   r, g, bl, a, 0, 0, 0, 0);
        quad_count++;
    }
    /* Left border */
    if (border.width.left > 0) {
        write_quad(&verts[quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT],
                   x, y, (float)border.width.left, h,
                   r, g, bl, a, 0, 0, 0, 0);
        quad_count++;
    }
    /* Right border */
    if (border.width.right > 0) {
        write_quad(&verts[quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT],
                   x + w - (float)border.width.right, y,
                   (float)border.width.right, h,
                   r, g, bl, a, 0, 0, 0, 0);
        quad_count++;
    }

    if (quad_count > 0) {
        int total_verts = quad_count * VERTS_PER_QUAD;
        vbo_upload(&b->vbo, GL_ARRAY_BUFFER, verts,
                   (size_t)(total_verts * FLOATS_PER_VERT) * sizeof(float),
                   GL_DYNAMIC_DRAW);
        b->glDrawArrays(GL_TRIANGLES, 0, total_verts);
    }
}

/**
 * @brief Begin scissor clipping to a bounding box.
 *
 * Converts from Clay's top-left origin to OpenGL's bottom-left origin.
 */
static void render_scissor_start(clay_backend_t *b,
                                 const Clay_RenderCommand *cmd) {
    b->glEnable(GL_SCISSOR_TEST);
    int sx = (int)cmd->boundingBox.x;
    int sy = b->window_h - (int)cmd->boundingBox.y
             - (int)cmd->boundingBox.height;
    int sw = (int)cmd->boundingBox.width;
    int sh = (int)cmd->boundingBox.height;
    b->glScissor(sx, sy, sw, sh);
}

/**
 * @brief End scissor clipping.
 */
static void render_scissor_end(clay_backend_t *b) {
    b->glDisable(GL_SCISSOR_TEST);
}

/**
 * @brief Render a textured image quad.
 *
 * If imageData is a uint32_t texture handle pointer, binds and draws it.
 */
static void render_image(clay_backend_t *b, const Clay_RenderCommand *cmd) {
    Clay_ImageRenderData img = cmd->renderData.image;
    if (!img.imageData) return;

    /* Interpret imageData as a pointer to a GL texture handle. */
    uint32_t tex_handle = *(uint32_t *)img.imageData;
    if (tex_handle == 0) return;

    float r = 1.0f, g = 1.0f, bl = 1.0f, a = 1.0f;
    /* Apply tint if provided (non-zero alpha). */
    if (img.backgroundColor.a > 0.0f) {
        r  = img.backgroundColor.r / 255.0f;
        g  = img.backgroundColor.g / 255.0f;
        bl = img.backgroundColor.b / 255.0f;
        a  = img.backgroundColor.a / 255.0f;
    }

    b->shader.glUniform1i(b->u_use_texture, 1);
    b->shader.glUniform1i(b->u_texture, 0);
    b->glActiveTexture(GL_TEXTURE0);
    b->glBindTexture(GL_TEXTURE_2D, tex_handle);

    float verts[VERTS_PER_QUAD * FLOATS_PER_VERT];
    write_quad(verts,
               cmd->boundingBox.x, cmd->boundingBox.y,
               cmd->boundingBox.width, cmd->boundingBox.height,
               r, g, bl, a,
               0.0f, 0.0f, 1.0f, 1.0f);

    vbo_upload(&b->vbo, GL_ARRAY_BUFFER, verts, sizeof(verts),
               GL_DYNAMIC_DRAW);
    b->glDrawArrays(GL_TRIANGLES, 0, VERTS_PER_QUAD);
}

/* ---- Public API ---- */

void clay_backend_render(clay_backend_t *backend,
                         struct Clay_RenderCommandArray cmds) {
    if (!backend->initialized) return;
    if (!backend->glDrawArrays) return; /* headless mode */

    Clay_RenderCommandArray *arr = &cmds;
    if (arr->length <= 0) return;

    /* Set up 2D rendering state. */
    backend->glEnable(GL_BLEND);
    backend->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    backend->glDisable(GL_DEPTH_TEST);

    /* Bind the UI shader and set the projection matrix. */
    shader_program_bind(&backend->shader);
    float proj[16];
    build_ortho(proj, backend->window_w, backend->window_h);
    backend->shader.glUniformMatrix4fv(backend->u_projection, 1,
                                       GL_FALSE, proj);

    /* Bind the VAO — stays bound for the entire render pass. */
    backend->vao.glBindVertexArray(backend->vao.handle);

    /* Dispatch each render command. */
    for (int32_t i = 0; i < arr->length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(arr, i);
        if (!cmd) continue;

        switch (cmd->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
            render_rectangle(backend, cmd);
            break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
            render_text(backend, cmd);
            break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
            render_border(backend, cmd);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            render_scissor_start(backend, cmd);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            render_scissor_end(backend);
            break;
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            render_image(backend, cmd);
            break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            /* Custom rendering: user-defined callback via userData. */
            break;
        default:
            break;
        }
    }

    /* Restore GL state. */
    backend->vao.glBindVertexArray(0);
    backend->glDisable(GL_SCISSOR_TEST);
    backend->glDisable(GL_BLEND);
}
