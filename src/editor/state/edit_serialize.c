/**
 * @file edit_serialize.c
 * @brief Level serialization — write entities to JSON and save to file.
 */

#include "ferrum/editor/edit_serialize.h"
#include "ferrum/editor/edit_entity.h"
#include <stdio.h>
#include <string.h>

/** @brief Check path for directory traversal (".."). */
static bool path_is_safe_(const char *path) {
    if (!path) return false;
    if (strstr(path, "..") != NULL) return false;
    return true;
}

/** @brief Map entity type to string. */
static const char *type_str_(uint32_t type) {
    uint32_t count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (types[i].type_id == type) return types[i].name;
    }
    return "box";
}

size_t edit_level_serialize(const struct edit_entity_store *store,
                            char *buf, size_t cap) {
    if (!store) return 0;

    size_t off = 0;

    /* Helper macro: append formatted text. */
    #define APPEND(...) do { \
        int _n = snprintf(buf ? buf + off : NULL, \
                          buf ? (cap > off ? cap - off : 0) : 0, \
                          __VA_ARGS__); \
        if (_n > 0) off += (size_t)_n; \
    } while (0)

    APPEND("{\"version\":1,\"entities\":[");

    bool first = true;
    for (uint32_t i = 0; i < store->capacity; i++) {
        const edit_entity_t *e = &store->entities[i];
        if (!e->active) continue;

        if (!first) APPEND(",");
        first = false;

        APPEND("{\"id\":%u,\"type\":\"%s\"",
               i, type_str_(e->type));

        /* Include name if set. */
        if (e->name[0] != '\0') {
            APPEND(",\"name\":\"%s\"", e->name);
        }

        APPEND(",\"pos\":[%.6g,%.6g,%.6g],"
               "\"rot\":[%.6g,%.6g,%.6g],"
               "\"scale\":[%.6g,%.6g,%.6g]",
               (double)e->pos[0], (double)e->pos[1], (double)e->pos[2],
               (double)e->rot[0], (double)e->rot[1], (double)e->rot[2],
               (double)e->scale[0], (double)e->scale[1], (double)e->scale[2]);

        /* Include pivot_offset only if non-zero. */
        if (e->pivot_offset[0] != 0.0f || e->pivot_offset[1] != 0.0f ||
            e->pivot_offset[2] != 0.0f) {
            APPEND(",\"pivot_offset\":[%.6g,%.6g,%.6g]",
                   (double)e->pivot_offset[0], (double)e->pivot_offset[1],
                   (double)e->pivot_offset[2]);
        }
        APPEND("}");
    }

    APPEND("]}");
    #undef APPEND

    /* Null-terminate if there's room. */
    if (buf && off < cap) buf[off] = '\0';

    return off;
}

bool edit_level_save(const struct edit_entity_store *store, const char *path) {
    if (!store || !path || !path_is_safe_(path)) return false;

    /* Serialize to a stack buffer. */
    char buf[65536];
    size_t len = edit_level_serialize(store, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    return written == len;
}
