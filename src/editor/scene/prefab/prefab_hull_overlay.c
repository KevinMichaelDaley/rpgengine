/**
 * @file prefab_hull_overlay.c
 * @brief Draw convex hull wireframes from the hull cache.
 *
 * Renders hull face edges as GL_LINES using the grid shader and
 * the overlay VAO/VBO on the viewport render state. Hulls are
 * drawn with depth test disabled so they remain visible.
 *
 * Non-static functions: prefab_hull_overlay_draw (1/4).
 */

#include "ferrum/editor/scene/prefab/prefab_hull_cache.h"
#include "ferrum/editor/scene/scene_viewport_render.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/vbo.h"
#include "ferrum/math/mat4.h"

#include <stddef.h>

/** Hull wireframe color: bright magenta. */
static const float HULL_R = 0.9f;
static const float HULL_G = 0.3f;
static const float HULL_B = 0.8f;

/** Maximum line vertices per frame (position + color = 6 floats each). */
#define MAX_LINE_VERTS 4096

/** Static line vertex buffer to avoid per-frame allocation.
 *  Each vertex: x, y, z, r, g, b (6 floats). */
static float s_line_verts[MAX_LINE_VERTS * 6];

void prefab_hull_overlay_draw(viewport_render_state_t *state,
                              const prefab_hull_cache_t *cache,
                              const mat4_t *view,
                              const mat4_t *proj) {
    if (!state || !cache || !view || !proj) return;
    if (!state->initialized) return;

    /* Collect all edge line segments from all valid hulls. */
    uint32_t vert_count = 0;

    for (uint32_t i = 0; i < cache->count; i++) {
        const prefab_hull_entry_t *entry = &cache->entries[i];
        if (!entry->valid) continue;

        const phys_convex_hull_t *hull = &entry->hull;

        for (uint32_t f = 0; f < hull->face_count; f++) {
            const phys_convex_face_t *face = &hull->faces[f];
            uint16_t start = face->index_start;
            uint16_t cnt = face->index_count;

            for (uint16_t e = 0; e < cnt; e++) {
                if (vert_count + 2 > MAX_LINE_VERTS) goto done_collect;

                uint16_t i0 = hull->indices[start + e];
                uint16_t i1 = hull->indices[start + ((e + 1) % cnt)];

                /* Vertex 0: position + color. */
                uint32_t off = vert_count * 6;
                s_line_verts[off + 0] = hull->vertices[i0].x;
                s_line_verts[off + 1] = hull->vertices[i0].y;
                s_line_verts[off + 2] = hull->vertices[i0].z;
                s_line_verts[off + 3] = HULL_R;
                s_line_verts[off + 4] = HULL_G;
                s_line_verts[off + 5] = HULL_B;
                vert_count++;

                /* Vertex 1: position + color. */
                off = vert_count * 6;
                s_line_verts[off + 0] = hull->vertices[i1].x;
                s_line_verts[off + 1] = hull->vertices[i1].y;
                s_line_verts[off + 2] = hull->vertices[i1].z;
                s_line_verts[off + 3] = HULL_R;
                s_line_verts[off + 4] = HULL_G;
                s_line_verts[off + 5] = HULL_B;
                vert_count++;
            }
        }
    }
done_collect:

    if (vert_count < 2) return;

    /* Use grid shader (has per-vertex color attribute). */
    mat4_t vp = mat4_mul(*proj, *view);
    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", vp.m, GL_FALSE);

    /* Upload to overlay VAO/VBO. */
    state->overlay_vao.glBindVertexArray(state->overlay_vao.handle);
    vbo_upload(&state->overlay_vbo, GL_ARRAY_BUFFER,
               s_line_verts, vert_count * 6 * sizeof(float),
               GL_DYNAMIC_DRAW);

    vao_attribute_t attrs[2] = {
        {0, 3, GL_FLOAT, GL_FALSE, 0,                 0},
        {1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0},
    };
    vao_bind_attributes(&state->overlay_vao, &state->overlay_vbo,
                        attrs, 2, 6 * sizeof(float));

    /* Draw without depth test so hulls are always visible. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDrawArrays(GL_LINES, 0, (int32_t)vert_count);
    state->glEnable(GL_DEPTH_TEST);

    state->overlay_vao.glBindVertexArray(0);
}
