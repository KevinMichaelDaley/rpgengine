/**
 * @file static_mesh_draw.c
 * @brief Bind VAO, draw submeshes, unbind.
 */

#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/gl_constants.h"

#include <stddef.h>

void static_mesh_bind(const static_mesh_t *mesh)
{
    if (!mesh) return;
    if (mesh->vao.glBindVertexArray) {
        mesh->vao.glBindVertexArray(mesh->vao.handle);
    }
}

void static_mesh_draw_submesh(const static_mesh_t *mesh,
                              uint32_t submesh_index)
{
    if (!mesh || submesh_index >= mesh->submesh_count) return;

    const render_submesh_t *sub = &mesh->submeshes[submesh_index];
    if (mesh->glDrawElements) {
        size_t offset_bytes = (size_t)sub->index_offset * sizeof(uint32_t);
        mesh->glDrawElements(
            GL_TRIANGLES,
            (int)sub->index_count,
            GL_UNSIGNED_INT,
            (const void *)offset_bytes
        );
    }
}

void static_mesh_unbind(void)
{
    /* No-op in test/headless — real GL bind handled via vao. */
}
