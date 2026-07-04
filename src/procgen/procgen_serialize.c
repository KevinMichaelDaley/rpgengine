/**
 * @file procgen_serialize.c
 * @brief Serialize fr_dungeon_layout_t → JSON level file.
 */

#include "ferrum/procgen/procgen_serialize.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Buffer-based JSON writer ─────────────────────────────────── */

typedef struct {
    char     *buf;
    uint32_t  cap;
    uint32_t  len;
    int       overflow;
} json_writer_t;

static void jw_init(json_writer_t *w, char *buf, uint32_t cap) {
    w->buf      = buf;
    w->cap      = cap;
    w->len      = 0;
    w->overflow = 0;
    if (cap > 0) buf[0] = '\0';
}

static void jw_append(json_writer_t *w, const char *s) {
    uint32_t slen = (uint32_t)strlen(s);
    if (w->overflow) return;
    if (w->len + slen >= w->cap) { w->overflow = 1; return; }
    memcpy(w->buf + w->len, s, slen + 1);
    w->len += slen;
}

static void jw_append_int(json_writer_t *w, int v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", v);
    jw_append(w, tmp);
}

static void jw_append_float(json_writer_t *w, float v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%.6g", v);
    jw_append(w, tmp);
}

static void jw_append_str(json_writer_t *w, const char *s) {
    jw_append(w, "\"");
    jw_append(w, s);
    jw_append(w, "\"");
}

static void jw_append_vec3(json_writer_t *w, float x, float y, float z) {
    jw_append(w, "[");
    jw_append_float(w, x);
    jw_append(w, ",");
    jw_append_float(w, y);
    jw_append(w, ",");
    jw_append_float(w, z);
    jw_append(w, "]");
}

/* ── Entity base ID ───────────────────────────────────────────── */

static int g_next_id = 0;
static void reset_id(void) { g_next_id = 0; }
static int next_id(void) { return g_next_id++; }

/* ── Write one entity ─────────────────────────────────────────── */

static void write_entity(json_writer_t *w, int id,
                         const char *type,
                         float px, float py, float pz,
                         float sx, float sy, float sz,
                         const char *name) {
    jw_append(w, "{");
    jw_append(w, "\"id\":");
    jw_append_int(w, id);
    jw_append(w, ",\"type\":");
    jw_append_str(w, type);
    jw_append(w, ",\"pos\":");
    jw_append_vec3(w, px, py, pz);
    jw_append(w, ",\"rot\":[0,0,0]");
    jw_append(w, ",\"scale\":");
    jw_append_vec3(w, sx, sy, sz);
    if (name && name[0]) {
        jw_append(w, ",\"name\":");
        jw_append_str(w, name);
    }
    jw_append(w, "}");
}

/* ── Serialize layout to buffer ───────────────────────────────── */

int procgen_serialize_to_json_buf(const fr_dungeon_layout_t *layout,
                                  char *buf, uint32_t buf_cap,
                                  uint32_t *out_len) {
    if (!layout || !buf || buf_cap == 0) return -1;

    json_writer_t w;
    jw_init(&w, buf, buf_cap);
    reset_id();

    jw_append(&w, "{\"entities\":[");

    int first = 1;

    /* Rooms. */
    for (uint32_t i = 0; i < layout->room_count; i++) {
        const fr_room_def_t *r = &layout->rooms[i];
        float cx = 0, cy = 0;
        for (uint32_t j = 0; j < r->vertex_count; j++) {
            cx += r->vertices[j].x;
            cy += r->vertices[j].y;
        }
        cx /= (float)r->vertex_count;
        cy /= (float)r->vertex_count;

        float w2 = 0, h2 = 0;
        for (uint32_t j = 0; j < r->vertex_count; j++) {
            float dx = fabsf(r->vertices[j].x - cx);
            float dy = fabsf(r->vertices[j].y - cy);
            if (dx > w2) w2 = dx;
            if (dy > h2) h2 = dy;
        }
        float height = r->ceil_z - r->floor_z;

        if (!first) jw_append(&w, ",");
        first = 0;

        const char *rtype = (r->vertex_count == 5) ? "room_pent" : "room_quad";
        write_entity(&w, next_id(), rtype,
                     cx, cy, r->floor_z + height * 0.5f,
                     w2 * 2.0f, h2 * 2.0f, height,
                     r->name[0] ? r->name : NULL);
    }

    /* Corridors. */
    for (uint32_t i = 0; i < layout->corridor_count; i++) {
        const fr_corridor_def_t *c = &layout->corridors[i];
        float cx = (c->from.x + c->to.x) * 0.5f;
        float cy = (c->from.y + c->to.y) * 0.5f;
        float dx = c->to.x - c->from.x;
        float dy = c->to.y - c->from.y;
        float length = sqrtf(dx * dx + dy * dy);
        float height = c->ceil_z - c->floor_z;

        if (!first) jw_append(&w, ",");
        first = 0;

        write_entity(&w, next_id(), "corridor",
                     cx, cy, c->floor_z + height * 0.5f,
                     length, c->width, height,
                     NULL);
    }

    /* Openings (doors/windows). */
    for (uint32_t i = 0; i < layout->opening_count; i++) {
        const fr_opening_def_t *o = &layout->openings[i];
        if (!first) jw_append(&w, ",");
        first = 0;
        const char *otype = (o->type == OPEN_DOOR) ? "door" : "window";
        write_entity(&w, next_id(), otype,
                     o->pos.x, o->pos.y, o->pos.z,
                     o->width, o->height, 0.1f,
                     NULL);
    }

    /* Ramps. */
    for (uint32_t i = 0; i < layout->ramp_count; i++) {
        const fr_ramp_def_t *r = &layout->ramps[i];
        if (!first) jw_append(&w, ",");
        first = 0;
        float cx = (r->from.x + r->to.x) * 0.5f;
        float cy = (r->from.y + r->to.y) * 0.5f;
        float dx = r->to.x - r->from.x;
        float dy = r->to.y - r->from.y;
        float length = sqrtf(dx * dx + dy * dy);
        float cz = (r->from.z + r->to.z) * 0.5f;
        write_entity(&w, next_id(), "ramp",
                     cx, cy, cz,
                     length, r->width, fabsf(r->height_change) + 0.1f,
                     NULL);
    }

    /* Markers. */
    for (uint32_t i = 0; i < layout->marker_count; i++) {
        const fr_marker_def_t *m = &layout->markers[i];
        if (!first) jw_append(&w, ",");
        first = 0;
        write_entity(&w, next_id(), "marker",
                     m->pos.x, m->pos.y, m->pos.z,
                     0.5f, 0.5f, 0.5f,
                     m->name[0] ? m->name : NULL);
    }

    /* Spawn. */
    if (!first) jw_append(&w, ",");
    first = 0;
    write_entity(&w, next_id(), "spawn",
                 layout->spawn_pos.x, layout->spawn_pos.y, layout->spawn_pos.z,
                 1.0f, 1.0f, 2.0f,
                 NULL);

    jw_append(&w, "]}");

    if (w.overflow) return -1;

    if (out_len) *out_len = w.len;
    return 0;
}

/* ── Serialize to file ────────────────────────────────────────── */

int procgen_serialize_to_json(const fr_dungeon_layout_t *layout,
                              const char *path,
                              char *err_buf, uint32_t err_cap) {
    if (!layout || !path) {
        if (err_buf && err_cap > 0) snprintf(err_buf, err_cap, "invalid arguments");
        return -1;
    }

    /* Pre-allocate a reasonable buffer. */
    char buf[65536];
    uint32_t out_len = 0;

    if (procgen_serialize_to_json_buf(layout, buf, sizeof(buf), &out_len) != 0) {
        if (err_buf && err_cap > 0) snprintf(err_buf, err_cap, "JSON buffer overflow");
        return -1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        if (err_buf && err_cap > 0) snprintf(err_buf, err_cap, "cannot open '%s'", path);
        return -1;
    }

    fwrite(buf, 1, out_len, f);
    fclose(f);
    return 0;
}

int procgen_serialize_level(const fr_dungeon_layout_t *layout,
                            const char *path,
                            char *err_buf, uint32_t err_cap) {
    return procgen_serialize_to_json(layout, path, err_buf, err_cap);
}
