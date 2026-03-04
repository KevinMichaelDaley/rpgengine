/**
 * @file skeletal_mesh_destroy.c
 * @brief Destroy a skeletal mesh and release all resources.
 */

#include "ferrum/renderer/mesh/skeletal_mesh.h"

#include <stdlib.h>
#include <string.h>

void skeletal_mesh_destroy(skeletal_mesh_t *mesh)
{
    if (!mesh) return;

    /* Destroy bone VBOs. */
    vbo_destroy(&mesh->vbo_bone_weights);
    vbo_destroy(&mesh->vbo_bone_indices);

    /* Free inverse-bind matrix array. */
    free(mesh->inv_bind_matrices);
    mesh->inv_bind_matrices = NULL;
    mesh->bone_count = 0;

    /* Destroy the base static mesh (VAO, attribute VBOs, IBO, submeshes). */
    static_mesh_destroy(&mesh->base);
}
