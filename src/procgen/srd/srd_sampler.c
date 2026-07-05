#include "ferrum/procgen/srd/srd_sampler.h"
#include <string.h>

int srd_sample_room(const fr_room_box_t *room, int grid_n,
                    srd_sample_point_t *points_out,
                    uint32_t cap, uint32_t *count_out) {
    if (!room || !points_out || !count_out || grid_n < 1) return -1;

    uint32_t n = (uint32_t)(grid_n * grid_n * grid_n);
    if (n > cap) n = cap;

    float hx = room->half_extent_x;
    float hz = room->half_extent_z;
    float hy = (room->ceil_z - room->floor_z) * 0.5f;
    float cy = room->floor_z + hy;

    uint32_t idx = 0;
    for (int i = 0; i < grid_n && idx < n; i++) {
        for (int j = 0; j < grid_n && idx < n; j++) {
            for (int k = 0; k < grid_n && idx < n; k++) {
                float t_x = (i + 0.5f) / grid_n;
                float t_y = (j + 0.5f) / grid_n;
                float t_z = (k + 0.5f) / grid_n;
                points_out[idx].x = room->center_x - hx + t_x * 2.0f * hx;
                points_out[idx].y = cy - hy + t_y * 2.0f * hy;
                points_out[idx].z = room->center_z - hz + t_z * 2.0f * hz;
                idx++;
            }
        }
    }

    *count_out = idx;
    return 0;
}
