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
#define DIM_FACTOR 0.5f

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
    case GIZMO_AXIS_X: *r = 1.0f * factor; *g = 0.2f * factor; *b = 0.2f * factor; break;
    case GIZMO_AXIS_Y: *r = 0.2f * factor; *g = 1.0f * factor; *b = 0.2f * factor; break;
    case GIZMO_AXIS_Z: *r = 0.3f * factor; *g = 0.3f * factor; *b = 1.0f * factor; break;
    default:           *r = 0.5f;           *g = 0.5f;           *b = 0.5f;           break;
    }
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
                                  float scale, gizmo_axis_t active) {
    int off = 0;
    float L = GIZMO_AXIS_LENGTH * scale;
    float ah = ARROWHEAD_SIZE * scale;
    float ap = ARROWHEAD_PERP * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    /* X axis: shaft + arrowhead. */
    float r, g, b;
    axis_color(GIZMO_AXIS_X, active, &r, &g, &b);
    off = push_line(buf, off, cx, cy, cz, cx + L, cy, cz, r, g, b);
    off = push_line(buf, off, cx + L, cy, cz, cx + L - ah, cy + ap, cz, r, g, b);
    off = push_line(buf, off, cx + L, cy, cz, cx + L - ah, cy - ap, cz, r, g, b);

    /* Y axis: shaft + arrowhead. */
    axis_color(GIZMO_AXIS_Y, active, &r, &g, &b);
    off = push_line(buf, off, cx, cy, cz, cx, cy + L, cz, r, g, b);
    off = push_line(buf, off, cx, cy + L, cz, cx + ap, cy + L - ah, cz, r, g, b);
    off = push_line(buf, off, cx, cy + L, cz, cx - ap, cy + L - ah, cz, r, g, b);

    /* Z axis: shaft + arrowhead. */
    axis_color(GIZMO_AXIS_Z, active, &r, &g, &b);
    off = push_line(buf, off, cx, cy, cz, cx, cy, cz + L, r, g, b);
    off = push_line(buf, off, cx, cy, cz + L, cx + ap, cy, cz + L - ah, r, g, b);
    off = push_line(buf, off, cx, cy, cz + L, cx - ap, cy, cz + L - ah, r, g, b);

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
                               float scale, gizmo_axis_t active) {
    int off = 0;
    float radius = GIZMO_AXIS_LENGTH * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;
    float step = 2.0f * 3.14159265f / (float)RING_SEGMENTS;

    /* X-axis ring (in YZ plane). */
    float r, g, b;
    axis_color(GIZMO_AXIS_X, active, &r, &g, &b);
    for (int i = 0; i < RING_SEGMENTS; ++i) {
        float a0 = (float)i * step;
        float a1 = (float)(i + 1) * step;
        off = push_line(buf, off,
                         cx, cy + cosf(a0) * radius, cz + sinf(a0) * radius,
                         cx, cy + cosf(a1) * radius, cz + sinf(a1) * radius,
                         r, g, b);
    }

    /* Y-axis ring (in XZ plane). */
    axis_color(GIZMO_AXIS_Y, active, &r, &g, &b);
    for (int i = 0; i < RING_SEGMENTS; ++i) {
        float a0 = (float)i * step;
        float a1 = (float)(i + 1) * step;
        off = push_line(buf, off,
                         cx + cosf(a0) * radius, cy, cz + sinf(a0) * radius,
                         cx + cosf(a1) * radius, cy, cz + sinf(a1) * radius,
                         r, g, b);
    }

    /* Z-axis ring (in XY plane). */
    axis_color(GIZMO_AXIS_Z, active, &r, &g, &b);
    for (int i = 0; i < RING_SEGMENTS; ++i) {
        float a0 = (float)i * step;
        float a1 = (float)(i + 1) * step;
        off = push_line(buf, off,
                         cx + cosf(a0) * radius, cy + sinf(a0) * radius, cz,
                         cx + cosf(a1) * radius, cy + sinf(a1) * radius, cz,
                         r, g, b);
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
                              float scale, gizmo_axis_t active) {
    int off = 0;
    float L = GIZMO_AXIS_LENGTH * scale;
    float cs = SCALE_CUBE_SIZE * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    /* X axis: shaft + cube at tip. */
    float r, g, b;
    axis_color(GIZMO_AXIS_X, active, &r, &g, &b);
    off = push_line(buf, off, cx, cy, cz, cx + L, cy, cz, r, g, b);
    /* Cube outline (4 lines forming a diamond). */
    off = push_line(buf, off, cx + L - cs, cy - cs, cz, cx + L + cs, cy - cs, cz, r, g, b);
    off = push_line(buf, off, cx + L + cs, cy - cs, cz, cx + L + cs, cy + cs, cz, r, g, b);
    off = push_line(buf, off, cx + L + cs, cy + cs, cz, cx + L - cs, cy + cs, cz, r, g, b);
    off = push_line(buf, off, cx + L - cs, cy + cs, cz, cx + L - cs, cy - cs, cz, r, g, b);

    /* Y axis: shaft + cube at tip. */
    axis_color(GIZMO_AXIS_Y, active, &r, &g, &b);
    off = push_line(buf, off, cx, cy, cz, cx, cy + L, cz, r, g, b);
    off = push_line(buf, off, cx - cs, cy + L - cs, cz, cx + cs, cy + L - cs, cz, r, g, b);
    off = push_line(buf, off, cx + cs, cy + L - cs, cz, cx + cs, cy + L + cs, cz, r, g, b);
    off = push_line(buf, off, cx + cs, cy + L + cs, cz, cx - cs, cy + L + cs, cz, r, g, b);
    off = push_line(buf, off, cx - cs, cy + L + cs, cz, cx - cs, cy + L - cs, cz, r, g, b);

    /* Z axis: shaft + cube at tip. */
    axis_color(GIZMO_AXIS_Z, active, &r, &g, &b);
    off = push_line(buf, off, cx, cy, cz, cx, cy, cz + L, r, g, b);
    off = push_line(buf, off, cx - cs, cy, cz + L - cs, cx + cs, cy, cz + L - cs, r, g, b);
    off = push_line(buf, off, cx + cs, cy, cz + L - cs, cx + cs, cy, cz + L + cs, r, g, b);
    off = push_line(buf, off, cx + cs, cy, cz + L + cs, cx - cs, cy, cz + L + cs, r, g, b);
    off = push_line(buf, off, cx - cs, cy, cz + L + cs, cx - cs, cy, cz + L - cs, r, g, b);

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

    switch (gizmo->mode) {
    case GIZMO_MODE_TRANSLATE:
        vert_count = build_translate_verts(verts, &gizmo->position,
                                            scale, gizmo->active_axis);
        break;
    case GIZMO_MODE_ROTATE:
        vert_count = build_rotate_verts(verts, &gizmo->position,
                                         scale, gizmo->active_axis);
        break;
    case GIZMO_MODE_SCALE:
        vert_count = build_scale_verts(verts, &gizmo->position,
                                        scale, gizmo->active_axis);
        break;
    }

    if (vert_count == 0) return;

    /* Use grid shader (position + color per vertex). */
    mat4_t vp = mat4_mul(*proj, *view);
    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", vp.m, GL_FALSE);

    /* Upload gizmo geometry to grid VBO. */
    state->grid_vao.glBindVertexArray(state->grid_vao.handle);
    vbo_upload(&state->grid_vbo, GL_ARRAY_BUFFER, verts,
               (size_t)vert_count * 6 * sizeof(float), GL_DYNAMIC_DRAW);

    /* Draw without depth test so gizmo is always visible. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDrawArrays(GL_LINES, 0, vert_count);
    state->glEnable(GL_DEPTH_TEST);

    state->grid_vao.glBindVertexArray(0);
}
