/**
 * @file skeletal_mesh_draw.c
 * @brief Bind VAO, draw submeshes, unbind — delegates to static_mesh ops.
 */

#include "ferrum/renderer/mesh/skeletal_mesh.h"

void skeletal_mesh_bind(const skeletal_mesh_t *mesh)
{
    if (!mesh) return;
    static_mesh_bind(&mesh->base);
}

void skeletal_mesh_draw_submesh(const skeletal_mesh_t *mesh,
                                uint32_t submesh_index)
{
    if (!mesh) return;
    static_mesh_draw_submesh(&mesh->base, submesh_index);
}

void skeletal_mesh_unbind(void)
{
    static_mesh_unbind();
}
