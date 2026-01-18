#include "ferrum/renderer/shader_program.h"

uint32_t shader_program_handle(const shader_program_t *program) {
    if (program == NULL) {
        return 0u;
    }
    return program->handle;
}
