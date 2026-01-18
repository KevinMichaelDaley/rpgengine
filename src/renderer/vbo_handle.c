#include "ferrum/renderer/vbo.h"

uint32_t vbo_handle(const vbo_t *vbo) {
    if (vbo == NULL) {
        return 0u;
    }
    return vbo->handle;
}
