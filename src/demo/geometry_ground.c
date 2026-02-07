#include "ferrum/demo/geometry.h"
#include <string.h>

uint32_t demo_generate_ground_plane(float *out_vertices, float half_size) {
    if (!out_vertices) return 18;
    float s = half_size;
    /* Two triangles forming a quad at y=0. */
    float verts[] = {
        -s, 0, -s,  -s, 0, s,  s, 0, s,   /* tri 1 */
        -s, 0, -s,  s, 0, s,  s, 0, -s,    /* tri 2 */
    };
    memcpy(out_vertices, verts, sizeof(verts));
    return 18;
}
