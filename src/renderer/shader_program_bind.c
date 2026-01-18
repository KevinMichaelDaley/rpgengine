#include "ferrum/renderer/shader_program.h"

shader_program_status_t shader_program_bind(const shader_program_t *program) {
    if (program == NULL || program->handle == 0u) {
        return SHADER_PROGRAM_ERR_INVALID;
    }
    if (program->glUseProgram == NULL) {
        return SHADER_PROGRAM_ERR_MISSING_GL;
    }
    program->glUseProgram(program->handle);
    return SHADER_PROGRAM_OK;
}
