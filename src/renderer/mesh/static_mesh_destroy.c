/**
 * @file static_mesh_destroy.c
 * @brief Destroy a static mesh and release all GPU resources.
 */

#include "ferrum/renderer/mesh/static_mesh.h"

#include <stdlib.h>
#include <string.h>

void static_mesh_destroy(static_mesh_t *mesh)
{
    if (!mesh) return;

    /* Release GPU resources. */
    vbo_destroy(&mesh->vbo_position);
    vbo_destroy(&mesh->vbo_normal);
    vbo_destroy(&mesh->vbo_tangent);
    vbo_destroy(&mesh->vbo_uv0);
    vbo_destroy(&mesh->vbo_uv1);
    vbo_destroy(&mesh->vbo_color);
    vbo_destroy(&mesh->ibo);
    vao_destroy(&mesh->vao);

    /* Free submesh array. */
    free(mesh->submeshes);

    /* Zero the struct so double-destroy is safe. */
    memset(mesh, 0, sizeof(*mesh));
}
