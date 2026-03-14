#ifndef FERRUM_RENDERER_GL_CONSTANTS_H
#define FERRUM_RENDERER_GL_CONSTANTS_H

#include <stdint.h>

/** @file
 * @brief Minimal GL constant definitions used by renderer wrappers.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_INT
#define GL_INT 0x1404
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_NEAREST
#define GL_NEAREST 0x2600
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_FALSE
#define GL_FALSE 0
#endif

/* ---- Blending ---- */
#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif

/* ---- State enable/disable ---- */
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44
#endif
#ifndef GL_FRONT
#define GL_FRONT 0x0404
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif
#ifndef GL_FRONT_AND_BACK
#define GL_FRONT_AND_BACK 0x0408
#endif
#ifndef GL_LINE
#define GL_LINE 0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif

/* ---- Clear buffer bits ---- */
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif

/* ---- Texture formats ---- */
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_LINEAR
#define GL_LINEAR 0x2601
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GL_CONSTANTS_H */
