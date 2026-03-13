/**
 * @file edit_serialize_full.c
 * @brief Full level serialization — entities + groups to JSON.
 *
 * Version 2 format:
 *   {"version":2,"entities":[...],"groups":[{"name":"&x","ids":[1,2],
 *     "pivot":[x,y,z],"parent":"&p"},...]}}
 *
 * Non-static functions: 1 (edit_level_serialize_full).
 */

#include "ferrum/editor/edit_serialize.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_cmd_ctx.h"

#include <stdio.h>
#include <string.h>

size_t edit_level_serialize_full(const struct edit_entity_store *store,
                                const struct edit_cmd_ctx *ctx,
                                char *buf, size_t cap) {
    if (!store) return 0;

    size_t off = 0;

    #define APPEND(...) do { \
        int _n = snprintf(buf ? buf + off : NULL, \
                          buf ? (cap > off ? cap - off : 0) : 0, \
                          __VA_ARGS__); \
        if (_n > 0) off += (size_t)_n; \
    } while (0)

    APPEND("{\"version\":2,\"entities\":[");

    /* ── Entities ──────────────────────────────────────────────── */
    {
        uint32_t type_count = 0;
        const edit_entity_type_info_t *types =
            edit_entity_type_registry(&type_count);

        bool first = true;
        for (uint32_t i = 0; i < store->capacity; i++) {
            const edit_entity_t *e = &store->entities[i];
            if (!e->active) continue;

            if (!first) APPEND(",");
            first = false;

            /* Look up type name. */
            const char *tname = "box";
            for (uint32_t t = 0; t < type_count; t++) {
                if (types[t].type_id == e->type) {
                    tname = types[t].name;
                    break;
                }
            }

            APPEND("{\"id\":%u,\"type\":\"%s\"", i, tname);
            if (e->name[0] != '\0') {
                APPEND(",\"name\":\"%s\"", e->name);
            }
            APPEND(",\"pos\":[%.6g,%.6g,%.6g],"
                   "\"rot\":[%.6g,%.6g,%.6g],"
                   "\"scale\":[%.6g,%.6g,%.6g]",
                   (double)e->pos[0], (double)e->pos[1], (double)e->pos[2],
                   (double)e->rot[0], (double)e->rot[1], (double)e->rot[2],
                   (double)e->scale[0], (double)e->scale[1],
                   (double)e->scale[2]);
            if (e->pivot_offset[0] != 0.0f || e->pivot_offset[1] != 0.0f ||
                e->pivot_offset[2] != 0.0f) {
                APPEND(",\"pivot_offset\":[%.6g,%.6g,%.6g]",
                       (double)e->pivot_offset[0], (double)e->pivot_offset[1],
                       (double)e->pivot_offset[2]);
            }
            APPEND("}");
        }
    }

    APPEND("]");

    /* ── Groups ────────────────────────────────────────────────── */
    if (ctx && ctx->groups) {
        APPEND(",\"groups\":[");
        bool first = true;
        for (uint32_t i = 0; i < ctx->group_capacity; i++) {
            const edit_group_t *g = &ctx->groups[i];
            if (!g->active) continue;

            if (!first) APPEND(",");
            first = false;

            APPEND("{\"name\":\"%s\",\"ids\":[", g->name);
            for (uint32_t j = 0; j < g->count; j++) {
                if (j > 0) APPEND(",");
                APPEND("%u", g->ids[j]);
            }
            APPEND("],\"pivot\":[%.6g,%.6g,%.6g]",
                   (double)g->pivot[0], (double)g->pivot[1],
                   (double)g->pivot[2]);
            if (g->parent[0] != '\0') {
                APPEND(",\"parent\":\"%s\"", g->parent);
            }
            APPEND("}");
        }
        APPEND("]");
    }

    APPEND("}");
    #undef APPEND

    if (buf && off < cap) buf[off] = '\0';
    return off;
}
