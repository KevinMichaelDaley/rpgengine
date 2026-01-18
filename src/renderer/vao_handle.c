#include "ferrum/renderer/vao.h"

uint32_t vao_handle(const vao_t *vao) {
    if (vao == NULL) {
        return 0u;
    }
    return vao->handle;
}
