#include "ferrum/renderer/shader_program.h"

void shader_program_destroy(shader_program_t *program) {
    if (program == NULL || program->handle == 0u) {
        return;
    }
    if (program->glDeleteProgram != NULL) {
        program->glDeleteProgram(program->handle);
    }
    program->handle = 0u;
}
