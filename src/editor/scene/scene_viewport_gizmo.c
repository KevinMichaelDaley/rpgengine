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

#include <stdio.h>
#include "ferrum/math/vec3.h"

#include <math.h>

/* ---- Constants ---- */

/** Gizmo axis length in world units (before distance scaling). */
#define GIZMO_AXIS_LENGTH 1.0f

/** Arrowhead size relative to axis length. */
#define ARROWHEAD_SIZE 0.15f

/** Arrowhead perpendicular offset. */
#define ARROWHEAD_PERP 0.06f

/** Number of line segments per rotation arc (quarter-circle). */
#define ARC_SEGMENTS 12

/** Arc angular span in radians (90 degrees = quarter circle). */
#define ARC_SPAN (3.14159265f * 0.5f)

/** Dim color multiplier for inactive axes. */
#define DIM_FACTOR 0.6f

/** Bright color multiplier for active/hovered axis. */
#define BRIGHT_FACTOR 1.0f

/** Line width for normal gizmo lines. */
#define GIZMO_LINE_WIDTH 3.0f

/** Line width for the active/hovered axis. */
#define GIZMO_ACTIVE_LINE_WIDTH 5.0f

/** Scale handle cube half-size. */
#define SCALE_CUBE_SIZE 0.12f

/* ---- Helpers ---- */

/** Target gizmo screen fraction: the gizmo axis tip will be this
 *  fraction of the viewport height away from center on screen. */
#define GIZMO_SCREEN_FRACTION 0.12f

/**
 * @brief Compute gizmo visual scale so the gizmo occupies a fixed
 *        fraction of screen height regardless of zoom and FOV.
 *
 * Uses the projection matrix to extract the effective FOV so the
 * renderer doesn't need the camera struct directly.
 */
static float compute_gizmo_scale(const vec3_t *gizmo_pos,
                                  const vec3_t *eye_pos,
                                  const mat4_t *proj) {
    float dx = gizmo_pos->x - eye_pos->x;
    float dy = gizmo_pos->y - eye_pos->y;
    float dz = gizmo_pos->z - eye_pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    /* Extract tan(fov/2) from the projection matrix:
     * proj[5] = 1 / tan(fov_y / 2)  for a standard perspective matrix. */
    float inv_tan = proj->m[5];
    float half_tan = (inv_tan > 0.001f) ? 1.0f / inv_tan : 1.0f;
    float scale = dist * half_tan * GIZMO_SCREEN_FRACTION * 2.0f;
    if (scale < 0.3f) scale = 0.3f;
    return scale;
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
    case GIZMO_AXIS_X:  *r = 1.0f * factor; *g = 0.1f * factor; *b = 0.1f * factor; break;
    case GIZMO_AXIS_Y:  *r = 0.1f * factor; *g = 1.0f * factor; *b = 0.1f * factor; break;
    case GIZMO_AXIS_Z:  *r = 0.2f * factor; *g = 0.2f * factor; *b = 1.0f * factor; break;
    case GIZMO_AXIS_XY: *r = 0.9f * factor; *g = 0.9f * factor; *b = 0.1f * factor; break;
    case GIZMO_AXIS_XZ: *r = 0.9f * factor; *g = 0.1f * factor; *b = 0.9f * factor; break;
    case GIZMO_AXIS_YZ: *r = 0.1f * factor; *g = 0.9f * factor; *b = 0.9f * factor; break;
    default:            *r = 0.5f;           *g = 0.5f;           *b = 0.5f;           break;
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

/** Plane square inner edge (fraction of axis length from gizmo center). */
#define PLANE_QUAD_MIN 0.20f
/** Plane square outer edge (fraction of axis length from gizmo center). */
#define PLANE_QUAD_MAX 0.40f
/** Normal offset for plane squares (fraction of axis length).
 *  Lifts each square off the axis plane so it remains selectable
 *  when viewed face-on (axes overlap the square). */
#define PLANE_NORMAL_OFFSET 0.08f

/**
 * @brief Push a single vertex (position + color) into the buffer.
 */
static int push_vert(float *buf, int offset,
                      float x, float y, float z,
                      float r, float g, float b) {
    int i = offset;
    buf[i++] = x; buf[i++] = y; buf[i++] = z;
    buf[i++] = r; buf[i++] = g; buf[i++] = b;
    return i;
}

/**
 * @brief Build filled plane constraint squares as triangles.
 *
 * Each square is drawn as 2 triangles (6 verts) for a solid fill,
 * plus 4 outline lines (8 verts). Colors: XY=yellow, XZ=magenta,
 * YZ=cyan. The fill uses a dimmer shade so outline edges are visible.
 *
 * @param[out] tri_count  Number of triangle vertices written.
 * @return Updated float offset (past both triangles and lines).
 */
static int build_plane_squares_(float *buf, int off, const vec3_t *pos,
                                  float scale, gizmo_axis_t active,
                                  const mat4_t *orient,
                                  int *tri_count) {
    float L = GIZMO_AXIS_LENGTH * scale;
    float lo = PLANE_QUAD_MIN * L;
    float hi = PLANE_QUAD_MAX * L;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    /* Plane pairs: (col_a, col_b, plane_axis). */
    static const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    static const gizmo_axis_t plane_ids[3] = {
        GIZMO_AXIS_XY, GIZMO_AXIS_XZ, GIZMO_AXIS_YZ
    };

    int tri_off = off;
    float noff = PLANE_NORMAL_OFFSET * L;

    /* First pass: filled triangles (dimmer color). */
    for (int p = 0; p < 3; p++) {
        vec3_t a = gizmo_axis_dir(orient, pairs[p][0]);
        vec3_t b = gizmo_axis_dir(orient, pairs[p][1]);
        /* Normal is the excluded axis — offset along it. */
        int ncol = (pairs[p][0] == 0 && pairs[p][1] == 1) ? 2
                 : (pairs[p][0] == 0 && pairs[p][1] == 2) ? 1 : 0;
        vec3_t n = gizmo_axis_dir(orient, ncol);

        float r, g, bl;
        axis_color(plane_ids[p], active, &r, &g, &bl);
        /* Dim the fill to distinguish from outline. */
        float fr = r * 0.5f, fg = g * 0.5f, fb = bl * 0.5f;

        /* Base position offset along the plane normal. */
        float ox = cx + n.x * noff;
        float oy = cy + n.y * noff;
        float oz = cz + n.z * noff;

        /* Four corners. */
        float c0x = ox + a.x * lo + b.x * lo;
        float c0y = oy + a.y * lo + b.y * lo;
        float c0z = oz + a.z * lo + b.z * lo;
        float c1x = ox + a.x * hi + b.x * lo;
        float c1y = oy + a.y * hi + b.y * lo;
        float c1z = oz + a.z * hi + b.z * lo;
        float c2x = ox + a.x * hi + b.x * hi;
        float c2y = oy + a.y * hi + b.y * hi;
        float c2z = oz + a.z * hi + b.z * hi;
        float c3x = ox + a.x * lo + b.x * hi;
        float c3y = oy + a.y * lo + b.y * hi;
        float c3z = oz + a.z * lo + b.z * hi;

        /* Triangle 1: c0-c1-c2. */
        off = push_vert(buf, off, c0x, c0y, c0z, fr, fg, fb);
        off = push_vert(buf, off, c1x, c1y, c1z, fr, fg, fb);
        off = push_vert(buf, off, c2x, c2y, c2z, fr, fg, fb);
        /* Triangle 2: c0-c2-c3. */
        off = push_vert(buf, off, c0x, c0y, c0z, fr, fg, fb);
        off = push_vert(buf, off, c2x, c2y, c2z, fr, fg, fb);
        off = push_vert(buf, off, c3x, c3y, c3z, fr, fg, fb);
    }

    *tri_count = (off - tri_off) / 6;

    /* Second pass: outline lines (full brightness, same normal offset). */
    for (int p = 0; p < 3; p++) {
        vec3_t a = gizmo_axis_dir(orient, pairs[p][0]);
        vec3_t b = gizmo_axis_dir(orient, pairs[p][1]);
        int ncol = (pairs[p][0] == 0 && pairs[p][1] == 1) ? 2
                 : (pairs[p][0] == 0 && pairs[p][1] == 2) ? 1 : 0;
        vec3_t n = gizmo_axis_dir(orient, ncol);

        float r, g, bl;
        axis_color(plane_ids[p], active, &r, &g, &bl);

        float ox = cx + n.x * noff;
        float oy = cy + n.y * noff;
        float oz = cz + n.z * noff;

        float c0x = ox + a.x * lo + b.x * lo;
        float c0y = oy + a.y * lo + b.y * lo;
        float c0z = oz + a.z * lo + b.z * lo;
        float c1x = ox + a.x * hi + b.x * lo;
        float c1y = oy + a.y * hi + b.y * lo;
        float c1z = oz + a.z * hi + b.z * lo;
        float c2x = ox + a.x * hi + b.x * hi;
        float c2y = oy + a.y * hi + b.y * hi;
        float c2z = oz + a.z * hi + b.z * hi;
        float c3x = ox + a.x * lo + b.x * hi;
        float c3y = oy + a.y * lo + b.y * hi;
        float c3z = oz + a.z * lo + b.z * hi;

        off = push_line(buf, off, c0x, c0y, c0z, c1x, c1y, c1z, r, g, bl);
        off = push_line(buf, off, c1x, c1y, c1z, c2x, c2y, c2z, r, g, bl);
        off = push_line(buf, off, c2x, c2y, c2z, c3x, c3y, c3z, r, g, bl);
        off = push_line(buf, off, c3x, c3y, c3z, c0x, c0y, c0z, r, g, bl);
    }

    return off;
}

/**
 * @brief Build translate gizmo geometry (3 axis lines + arrowheads).
 *
 * Each axis: 1 shaft line + 2 arrowhead lines = 3 lines = 6 verts.
 * Total: 3 axes * 6 verts = 18 verts.
 *
 * @return Number of vertices written.
 */
/**
 * @param[out] plane_tri_start  First vertex of plane triangle fill (-1 if none).
 * @param[out] plane_tri_count  Number of triangle vertices for plane fills.
 * @param[out] line_start       First vertex of line geometry.
 * @return Total number of vertices written.
 */
static int build_translate_verts(float *buf, const vec3_t *pos,
                                  float scale, gizmo_axis_t active,
                                  const mat4_t *orient,
                                  int *plane_tri_start,
                                  int *plane_tri_count) {
    /* Build plane fill triangles first so they're drawn behind lines. */
    int off = 0;
    int tri_count = 0;
    *plane_tri_start = 0;
    off = build_plane_squares_(buf, off, pos, scale, active, orient,
                                &tri_count);
    *plane_tri_count = tri_count;

    /* Line geometry starts after the triangles. */
    float L = GIZMO_AXIS_LENGTH * scale;
    float ah = ARROWHEAD_SIZE * scale;
    float ap = ARROWHEAD_PERP * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    gizmo_axis_t axis_ids[3] = {GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z};
    for (int i = 0; i < 3; i++) {
        vec3_t dir = gizmo_axis_dir(orient, i);
        vec3_t perp = find_perp(dir);

        float tx = cx + dir.x * L;
        float ty = cy + dir.y * L;
        float tz = cz + dir.z * L;

        float bx = cx + dir.x * (L - ah);
        float by = cy + dir.y * (L - ah);
        float bz = cz + dir.z * (L - ah);

        float r, g, b;
        axis_color(axis_ids[i], active, &r, &g, &b);

        off = push_line(buf, off, cx, cy, cz, tx, ty, tz, r, g, b);
        off = push_line(buf, off, tx, ty, tz,
                         bx + perp.x * ap, by + perp.y * ap, bz + perp.z * ap,
                         r, g, b);
        off = push_line(buf, off, tx, ty, tz,
                         bx - perp.x * ap, by - perp.y * ap, bz - perp.z * ap,
                         r, g, b);
    }

    return off / 6;
}

/**
 * @brief Build a single rotation ring into the buffer.
 *
 * @return Number of floats written (not vertices).
 */
/**
 * @brief Build a quarter-circle arc in the plane spanned by u and v.
 *
 * The arc sweeps from angle 0 to ARC_SPAN (90°), parameterized as
 * point(t) = center + (u * cos(t) + v * sin(t)) * radius.
 *
 * @param u  First tangent axis (start direction of arc).
 * @param v  Second tangent axis (90° from u in the ring plane).
 */
static int build_one_arc(float *buf, int off, const vec3_t *pos,
                           float radius, const vec3_t *u, const vec3_t *v,
                           float r, float g, float b) {
    float cx = pos->x, cy = pos->y, cz = pos->z;
    float step = ARC_SPAN / (float)ARC_SEGMENTS;

    for (int i = 0; i < ARC_SEGMENTS; ++i) {
        float a0 = (float)i * step;
        float a1 = (float)(i + 1) * step;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        off = push_line(buf, off,
            cx + (u->x * c0 + v->x * s0) * radius,
            cy + (u->y * c0 + v->y * s0) * radius,
            cz + (u->z * c0 + v->z * s0) * radius,
            cx + (u->x * c1 + v->x * s1) * radius,
            cy + (u->y * c1 + v->y * s1) * radius,
            cz + (u->z * c1 + v->z * s1) * radius,
            r, g, b);
    }

    return off;
}

/**
 * @brief Build rotate gizmo geometry (3 quarter-circle arcs).
 *
 * Each arc occupies a unique 90° sector so they don't overlap:
 *   X ring (red):   arc in the +Y/+Z quadrant
 *   Y ring (green): arc in the +Z/+X quadrant
 *   Z ring (blue):  arc in the +X/+Y quadrant
 *
 * Inactive arcs are written first, then the active arc last,
 * so they can be drawn in two passes with different line widths.
 *
 * Each arc: ARC_SEGMENTS line segments = ARC_SEGMENTS * 2 verts.
 * Total: 3 * ARC_SEGMENTS * 2 verts.
 *
 * @param[out] active_start_vert  First vertex index of the active arc
 *                                 (-1 if no active arc).
 * @return Total number of vertices written.
 */
static int build_rotate_verts(float *buf, const vec3_t *pos,
                               float scale, gizmo_axis_t active,
                               const mat4_t *orient,
                               const int8_t *arc_sign_u,
                               const int8_t *arc_sign_v,
                               int *active_start_vert) {
    int off = 0;
    float radius = GIZMO_AXIS_LENGTH * scale;

    gizmo_axis_t axis_ids[3] = {GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z};

    /* Each ring's arc uses the other two axes as u and v.
     * col_u and col_v index into the orientation matrix.
     * The arc_sign arrays flip u/v to face the camera. */
    static const int ring_uv[3][2] = {
        {1, 2},  /* X ring: u = Y, v = Z */
        {2, 0},  /* Y ring: u = Z, v = X */
        {0, 1},  /* Z ring: u = X, v = Y */
    };

    *active_start_vert = -1;

    /* First pass: inactive arcs. */
    for (int a = 0; a < 3; a++) {
        if (axis_ids[a] == active) continue;
        vec3_t u = vec3_scale(gizmo_axis_dir(orient, ring_uv[a][0]),
                               (float)arc_sign_u[a]);
        vec3_t v = vec3_scale(gizmo_axis_dir(orient, ring_uv[a][1]),
                               (float)arc_sign_v[a]);
        float r, g, b;
        axis_color(axis_ids[a], active, &r, &g, &b);
        off = build_one_arc(buf, off, pos, radius, &u, &v, r, g, b);
    }

    /* Second pass: active arc (drawn last with thicker line). */
    for (int a = 0; a < 3; a++) {
        if (axis_ids[a] != active) continue;
        *active_start_vert = off / 6;
        vec3_t u = vec3_scale(gizmo_axis_dir(orient, ring_uv[a][0]),
                               (float)arc_sign_u[a]);
        vec3_t v = vec3_scale(gizmo_axis_dir(orient, ring_uv[a][1]),
                               (float)arc_sign_v[a]);
        float r, g, b;
        axis_color(axis_ids[a], active, &r, &g, &b);
        off = build_one_arc(buf, off, pos, radius, &u, &v, r, g, b);
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
                              const mat4_t *orient,
                              int *plane_tri_start,
                              int *plane_tri_count) {
    /* Build plane fill triangles first so they're drawn behind lines. */
    int off = 0;
    int tri_count = 0;
    *plane_tri_start = 0;
    off = build_plane_squares_(buf, off, pos, scale, active, orient,
                                &tri_count);
    *plane_tri_count = tri_count;

    float L = GIZMO_AXIS_LENGTH * scale;
    float cs = SCALE_CUBE_SIZE * scale;
    float cx = pos->x, cy = pos->y, cz = pos->z;

    gizmo_axis_t axis_ids[3] = {GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z};

    for (int i = 0; i < 3; i++) {
        vec3_t dir = gizmo_axis_dir(orient, i);
        vec3_t perp = find_perp(dir);

        float tx = cx + dir.x * L;
        float ty = cy + dir.y * L;
        float tz = cz + dir.z * L;

        float r, g, b;
        axis_color(axis_ids[i], active, &r, &g, &b);

        off = push_line(buf, off, cx, cy, cz, tx, ty, tz, r, g, b);

        vec3_t perp2 = vec3_cross(dir, perp);
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
        (void)perp2;

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

    /* Extract eye position from the view matrix.  The view matrix is
     * column-major; eye = -R^T * t where R is the upper-left 3×3 and
     * t is column 3.  Equivalently:
     *   eye.x = -(row0 · col3), eye.y = -(row1 · col3), eye.z = -(row2 · col3)
     * Row i of a column-major mat4 is m[i], m[4+i], m[8+i]. */
    vec3_t eye = {
        -(view->m[0] * view->m[12] + view->m[4] * view->m[13] +
          view->m[8] * view->m[14]),
        -(view->m[1] * view->m[12] + view->m[5] * view->m[13] +
          view->m[9] * view->m[14]),
        -(view->m[2] * view->m[12] + view->m[6] * view->m[13] +
          view->m[10] * view->m[14]),
    };
    float scale = compute_gizmo_scale(&gizmo->position, &eye, proj);
    if (scale < 0.01f) scale = 0.01f;

    /* Build gizmo geometry based on current mode.
     * Max buffer: rotate = 3 arcs × 12 segs × 2 verts = 72 verts,
     * translate/scale with plane fills ≈ 90 verts.
     * Use 1500 floats to be safe. */
    float verts[1500];
    int vert_count = 0;
    int active_start = -1; /* First vertex of the active ring (rotate only). */
    int plane_tri_start = 0; /* First vertex of plane fill triangles. */
    int plane_tri_count = 0;  /* Number of triangle vertices for plane fills. */

    const mat4_t *orient = &gizmo->orientation;
    switch (gizmo->mode) {
    case GIZMO_MODE_TRANSLATE:
        vert_count = build_translate_verts(verts, &gizmo->position,
                                            scale, gizmo->active_axis, orient,
                                            &plane_tri_start, &plane_tri_count);
        break;
    case GIZMO_MODE_ROTATE:
        vert_count = build_rotate_verts(verts, &gizmo->position,
                                         scale, gizmo->active_axis, orient,
                                         gizmo->arc_sign_u, gizmo->arc_sign_v,
                                         &active_start);
        break;
    case GIZMO_MODE_SCALE:
        vert_count = build_scale_verts(verts, &gizmo->position,
                                        scale, gizmo->active_axis, orient,
                                        &plane_tri_start, &plane_tri_count);
        break;
    case GIZMO_MODE_NONE:
    default:
        return; /* No gizmo to draw. */
    }

    if (vert_count == 0) return;

    /* Use grid shader (position + color per vertex). */
    mat4_t vp = mat4_mul(*proj, *view);
    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", vp.m, GL_FALSE);

    /* Upload gizmo geometry to overlay VBO (separate from grid VBO
     * so the grid data is not clobbered). */
    state->overlay_vao.glBindVertexArray(state->overlay_vao.handle);
    vbo_upload(&state->overlay_vbo, GL_ARRAY_BUFFER, verts,
               (size_t)vert_count * 6 * sizeof(float), GL_DYNAMIC_DRAW);

    /* Bind attribute pointers within the overlay VAO. */
    vao_attribute_t attrs[2] = {
        {0, 3, GL_FLOAT, GL_FALSE, 0,                 0}, /* position */
        {1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0}, /* color */
    };
    vao_bind_attributes(&state->overlay_vao, &state->overlay_vbo, attrs, 2,
                        6 * sizeof(float));

    /* Draw without depth test or face culling so gizmo is always visible. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDisable(GL_CULL_FACE);

    /* Draw plane fill triangles first (behind the line geometry). */
    if (plane_tri_count > 0) {
        state->glDrawArrays(GL_TRIANGLES, plane_tri_start, plane_tri_count);
    }

    /* Line geometry starts after the triangle vertices. */
    int line_start = plane_tri_start + plane_tri_count;
    int line_count = vert_count - line_start;

    if (active_start >= 0 && state->glLineWidth) {
        /* Two-pass draw for rotate mode: inactive rings at normal width,
         * active ring at thicker width for visual feedback. */
        int inactive_count = active_start;
        int active_count = vert_count - active_start;

        state->glLineWidth(GIZMO_LINE_WIDTH);
        if (inactive_count > 0) {
            state->glDrawArrays(GL_LINES, 0, inactive_count);
        }

        state->glLineWidth(GIZMO_ACTIVE_LINE_WIDTH);
        if (active_count > 0) {
            state->glDrawArrays(GL_LINES, active_start, active_count);
        }

        state->glLineWidth(1.0f);
    } else if (line_count > 0) {
        /* Draw line geometry (axis lines + plane outlines). */
        if (state->glLineWidth) {
            state->glLineWidth(GIZMO_LINE_WIDTH);
        }
        state->glDrawArrays(GL_LINES, line_start, line_count);
        if (state->glLineWidth) {
            state->glLineWidth(1.0f);
        }
    }

    state->glEnable(GL_CULL_FACE);
    state->glEnable(GL_DEPTH_TEST);

    state->overlay_vao.glBindVertexArray(0);
}
