#ifndef FERRUM_DEMO_GEOMETRY_H
#define FERRUM_DEMO_GEOMETRY_H

#include <stdint.h>
#include <stddef.h>

/**
 * Generate icosphere vertex data (triangles, position-only).
 * Subdivision level 2 (~80 triangles, ~240 vertices × 3 floats each).
 *
 * @param out_vertices  Output buffer (caller-allocated, or NULL to query size).
 * @param out_count     On return: number of floats written (or needed).
 * @return 0 on success, -1 on error.
 *
 * Vertices are vec3 positions (x,y,z triples). Unit sphere (radius 1.0).
 * Draw with GL_TRIANGLES.
 */
int demo_generate_icosphere(float *out_vertices, uint32_t *out_count);

/**
 * Generate ground plane vertex data (two triangles, large quad).
 * Centered at origin, extends ±half_size on X and Z, Y=0.
 *
 * @param out_vertices  Output buffer (caller-allocated, 18 floats min).
 * @param half_size     Half-extent of the ground plane.
 * @return Number of floats written (18 = 6 verts × 3 components).
 */
uint32_t demo_generate_ground_plane(float *out_vertices, float half_size);

#endif
