/**
 * @file scene_cmd.c
 * @brief Formats spawn, delete, select, and deselect JSON commands.
 *
 * Each function writes a newline-terminated JSON object into the
 * caller-provided buffer. Returns the number of bytes written
 * (excluding NUL terminator), or -1 if the buffer is too small.
 *
 * Non-static functions (4 / 4 limit):
 *   scene_cmd_format_spawn
 *   scene_cmd_format_delete
 *   scene_cmd_format_select
 *   scene_cmd_format_deselect
 */

#include "ferrum/editor/scene/scene_cmd.h"
#include <stdio.h>
#include <string.h>

/* Maps entity type IDs to their protocol name strings. */
static const char *type_name(uint32_t type)
{
    static const char *names[] = {
        "box", "sphere", "capsule", "marker", "mesh", "halfspace"
    };
    if (type < 6) {
        return names[type];
    }
    /* Fall back to "box" for unknown types. */
    return "box";
}

int scene_cmd_format_spawn(char *buf, size_t cap, uint32_t id,
                           uint32_t type, const float pos[3],
                           const char *name)
{
    if (!buf || cap == 0 || !pos) {
        return -1;
    }

    const char *tname = type_name(type);
    int n;

    if (name) {
        n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"spawn\",\"args\":"
                     "{\"type\":\"%s\",\"pos\":[%.6g,%.6g,%.6g],"
                     "\"name\":\"%s\"}}\n",
                     (unsigned)id, tname,
                     (double)pos[0], (double)pos[1], (double)pos[2],
                     name);
    } else {
        n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"spawn\",\"args\":"
                     "{\"type\":\"%s\",\"pos\":[%.6g,%.6g,%.6g]}}\n",
                     (unsigned)id, tname,
                     (double)pos[0], (double)pos[1], (double)pos[2]);
    }

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

int scene_cmd_format_delete(char *buf, size_t cap, uint32_t id)
{
    if (!buf || cap == 0) {
        return -1;
    }

    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"delete\",\"args\":{}}\n",
                     (unsigned)id);

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

int scene_cmd_format_select(char *buf, size_t cap, uint32_t id,
                            uint32_t entity_id)
{
    if (!buf || cap == 0) {
        return -1;
    }

    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"select\",\"args\":"
                     "{\"entity_id\":%u}}\n",
                     (unsigned)id, (unsigned)entity_id);

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

int scene_cmd_format_deselect(char *buf, size_t cap, uint32_t id,
                              uint32_t entity_id)
{
    if (!buf || cap == 0) {
        return -1;
    }

    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"deselect\",\"args\":"
                     "{\"entity_id\":%u}}\n",
                     (unsigned)id, (unsigned)entity_id);

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}
