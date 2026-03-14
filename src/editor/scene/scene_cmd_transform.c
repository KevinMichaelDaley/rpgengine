/**
 * @file scene_cmd_transform.c
 * @brief Formats list, move, rotate, and scale JSON commands.
 *
 * Each function writes a newline-terminated JSON object into the
 * caller-provided buffer. Returns the number of bytes written
 * (excluding NUL terminator), or -1 if the buffer is too small.
 *
 * Non-static functions (4 / 4 limit):
 *   scene_cmd_format_list
 *   scene_cmd_format_move
 *   scene_cmd_format_rotate
 *   scene_cmd_format_scale
 */

#include "ferrum/editor/scene/scene_cmd.h"
#include <stdio.h>

/**
 * @brief Format a 3-component vector command (move, rotate, scale).
 *
 * Shared helper for commands that carry a single named float[3] argument.
 *
 * @param buf       Output buffer.
 * @param cap       Buffer capacity.
 * @param id        Request ID.
 * @param cmd_name  Protocol command name (e.g., "move").
 * @param arg_name  Argument key name (e.g., "delta" or "factor").
 * @param vec       3-component float vector.
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
static int format_vec3_cmd(char *buf, size_t cap, uint32_t id,
                           const char *cmd_name, const char *arg_name,
                           const float vec[3])
{
    if (!buf || cap == 0 || !vec) {
        return -1;
    }

    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"%s\",\"args\":"
                     "{\"%s\":[%.6g,%.6g,%.6g]}}\n",
                     (unsigned)id, cmd_name, arg_name,
                     (double)vec[0], (double)vec[1], (double)vec[2]);

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

int scene_cmd_format_list(char *buf, size_t cap, uint32_t id)
{
    if (!buf || cap == 0) {
        return -1;
    }

    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"list_entities\",\"args\":{}}\n",
                     (unsigned)id);

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

int scene_cmd_format_move(char *buf, size_t cap, uint32_t id,
                          const float delta[3])
{
    return format_vec3_cmd(buf, cap, id, "move", "delta", delta);
}

int scene_cmd_format_rotate(char *buf, size_t cap, uint32_t id,
                            const float quat[4])
{
    if (!buf || cap == 0 || !quat) return -1;
    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"rotate\",\"args\":"
                     "{\"quat\":[%.8g,%.8g,%.8g,%.8g]}}\n",
                     (unsigned)id,
                     (double)quat[0], (double)quat[1],
                     (double)quat[2], (double)quat[3]);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

int scene_cmd_format_scale(char *buf, size_t cap, uint32_t id,
                           const float factor[3])
{
    return format_vec3_cmd(buf, cap, id, "scale", "factor", factor);
}
