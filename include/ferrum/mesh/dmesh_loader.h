/**
 * @file dmesh_loader.h
 * @brief Loader for the dual-UV binary mesh (.dmesh): position, normal, and TWO
 *        UV channels (material + lightmap) per vertex.
 *
 * The offline arch/lightmap pipeline exports meshes from Blender as a flat
 * triangle soup: a uint32 corner-count, then count * (pos3, nrm3, uv0_2, uv1_2)
 * little-endian floats (Y-up). This loader welds coincident corners into an
 * indexed @ref obj_mesh_t -- channel-0 UV (@ref obj_mesh.uvs) = material UV,
 * channel-1 UV (@ref obj_mesh.uvs1) = lightmap UV -- and generates smooth
 * per-vertex tangents. Callers therefore get real shared-vertex topology and
 * continuous tangents, never a triangle soup with discontinuous per-face frames.
 *
 * Ownership: fills an @ref obj_mesh_t the caller frees with @ref obj_mesh_free.
 * Thread safety: reentrant. Errors: returns -1 on bad args / IO / OOM.
 */
#ifndef FERRUM_MESH_DMESH_LOADER_H
#define FERRUM_MESH_DMESH_LOADER_H

#include "ferrum/mesh/obj_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load a dual-UV .dmesh binary into an indexed @ref obj_mesh_t.
 *
 * Dedupes the triangle soup (weld by position + normal + both UV channels),
 * fills @p out->uvs (channel 0) and @p out->uvs1 (channel 1), and generates
 * tangents from channel 0. Positions/normals are taken from the file verbatim.
 *
 * @return 0 on success, -1 on error (NULL args, missing file, out of memory).
 */
int dmesh_load(const char *path, obj_mesh_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_MESH_DMESH_LOADER_H */
