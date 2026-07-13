#ifndef FERRUM_RENDERER_PBR_SHADER_H
#define FERRUM_RENDERER_PBR_SHADER_H

#include <stddef.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"

/** @file
 * @brief The core PBR shader program: a metallic-roughness Cook-Torrance BRDF.
 *
 * Vertex inputs follow the engine's static-mesh attribute layout
 * (0=position, 1=normal, 2=tangent.xyzw, 3=uv0, 4=uv1) and the fragment shader
 * consumes the @ref render_material uniform contract (albedo/normal/metallic/
 * roughness/AO/emissive maps + tint, specular strength, metalness, roughness
 * min/max, normal/AO strength, emissive strength). Lighting in this core
 * program is a single directional "sun" plus a constant ambient term; the
 * lightmap ambient (rpg-0i1w) and the clustered punctual set (rpg-pdiv) extend
 * it in later tasks.
 *
 * Camera/light uniforms: u_model, u_view, u_projection (mat4), u_eye_pos,
 * u_sun_dir (direction TO the light), u_sun_color, u_ambient (vec3).
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compile and link the core PBR shader program.
 * @param out     Output program (non-NULL).
 * @param loader  GL loader table (non-NULL).
 * @param log     Optional buffer for the compile/link log (may be NULL).
 * @param log_cap Capacity of @p log.
 * @return SHADER_PROGRAM_OK or a shader error status.
 */
shader_program_status_t pbr_shader_create(shader_program_t *out,
                                          const gl_loader_t *loader, char *log,
                                          size_t log_cap);

/** @return The vertex shader GLSL source (static string). */
const char *pbr_shader_vertex_source(void);

/** @return The fragment shader GLSL source (static string). */
const char *pbr_shader_fragment_source(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_PBR_SHADER_H */
