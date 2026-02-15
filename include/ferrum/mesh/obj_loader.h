/**
 * @file obj_loader.h
 * @brief Wavefront OBJ triangle mesh loader.
 *
 * Loads triangle faces from a Wavefront .obj file into a flat vertex
 * buffer suitable for OpenGL rendering or physics triangle arrays.
 * Only triangle faces are supported (quads are not triangulated).
 *
 * Supported face formats:
 *   - f v1 v2 v3           (position only)
 *   - f v1//vn1 v2//vn2 v3//vn3  (position + normal)
 *   - f v1/vt1/vn1 ...     (position + texcoord + normal)
 *   - f v1/vt1 ...          (position + texcoord)
 *
 * Ownership: The caller owns the output buffer.
 * Thread safety: Reentrant (no global state).
 */

#ifndef FERRUM_MESH_OBJ_LOADER_H
#define FERRUM_MESH_OBJ_LOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load triangle vertices from a Wavefront .obj file.
 *
 * Parses vertex positions (v lines) and triangle faces (f lines).
 * Each triangle is emitted as 9 consecutive floats (3 vertices × xyz).
 * A uniform scale factor is applied to all vertex positions.
 *
 * If the file contains more triangles than max_tris, the function
 * returns -1 (overflow) and sets *out_tri_count to the total number
 * of triangles in the file.  This allows a two-pass pattern:
 *   1. Call with max_tris=0 and verts_out=NULL to query the count.
 *   2. Allocate and call again with the correct capacity.
 *
 * @param path          Path to the .obj file (must not be NULL).
 * @param scale         Uniform scale applied to all vertex positions.
 * @param verts_out     Output buffer for triangle vertices (may be NULL
 *                      when max_tris is 0 for count-only mode).
 *                      Must hold at least max_tris × 9 floats.
 * @param max_tris      Maximum number of triangles that fit in verts_out.
 * @param out_tri_count Receives the number of triangles loaded (or total
 *                      count on overflow).  Must not be NULL.
 *
 * @return 0 on success, -1 on overflow or error (NULL path, file not found).
 *
 * Side effects: Reads from the filesystem.
 */
int obj_load_triangles(const char *path,
                       float scale,
                       float *verts_out,
                       uint32_t max_tris,
                       uint32_t *out_tri_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_MESH_OBJ_LOADER_H */
