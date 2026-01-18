#include "ferrum/renderer/vbo.h"

vbo_status_t vbo_upload(vbo_t *vbo, uint32_t target, const void *data, size_t size, uint32_t usage) {
    if (vbo == NULL || vbo->handle == 0u) {
        return VBO_ERR_INVALID;
    }
    if (size == 0u) {
        return VBO_ERR_ZERO_SIZE;
    }
    if (vbo->glBindBuffer == NULL || vbo->glBufferData == NULL) {
        return VBO_ERR_MISSING_GL;
    }
    vbo->glBindBuffer(target, vbo->handle);
    vbo->glBufferData(target, size, data, usage);
    return VBO_OK;
}
