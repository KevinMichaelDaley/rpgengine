/**
 * @file scene_cmd_abs.c
 * @brief Formats absolute-value transform commands (move_abs, scale_abs).
 *
 * These set the exact position/scale on the server, used after
 * client-side snapping to ensure client and server stay in sync.
 *
 * Non-static functions (2 / 4 limit):
 *   scene_cmd_format_move_abs
 *   scene_cmd_format_scale_abs
 */

#include "ferrum/editor/scene/scene_cmd.h"
#include <stdio.h>

int scene_cmd_format_move_abs(char *buf, size_t cap, uint32_t id,
                               const float pos[3])
{
    if (!buf || cap == 0 || !pos) return -1;
    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"move\",\"args\":"
                     "{\"abs\":[%.8g,%.8g,%.8g]}}\n",
                     (unsigned)id,
                     (double)pos[0], (double)pos[1], (double)pos[2]);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

int scene_cmd_format_scale_abs(char *buf, size_t cap, uint32_t id,
                                const float scale[3])
{
    if (!buf || cap == 0 || !scale) return -1;
    int n = snprintf(buf, cap,
                     "{\"id\":%u,\"cmd\":\"scale\",\"args\":"
                     "{\"abs\":[%.8g,%.8g,%.8g]}}\n",
                     (unsigned)id,
                     (double)scale[0], (double)scale[1], (double)scale[2]);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}
