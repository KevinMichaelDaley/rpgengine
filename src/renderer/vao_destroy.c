#include "ferrum/renderer/vao.h"

void vao_destroy(vao_t *vao) {
    if (vao == NULL || vao->handle == 0u) {
        return;
    }
    if (vao->glDeleteVertexArrays != NULL) {
        vao->glDeleteVertexArrays(1, &vao->handle);
    }
    vao->handle = 0u;
}
