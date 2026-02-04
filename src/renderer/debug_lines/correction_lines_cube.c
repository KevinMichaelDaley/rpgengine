#include "ferrum/renderer/debug_correction_lines.h"

#include <math.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec4.h"

static vec3_t rotate_offset_(quat_t rot, vec3_t v) {
    mat4_t m;
    if (quat_to_mat4(rot, &m) != 0) {
        return v;
    }
    const vec4_t r = mat4_mul_vec4(m, (vec4_t){v.x, v.y, v.z, 0.0f});
    return (vec3_t){r.x, r.y, r.z};
}

size_t fr_debug_correction_lines_cube(vec3_t est_pos,
                                     quat_t est_rot,
                                     vec3_t true_pos,
                                     quat_t true_rot,
                                     float half_extent,
                                     vec3_t *out_vertices,
                                     size_t out_vertices_cap) {
    if (!out_vertices || out_vertices_cap < 16u) {
        return 0u;
    }
    if (!(half_extent > 0.0f) || !isfinite(half_extent)) {
        return 0u;
    }

    const float h = half_extent;
    const vec3_t corners[8] = {
        {-h, -h, -h},
        {-h, -h, +h},
        {-h, +h, -h},
        {-h, +h, +h},
        {+h, -h, -h},
        {+h, -h, +h},
        {+h, +h, -h},
        {+h, +h, +h},
    };

    size_t out = 0u;
    for (size_t i = 0u; i < 8u; ++i) {
        const vec3_t off = corners[i];

        const vec3_t est_off = rotate_offset_(est_rot, off);
        const vec3_t true_off = rotate_offset_(true_rot, off);

        const vec3_t est_corner = (vec3_t){
            est_pos.x + est_off.x,
            est_pos.y + est_off.y,
            est_pos.z + est_off.z,
        };

        const vec3_t true_corner = (vec3_t){
            true_pos.x + true_off.x,
            true_pos.y + true_off.y,
            true_pos.z + true_off.z,
        };

        out_vertices[out + 0u] = est_corner;
        out_vertices[out + 1u] = true_corner;
        out += 2u;
    }

    return out;
}
