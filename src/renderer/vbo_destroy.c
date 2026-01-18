#include "ferrum/renderer/vbo.h"

void vbo_destroy(vbo_t *vbo) {
    if (vbo == NULL || vbo->handle == 0u) {
        return;
    }
    if (vbo->glDeleteBuffers != NULL) {
        vbo->glDeleteBuffers(1, &vbo->handle);
    }
    vbo->handle = 0u;
}
