/**
 * @file scene_viewport_gizmo.c
 * @brief Viewport gizmo rendering: translate/rotate/scale handles.
 *
 * Draws transform gizmo axes at the selection center using the grid
 * shader (position + color per vertex, GL_LINES). The gizmo is scaled
 * by camera distance for constant screen size. Active/hovered axis
 * is drawn brighter.
 *
 * Non-static functions (1 / 4 limit):
 *   viewport_render_draw_gizmo
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/editor/edit_selection.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <math.h>

/* ---- Constants ---- */

/** Gizmo axis length in world units (before distance scaling). */
#define GIZMO_AXIS_LENGTH 1.0f

/** Arrowhead size relative to axis length. */
#define ARROWHEAD_SIZE 0.15f

/** Arrowhead perpendicular offset. */
#define ARROWHEAD_PERP 0.06f

/** Number of segments for rotation ring. */
#define RING_SEGMENTS 32

/** Dim color multiplier for inactive axes. */
#define DIM_FACTOR 0.8f

/** Bright color multiplier for active/hovered axis. */
#define BRIGHT_FACTOR 1.0f

/** Scale handle cube half-size. */
#define SCALE_CUBE_SIZE 0.06f

/* ---- Helpers ---- */

/**
 * @brief Compute gizmo visual scale based on camera distance.
 *
 * Keeps gizmo roughly constant screen size regardless of zoom.
 */
static float compute_gizmo_scale(const vec3_t *gizmo_pos,
                                  const vec3_t *eye_pos) {
    float dx = gizmo_pos->x - eye_pos->x;
    float dy = gizmo_pos->y - eye_pos->y;
    float dz = gizmo_pos->z - eye_pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    /* Scale factor: 0.15 units per unit of distance. */
    return dist * 0.15f;
}

/**
 * @brief Push a line segment (2 vertices, 6 floats each) into the buffer.
 */
static int push_line(float *buf, int offset,
                     float x0, float y0, float z0,
                     float x1, float y1, float z1,
                     float r, float g, float b) {
    int i = offset;
    buf[i++] = x0; buf[i++] = y0; buf[i++] = z0;
    buf[i++] = r;  buf[i++] = g;  buf[i++] = b;
    buf[i++] = x1; buf[i++] = y1; buf[i++] = z1;
    buf[i++] = r;  buf[i++] = g;  buf[i++] = b;
    return i;
}

/**
 * @brief Get axis color with brightness based on hover state.
 */
static void axis_color(gizmo_axis_t axis, gizmo_axis_t active,
                        float *r, float *g, float *b) {
    float factor = (axis == active) ? BRIGHT_FACTOR : DIM_FACTOR;
    switch (axis) {
    case GIZMO_AXIS_X: *r = 1.0f * factor; *g = 0.1f * factor; *b = 0.1f * factor; break;
    case GIZMO_AXIS_Y: *r = 0.1f * factor; *g = 1.0f * factor; *b = 0.1f * factor; break;
    case GIZMO_AXIS_Z: *r = 0.2f * factor; *g = 0.2f * factor; *b = 1.0f * factor; break;
    default:           *r = 0.5f;           *g = 0.5f;           *b = 0.5f;           break;
    }
}

/**
 * @brief Extract oriented axis direction from gizmo orientation matrix.
 *
 * Column 0 = oriented X, column 1 = oriented Y, column 2 = oriented Z.
 */
static vec3_t gizmo_axis_dir(const mat4_t *orient, int col) {
    return (vec3_t){orient->m[col * 4 + 0],
                    orient->m[col * 4 + 1],
                    orient->m[col * 4 + 2]};
}

/**
 * @brief Find a vector perpendicular to the given axis direction.
 *
 * Used for arrowhead and scale-cube offsets.
 */
static vec3_t find_perp(vec3_t axis) {
    /* Cross with whichever world axis is least parallel. */
    vec3_t up = (fabsf(axis.y) < 0.9f)
        ? (vec3_t){0.0f, 1.0f, 0.0f}
        : (vec3_t){1.0f, 0.0f, 0.0f};
    vec3_t perp = vec3_cross(axis, up);
    return vec3_normalize_safe(perp, 1e-8f);
}

/**
 * @brief Build translate gizmo geometry (3 axis lines + arrowheads).
 *
 * Each axis: 1 shaft line + 2 arrowhead lines = 3 lines = 6 verts.
 * Total: 3 axes * 6 verts = 18 verts.
 *
 * @return Number of vertices written.
 */
static int build_translate_verts(float *buf, const vec3_t *pos,
                                  float scale, gizmo_axis_t active,
                                  const mat4_t *orient) {
    int off = 0;
    float L = GIZMO_AXIS_LENGTH * scale;
    float ah = ARROWHEAD_SIZE * scale;
    float ap = ARROWHEAD_PERP * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    gizmo_axis_t axis_ids[3] = {GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z};
    for (int i = 0; i < 3; i++) {
        vec3_t dir = gizmo_axis_dir(orient, i);
        vec3_t perp = find_perp(dir);

        /* Tip position. */
        float tx = cx + dir.x * L;
        float ty = cy + dir.y * L;
        float tz = cz + dir.z * L;

        /* Arrowhead base position. */
        float bx = cx + dir.x * (L - ah);
        float by = cy + dir.y * (L - ah);
        float bz = cz + dir.z * (L - ah);

        float r, g, b;
        axis_color(axis_ids[i], active, &r, &g, &b);

        /* Shaft line. */
        off = push_line(buf, off, cx, cy, cz, tx, ty, tz, r, g, b);
        /* Arrowhead lines. */
        off = push_line(buf, off, tx, ty, tz,
                         bx + perp.x * ap, by + perp.y * ap, bz + perp.z * ap,
                         r, g, b);
        off = push_line(buf, off, tx, ty, tz,
                         bx - perp.x * ap, by - perp.y * ap, bz - perp.z * ap,
                         r, g, b);
    }

    return off / 6; /* 6 floats per vertex */
}

/**
 * @brief Build rotate gizmo geometry (3 axis rings).
 *
 * Each ring: RING_SEGMENTS line segments = RING_SEGMENTS * 2 verts.
 * Total: 3 * RING_SEGMENTS * 2 verts.
 *
 * @return Number of vertices written.
 */
static int build_rotate_verts(float *buf, const vec3_t *pos,
                               float scale, gizmo_axis_t active,
                               const mat4_t *orient) {
    int off = 0;
    float radius = GIZMO_AXIS_LENGTH * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;
    float step = 2.0f * 3.14159265f / (float)RING_SEGMENTS;

    gizmo_axis_t axis_ids[3] = {GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z};

    for (int a = 0; a < 3; a++) {
        /* Ring normal is the oriented axis. Build two perpendicular
         * vectors in the ring plane using the other two axes. */
        vec3_t normal = gizmo_axis_dir(orient, a);
        vec3_t u = find_perp(normal);
        vec3_t v = vec3_cross(normal, u);

        float r, g, b;
        axis_color(axis_ids[a], active, &r, &g, &b);

        for (int i = 0; i < RING_SEGMENTS; ++i) {
            float a0 = (float)i * step;
            float a1 = (float)(i + 1) * step;
            float c0 = cosf(a0), s0 = sinf(a0);
            float c1 = cosf(a1), s1 = sinf(a1);
            off = push_line(buf, off,
                cx + (u.x * c0 + v.x * s0) * radius,
                cy + (u.y * c0 + v.y * s0) * radius,
                cz + (u.z * c0 + v.z * s0) * radius,
                cx + (u.x * c1 + v.x * s1) * radius,
                cy + (u.y * c1 + v.y * s1) * radius,
                cz + (u.z * c1 + v.z * s1) * radius,
                r, g, b);
        }
    }

    return off / 6;
}

/**
 * @brief Build scale gizmo geometry (3 axis lines + cube markers).
 *
 * Each axis: 1 shaft line + 4 cube-outline lines = 5 lines = 10 verts.
 * Total: 3 * 10 = 30 verts.
 *
 * @return Number of vertices written.
 */
static int build_scale_verts(float *buf, const vec3_t *pos,
                              float scale, gizmo_axis_t active,
                              const mat4_t *orient) {
    int off = 0;
    float L = GIZMO_AXIS_LENGTH * scale;
    float cs = SCALE_CUBE_SIZE * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    gizmo_axis_t axis_ids[3] = {GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z};

    for (int i = 0; i < 3; i++) {
        vec3_t dir = gizmo_axis_dir(orient, i);
        vec3_t perp = find_perp(dir);

        /* Tip position. */
        float tx = cx + dir.x * L;
        float ty = cy + dir.y * L;
        float tz = cz + dir.z * L;

        float r, g, b;
        axis_color(axis_ids[i], active, &r, &g, &b);

        /* Shaft line. */
        off = push_line(buf, off, cx, cy, cz, tx, ty, tz, r, g, b);

        /* Cube outline (4 lines forming a square at the tip,
         * in the plane perpendicular to the axis). */
        vec3_t perp2 = vec3_cross(dir, perp);
        /* Four corners: tip ± cs*perp ± cs*dir */
        float c1x = tx - dir.x * cs + perp.x * cs;
        float c1y = ty - dir.y * cs + perp.y * cs;
        float c1z = tz - dir.z * cs + perp.z * cs;
        float c2x = tx + dir.x * cs + perp.x * cs;
        float c2y = ty + dir.y * cs + perp.y * cs;
        float c2z = tz + dir.z * cs + perp.z * cs;
        float c3x = tx + dir.x * cs - perp.x * cs;
        float c3y = ty + dir.y * cs - perp.y * cs;
        float c3z = tz + dir.z * cs - perp.z * cs;
        float c4x = tx - dir.x * cs - perp.x * cs;
        float c4y = ty - dir.y * cs - perp.y * cs;
        float c4z = tz - dir.z * cs - perp.z * cs;
        (void)perp2; /* Cube uses axis+perp plane for simplicity. */

        off = push_line(buf, off, c1x, c1y, c1z, c2x, c2y, c2z, r, g, b);
        off = push_line(buf, off, c2x, c2y, c2z, c3x, c3y, c3z, r, g, b);
        off = push_line(buf, off, c3x, c3y, c3z, c4x, c4y, c4z, r, g, b);
        off = push_line(buf, off, c4x, c4y, c4z, c1x, c1y, c1z, r, g, b);
    }

    return off / 6;
}

/* ---- Public API ---- */

void viewport_render_draw_gizmo(viewport_render_state_t *state,
                                  const gizmo_state_t *gizmo,
                                  const edit_selection_t *selection,
                                  const mat4_t *view,
                                  const mat4_t *proj) {
    if (!state || !state->initialized || !gizmo || !selection) return;
    if (edit_selection_count(selection) == 0) return;

    /* Compute eye position for distance-based scaling. */
    vec3_t eye = editor_camera_eye_position(&state->camera);
    float scale = compute_gizmo_scale(&gizmo->position, &eye);
    if (scale < 0.01f) scale = 0.01f;

    /* Build gizmo geometry based on current mode.
     * Max buffer: rotate mode = 3 * 32 * 2 = 192 verts * 6 floats = 1152.
     * Use 1200 to be safe. */
    float verts[1200];
    int vert_count = 0;

    const mat4_t *orient = &gizmo->orientation;
    switch (gizmo->mode) {
    case GIZMO_MODE_TRANSLATE:
        vert_count = build_translate_verts(verts, &gizmo->position,
                                            scale, gizmo->active_axis, orient);
        break;
    case GIZMO_MODE_ROTATE:
        vert_count = build_rotate_verts(verts, &gizmo->position,
                                         scale, gizmo->active_axis, orient);
        break;
    case GIZMO_MODE_SCALE:
        vert_count = build_scale_verts(verts, &gizmo->position,
                                        scale, gizmo->active_axis, orient);
        break;
    }

    if (vert_count == 0) return;

    /* Use grid shader (position + color per vertex). */
    mat4_t vp = mat4_mul(*proj, *view);
    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", vp.m, GL_FALSE);

    /* Upload gizmo geometry to grid VBO and re-bind vertex attributes.
     * Some drivers invalidate attribute pointers after glBufferData
     * changes the buffer size. Re-set the pointers to be safe. */
    state->grid_vao.glBindVertexArray(state->grid_vao.handle);
    vbo_upload(&state->grid_vbo, GL_ARRAY_BUFFER, verts,
               (size_t)vert_count * 6 * sizeof(float), GL_DYNAMIC_DRAW);

    /* Re-bind attribute pointers within the VAO. */
    vao_attribute_t attrs[2] = {
        {0, 3, GL_FLOAT, GL_FALSE, 0,                 0}, /* position */
        {1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0}, /* color */
    };
    vao_bind_attributes(&state->grid_vao, &state->grid_vbo, attrs, 2,
                        6 * sizeof(float));

    /* Draw without depth test or face culling so gizmo is always visible. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDisable(GL_CULL_FACE);
    state->glDrawArrays(GL_LINES, 0, vert_count);
    state->glEnable(GL_CULL_FACE);
    state->glEnable(GL_DEPTH_TEST);

    state->grid_vao.glBindVertexArray(0);
}
