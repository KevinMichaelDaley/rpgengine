/**
 * @file grammar_blockout.c
 * @brief Blockout grammar rasterizer: token stream → fr_dungeon_layout_t.
 */

#include "ferrum/procgen/grammar_blockout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Dynamic arrays ────────────────────────────────────────────── */

#define DEF_ARRAY(name, T)                              \
    typedef struct {                                    \
        T      *data;                                   \
        uint32_t count;                                 \
        uint32_t cap;                                   \
    } name;                                             \
    static int name##_init(name *a, uint32_t cap) {      \
        a->data = malloc((size_t)cap * sizeof(T));       \
        if (!a->data) return -1;                         \
        a->count = 0;                                   \
        a->cap   = cap;                                  \
        return 0;                                        \
    }                                                    \
    static int name##_push(name *a, const T *v) {         \
        if (a->count >= a->cap) {                        \
            uint32_t nc = a->cap * 2;                    \
            T *nd = realloc(a->data, (size_t)nc * sizeof(T)); \
            if (!nd) return -1;                          \
            a->data = nd;                                \
            a->cap  = nc;                                \
        }                                                \
        a->data[a->count++] = *v;                        \
        return 0;                                        \
    }

DEF_ARRAY(room_arr,     fr_room_def_t);
DEF_ARRAY(corridor_arr, fr_corridor_def_t);
DEF_ARRAY(opening_arr,  fr_opening_def_t);
DEF_ARRAY(ramp_arr,     fr_ramp_def_t);
DEF_ARRAY(marker_arr,   fr_marker_def_t);

static void rooms_free(room_arr *a)      { free(a->data); a->data = NULL; a->count = 0; a->cap = 0; }
static void corridors_free(corridor_arr *a) { free(a->data); a->data = NULL; a->count = 0; a->cap = 0; }
static void openings_free(opening_arr *a)  { free(a->data); a->data = NULL; a->count = 0; a->cap = 0; }
static void ramps_free(ramp_arr *a)       { free(a->data); a->data = NULL; a->count = 0; a->cap = 0; }
static void markers_free(marker_arr *a)   { free(a->data); a->data = NULL; a->count = 0; a->cap = 0; }

/* ── Parameter lookups ─────────────────────────────────────────── */

static int find_param_float(const procgen_token_t *tokens, uint32_t count,
                            uint32_t start, const char *name, float *out) {
    for (uint32_t i = start; i < count; i++) {
        /* Stop at next keyword (not a param). */
        if (tokens[i].type >= TOK_GRAMMAR && tokens[i].type <= TOK_EBLOCK
            && tokens[i].type != TOK_MARKER) {
            break;
        }
        if (strcmp(tokens[i].param_name, name) == 0) {
            *out = tokens[i].value.f;
            return 0;
        }
    }
    return -1;
}

static int find_param_string(const procgen_token_t *tokens, uint32_t count,
                             uint32_t start, const char *name,
                             char *out, uint32_t out_cap) {
    for (uint32_t i = start; i < count; i++) {
        if (tokens[i].type >= TOK_GRAMMAR && tokens[i].type <= TOK_EBLOCK
            && tokens[i].type != TOK_MARKER) {
            break;
        }
        if (strcmp(tokens[i].param_name, name) == 0) {
            uint32_t clen = out_cap - 1;
            uint32_t slen = (uint32_t)strlen(tokens[i].value.s);
            memcpy(out, tokens[i].value.s, slen < clen ? slen : clen);
            out[slen < clen ? slen : clen] = '\0';
            return 0;
        }
    }
    return -1;
}

/* ── Coordinate tuple parser ──────────────────────────────────── */

/**
 * @brief Parse a coordinate tuple string like "(20,0)" or "(20,0,5)"
 *        or "((x1,y1),(x2,y2),...)" into floats.
 *
 * Stores parsed floats in *out and returns count (2, 3, or 2*N).
 * Returns -1 on parse error.
 */
static int parse_tuple(const char *tuple, float *out, uint32_t out_cap) {
    if (!tuple || !out || tuple[0] != '(') return -1;

    const char *p = tuple + 1;  /* skip opening ( */
    uint32_t count = 0;

    while (*p && *p != ')' && count < out_cap) {
        /* Skip whitespace and commas. */
        while (*p == ' ' || *p == ',' || *p == '\t') p++;

        if (*p == '(') {
            /* Nested tuple: ((x1,y1),(x2,y2),...).
               Recursively parse each inner pair. */
            p++; /* skip inner ( */
            for (int j = 0; j < 2 && count < out_cap; j++) {
                while (*p == ' ' || *p == ',' || *p == '\t') p++;
                char *end = NULL;
                out[count++] = strtof(p, &end);
                if (end == p) return -1;
                p = end;
            }
            /* skip closing ) of inner pair */
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ')') p++;
        } else if (*p == ')' || *p == '\0') {
            break;
        } else {
            /* Plain number. */
            char *end = NULL;
            out[count++] = strtof(p, &end);
            if (end == p) return -1;
            p = end;
        }
    }

    return (int)count;
}

/**
 * @brief Find a coordinate-tuple parameter and parse it.
 *
 * Searches forward from @p start for a param token with the given
 * name whose value is a tuple string. Parses it into floats.
 *
 * @return number of floats parsed (≥0), or -1 if param not found.
 */
static int find_param_tuple(const procgen_token_t *tokens, uint32_t count,
                            uint32_t start, const char *name,
                            float *out, uint32_t out_cap) {
    char raw[256];
    if (find_param_string(tokens, count, start, name, raw, sizeof(raw)) != 0)
        return -1;
    return parse_tuple(raw, out, out_cap);
}

/* ── Room rasterization ────────────────────────────────────────── */

static int rasterize_room_quad(const procgen_token_t *tokens, uint32_t count,
                               uint32_t pos, room_arr *rooms,
                               char *err_buf, uint32_t err_cap) {
    float x = 0, y = 0, w = 0, h = 0, floor_z = 0, ceil_z = 0;

    if (find_param_float(tokens, count, pos + 1, "x", &x) != 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD missing parameter 'x'");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "y", &y) != 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD missing parameter 'y'");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "w", &w) != 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD missing parameter 'w'");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "h", &h) != 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD missing parameter 'h'");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "floor_z", &floor_z) != 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD missing parameter 'floor_z'");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "ceil_z", &ceil_z) != 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD missing parameter 'ceil_z'");
        return -1;
    }

    if (w <= 0 || h <= 0) {
        snprintf(err_buf, err_cap, "ROOM_QUAD width/height must be positive");
        return -1;
    }
    if (ceil_z <= floor_z) {
        snprintf(err_buf, err_cap, "ROOM_QUAD ceil_z must be greater than floor_z");
        return -1;
    }

    fr_room_def_t room;
    memset(&room, 0, sizeof(room));
    room.vertex_count = 4;
    room.vertices[0] = (vec3_t){x,     y,     floor_z};
    room.vertices[1] = (vec3_t){x + w, y,     floor_z};
    room.vertices[2] = (vec3_t){x + w, y + h, floor_z};
    room.vertices[3] = (vec3_t){x,     y + h, floor_z};
    room.floor_z = floor_z;
    room.ceil_z  = ceil_z;

    char name[64] = {0};
    if (find_param_string(tokens, count, pos + 1, "name", name, sizeof(name)) == 0) {
        memcpy(room.name, name, sizeof(room.name));
    }

    return room_arr_push(rooms, &room);
}

static int rasterize_room_pent(const procgen_token_t *tokens, uint32_t count,
                               uint32_t pos, room_arr *rooms,
                               char *err_buf, uint32_t err_cap) {
    float floor_z = 0, ceil_z = 0;

    if (find_param_float(tokens, count, pos + 1, "floor_z", &floor_z) != 0) {
        snprintf(err_buf, err_cap, "ROOM_PENT missing parameter 'floor_z'");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "ceil_z", &ceil_z) != 0) {
        snprintf(err_buf, err_cap, "ROOM_PENT missing parameter 'ceil_z'");
        return -1;
    }
    if (ceil_z <= floor_z) {
        snprintf(err_buf, err_cap, "ROOM_PENT ceil_z must be greater than floor_z");
        return -1;
    }

    /* For now, generate a default pentagon.  The real polygon= param
       requires coordinate tuple parsing which will be added in P1-refine. */
    fr_room_def_t room;
    memset(&room, 0, sizeof(room));
    room.vertex_count = 5;
    float cx = 0, cy = 0, r = 5.0f;
    for (uint32_t i = 0; i < 5; i++) {
        float angle = (float)i * 2.0f * (float)M_PI / 5.0f - (float)M_PI / 2.0f;
        room.vertices[i] = (vec3_t){
            cx + r * cosf(angle),
            cy + r * sinf(angle),
            floor_z
        };
    }
    room.floor_z = floor_z;
    room.ceil_z  = ceil_z;

    char name[64] = {0};
    if (find_param_string(tokens, count, pos + 1, "name", name, sizeof(name)) == 0) {
        memcpy(room.name, name, sizeof(room.name));
    }

    return room_arr_push(rooms, &room);
}

/* ── Corridor rasterization ────────────────────────────────────── */

static int rasterize_corridor(const procgen_token_t *tokens, uint32_t count,
                              uint32_t pos, corridor_arr *corridors,
                              char *err_buf, uint32_t err_cap) {
    float from_x = 0, from_y = 0, to_x = 0, to_y = 0, w = 0, floor_z = 0, ceil_z = 0;

    /* Try from=(x,y) and to=(x,y) coordinate tuples. */
    float ftup[4];
    if (find_param_tuple(tokens, count, pos + 1, "from", ftup, 4) >= 2) {
        from_x = ftup[0]; from_y = ftup[1];
    }
    float ttup[4];
    if (find_param_tuple(tokens, count, pos + 1, "to", ttup, 4) >= 2) {
        to_x = ttup[0]; to_y = ttup[1];
    }

    if (find_param_float(tokens, count, pos + 1, "w", &w) != 0) {
        w = 4.0f;
    }
    if (find_param_float(tokens, count, pos + 1, "floor_z", &floor_z) != 0) {
        snprintf(err_buf, err_cap, "corridor missing floor_z");
        return -1;
    }
    if (find_param_float(tokens, count, pos + 1, "ceil_z", &ceil_z) != 0) {
        snprintf(err_buf, err_cap, "corridor missing ceil_z");
        return -1;
    }
    if (ceil_z <= floor_z) {
        snprintf(err_buf, err_cap, "corridor ceil_z must be greater than floor_z");
        return -1;
    }

    fr_corridor_def_t corr;
    memset(&corr, 0, sizeof(corr));
    corr.from       = (vec3_t){from_x, from_y, floor_z};
    corr.to         = (vec3_t){to_x,   to_y,   floor_z};
    corr.width      = w > 0 ? w : 4.0f;
    corr.floor_z    = floor_z;
    corr.ceil_z     = ceil_z;

    tok_type_t kw = tokens[pos].type;
    if (kw == TOK_CORRIDOR_H)      corr.angle_type = CORR_ANGLE_H;
    else if (kw == TOK_CORRIDOR_V) corr.angle_type = CORR_ANGLE_V;
    else if (kw == TOK_CORRIDOR_DIAG) corr.angle_type = CORR_ANGLE_45;

    return corridor_arr_push(corridors, &corr);
}

/* ── Opening rasterization ─────────────────────────────────────── */

static int rasterize_opening(const procgen_token_t *tokens, uint32_t count,
                             uint32_t pos, opening_arr *openings,
                             char *err_buf, uint32_t err_cap) {
    float ox = 0, oy = 0, oz = 0, w = 0, h = 0;

    float tup[3];
    if (find_param_tuple(tokens, count, pos + 1, "at", tup, 3) >= 2) {
        ox = tup[0]; oy = tup[1];
        if (find_param_tuple(tokens, count, pos + 1, "at", tup, 3) >= 3)
            oz = tup[2];
    }

    if (find_param_float(tokens, count, pos + 1, "w", &w) != 0) w = 2.0f;
    if (find_param_float(tokens, count, pos + 1, "h", &h) != 0) h = 3.0f;

    fr_opening_def_t op;
    memset(&op, 0, sizeof(op));
    op.pos    = (vec3_t){ox, oy, oz};
    op.width  = w > 0 ? w : 2.0f;
    op.height = h > 0 ? h : 3.0f;
    op.type   = (tokens[pos].type == TOK_DOOR) ? OPEN_DOOR : OPEN_WINDOW;

    if (opening_arr_push(openings, &op) != 0) {
        snprintf(err_buf, err_cap, "failed to add opening");
        return -1;
    }
    return 0;
}

/* ── Ramp rasterization ────────────────────────────────────────── */

static int rasterize_ramp(const procgen_token_t *tokens, uint32_t count,
                          uint32_t pos, ramp_arr *ramps,
                          char *err_buf, uint32_t err_cap) {
    float fx = 0, fy = 0, tx = 10, ty = 0, dz = 0, w = 4.0f;

    float ftup[4];
    if (find_param_tuple(tokens, count, pos + 1, "from", ftup, 4) >= 2) {
        fx = ftup[0]; fy = ftup[1];
    }
    float ttup[4];
    if (find_param_tuple(tokens, count, pos + 1, "to", ttup, 4) >= 2) {
        tx = ttup[0]; ty = ttup[1];
    }

    if (find_param_float(tokens, count, pos + 1, "dz", &dz) != 0) {
        snprintf(err_buf, err_cap, "ramp missing dz parameter");
        return -1;
    }
    find_param_float(tokens, count, pos + 1, "w", &w);

    float hc = (tokens[pos].type == TOK_RAMP_UP) ? dz : -dz;

    fr_ramp_def_t ramp;
    memset(&ramp, 0, sizeof(ramp));
    ramp.from = (vec3_t){fx, fy, 0.0f};
    ramp.to   = (vec3_t){tx, ty, hc};
    ramp.height_change = hc;
    ramp.width = w > 0 ? w : 4.0f;

    return ramp_arr_push(ramps, &ramp);
}

/* ── Marker rasterization ──────────────────────────────────────── */

static int rasterize_marker(const procgen_token_t *tokens, uint32_t count,
                            uint32_t pos, marker_arr *markers,
                            char *err_buf, uint32_t err_cap) {
    float x = 0, y = 0, z = 0;
    find_param_float(tokens, count, pos + 1, "x", &x);
    find_param_float(tokens, count, pos + 1, "y", &y);
    find_param_float(tokens, count, pos + 1, "z", &z);

    fr_marker_def_t m;
    memset(&m, 0, sizeof(m));
    m.pos = (vec3_t){x, y, z};

    if (find_param_string(tokens, count, pos + 1, "name", m.name, sizeof(m.name)) != 0) {
        snprintf(err_buf, err_cap, "marker missing name parameter");
        return -1;
    }

    return marker_arr_push(markers, &m);
}

/* ── Main rasterize function ───────────────────────────────────── */

int grammar_blockout_rasterize(const procgen_token_t *tokens,
                               uint32_t count,
                               fr_dungeon_layout_t *layout,
                               char *err_buf, uint32_t err_cap) {
    if (!tokens || !layout || count == 0) {
        if (err_buf && err_cap > 0) snprintf(err_buf, err_cap, "invalid arguments");
        return -1;
    }

    memset(layout, 0, sizeof(*layout));
    layout->version = 1;

    room_arr      rooms;
    corridor_arr  corridors;
    opening_arr   openings;
    ramp_arr      ramps;
    marker_arr    markers;

    if (room_arr_init(&rooms, 4) != 0)      return -1;
    if (corridor_arr_init(&corridors, 4) != 0) { rooms_free(&rooms); return -1; }
    if (opening_arr_init(&openings, 4) != 0)   { rooms_free(&rooms); corridors_free(&corridors); return -1; }
    if (ramp_arr_init(&ramps, 4) != 0)        { rooms_free(&rooms); corridors_free(&corridors); openings_free(&openings); return -1; }
    if (marker_arr_init(&markers, 4) != 0)    { rooms_free(&rooms); corridors_free(&corridors); openings_free(&openings); ramps_free(&ramps); return -1; }

    /* Copy grammar name from header. */
    for (uint32_t i = 0; i < count; i++) {
        if (tokens[i].type == TOK_GRAMMAR) {
            uint32_t slen = (uint32_t)strlen(tokens[i].value.s);
            uint32_t clen = sizeof(layout->grammar_name) - 1;
            memcpy(layout->grammar_name, tokens[i].value.s,
                   slen < clen ? slen : clen);
            layout->grammar_name[slen < clen ? slen : clen] = '\0';
            layout->grammar_version = tokens[i].grammar_version;
            break;
        }
    }

    int spawn_found = 0;

    for (uint32_t i = 0; i < count; i++) {
        const procgen_token_t *t = &tokens[i];

        /* Skip param tokens — they are consumed by keyword handlers. */
        if (t->param_name[0] != '\0') continue;

        switch (t->type) {
        case TOK_SPAWN: {
            float sx = 0, sy = 0, sz = 0;
            find_param_float(tokens, count, i + 1, "x", &sx);
            find_param_float(tokens, count, i + 1, "y", &sy);
            find_param_float(tokens, count, i + 1, "z", &sz);
            layout->spawn_pos = (vec3_t){sx, sy, sz};
            spawn_found = 1;
            break;
        }
        case TOK_ROOM_QUAD:
            if (rasterize_room_quad(tokens, count, i, &rooms, err_buf, err_cap) != 0) goto fail;
            break;
        case TOK_ROOM_PENT:
            if (rasterize_room_pent(tokens, count, i, &rooms, err_buf, err_cap) != 0) goto fail;
            break;
        case TOK_CORRIDOR_H:
        case TOK_CORRIDOR_V:
        case TOK_CORRIDOR_DIAG:
            if (rasterize_corridor(tokens, count, i, &corridors, err_buf, err_cap) != 0) goto fail;
            break;
        case TOK_DOOR:
        case TOK_WINDOW:
            if (rasterize_opening(tokens, count, i, &openings, err_buf, err_cap) != 0) goto fail;
            break;
        case TOK_RAMP_UP:
        case TOK_RAMP_DOWN:
            if (rasterize_ramp(tokens, count, i, &ramps, err_buf, err_cap) != 0) goto fail;
            break;
        case TOK_MARKER:
            if (rasterize_marker(tokens, count, i, &markers, err_buf, err_cap) != 0) goto fail;
            break;
        case TOK_BLOCK:
        case TOK_EBLOCK:
        case TOK_GRAMMAR:
            /* Already handled or no-op. */
            break;
        default:
            /* Param tokens are skipped — they're consumed by keyword handlers. */
            break;
        }
    }

    /* Transfer to layout. */
    layout->room_count     = rooms.count;
    layout->rooms          = rooms.data;
    layout->corridor_count = corridors.count;
    layout->corridors      = corridors.data;
    layout->opening_count  = openings.count;
    layout->openings       = openings.data;
    layout->ramp_count     = ramps.count;
    layout->ramps          = ramps.data;
    layout->marker_count   = markers.count;
    layout->markers        = markers.data;

    if (!spawn_found) {
        snprintf(err_buf, err_cap, "no SPAWN token found");
        goto fail;
    }

    return 0;

fail:
    rooms_free(&rooms);
    corridors_free(&corridors);
    openings_free(&openings);
    ramps_free(&ramps);
    markers_free(&markers);
    return -1;
}
