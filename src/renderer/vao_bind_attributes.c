#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/gl_constants.h"

#include <stdint.h>

vao_status_t vao_bind_attributes(vao_t *vao,
                                const vbo_t *vbo,
                                const vao_attribute_t *attributes,
                                size_t attribute_count,
                                size_t stride) {
    if (vao == NULL || vbo == NULL || attributes == NULL || attribute_count == 0u || stride == 0u) {
        return VAO_ERR_INVALID;
    }
    if (vao->handle == 0u || vbo->handle == 0u) {
        return VAO_ERR_INVALID;
    }
    if (vao->glBindVertexArray == NULL || vao->glBindBuffer == NULL || vao->glEnableVertexAttribArray == NULL ||
        vao->glVertexAttribPointer == NULL || vao->glVertexAttribIPointer == NULL) {
        return VAO_ERR_MISSING_GL;
    }

    vao->glBindVertexArray(vao->handle);
    vao->glBindBuffer((uint32_t)GL_ARRAY_BUFFER, vbo->handle);

    for (size_t i = 0; i < attribute_count; ++i) {
        const vao_attribute_t *attr = &attributes[i];
        const void *pointer = (const void *)(uintptr_t)attr->offset;
        vao->glEnableVertexAttribArray(attr->index);
        if (attr->integer) {
            vao->glVertexAttribIPointer(attr->index,
                                        (int32_t)attr->components,
                                        attr->type,
                                        stride,
                                        pointer);
        } else {
            vao->glVertexAttribPointer(attr->index,
                                       (int32_t)attr->components,
                                       attr->type,
                                       attr->normalized ? 1u : 0u,
                                       stride,
                                       pointer);
        }
    }

    return VAO_OK;
}
