/**
 * @file gi_voxelize_init.c
 * @brief Build/teardown of the dynamic-geometry voxeliser (see gi_voxelize.h).
 */
#include <glad/glad.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_voxelize.h"

/* 4.3 enums glad (3.3) lacks. */
#define GV_GL_WRITE_ONLY               0x88B9
#define GV_GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define GV_GL_FRAMEBUFFER_DEFAULT_WIDTH  0x9310
#define GV_GL_FRAMEBUFFER_DEFAULT_HEIGHT 0x9311

/* Vertex: world position + an axis-aligned ortho. u_axis picks WHICH axis we are
 * rasterising along; the two remaining axes become the raster plane. Depth is
 * flat (0) because the destination voxel comes from the world position in the FS,
 * not from the framebuffer -- so no gl_Layer routing is needed. */
static const char *const GV_VS =
    "#version 430 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "uniform mat4 u_model;\n"
    "uniform vec3 u_origin;\n"
    "uniform vec3 u_extent;\n"
    "uniform int u_axis;\n"
    "out vec3 v_world;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_world = wp.xyz;\n"
    "  vec3 n = (wp.xyz - u_origin) / max(u_extent, vec3(1e-6));\n"
    "  vec2 xy = (u_axis==0) ? n.yz : ((u_axis==1) ? n.xz : n.xy);\n"
    "  gl_Position = vec4(xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Fragment: stamp the object's albedo into the voxel the fragment's WORLD position
 * falls in. a=1 marks "dynamic geometry here" so the probe shader prefers it over
 * the baked voxel albedo. */
static const char *const GV_FS =
    "#version 430 core\n"
    "layout(rgba8, binding=0) uniform writeonly image3D u_vol;\n"
    "in vec3 v_world;\n"
    "uniform vec3 u_origin;\n"
    "uniform vec3 u_extent;\n"
    "uniform ivec3 u_dim;\n"
    "uniform vec3 u_albedo;\n"
    "void main(){\n"
    "  vec3 n = (v_world - u_origin) / max(u_extent, vec3(1e-6));\n"
    "  ivec3 vi = ivec3(floor(n * vec3(u_dim)));\n"
    "  if(all(greaterThanEqual(vi, ivec3(0))) && all(lessThan(vi, u_dim)))\n"
    "    imageStore(u_vol, vi, vec4(u_albedo, 1.0));\n"
    "}\n";

static unsigned int gv_compile(unsigned int type, const char *src)
{
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    int ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, (int)sizeof log, NULL, log);
        fprintf(stderr, "gi_voxelize: shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool gi_voxelize_init(gi_voxelize_t *v, const gl_loader_t *loader)
{
    if (v == NULL || loader == NULL || loader->get_proc_address == NULL) return false;
    memset(v, 0, sizeof *v);

#define GV_LOAD(dst, name)                                                     \
    do { void *p_ = loader->get_proc_address((name), loader->user_data);        \
         if (p_ == NULL) { fprintf(stderr, "gi_voxelize: missing %s\n", (name)); \
                           return false; }                                      \
         memcpy(&(dst), &p_, sizeof p_); } while (0)
    GV_LOAD(v->BindImageTexture, "glBindImageTexture");
    GV_LOAD(v->MemoryBarrier, "glMemoryBarrier");
    GV_LOAD(v->FramebufferParameteri, "glFramebufferParameteri");
    GV_LOAD(v->GenFramebuffers, "glGenFramebuffers");
    GV_LOAD(v->DeleteFramebuffers, "glDeleteFramebuffers");
    GV_LOAD(v->BindFramebuffer, "glBindFramebuffer");
#undef GV_LOAD

    unsigned int vs = gv_compile(GL_VERTEX_SHADER, GV_VS);
    unsigned int fs = vs ? gv_compile(GL_FRAGMENT_SHADER, GV_FS) : 0u;
    if (vs == 0u || fs == 0u) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return false; }
    v->prog = glCreateProgram();
    glAttachShader(v->prog, vs); glAttachShader(v->prog, fs);
    glLinkProgram(v->prog);
    glDeleteShader(vs); glDeleteShader(fs);
    int ok = 0; glGetProgramiv(v->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(v->prog, (int)sizeof log, NULL, log);
        fprintf(stderr, "gi_voxelize: link failed: %s\n", log);
        glDeleteProgram(v->prog); v->prog = 0u;
        return false;
    }
    v->loc_model  = glGetUniformLocation(v->prog, "u_model");
    v->loc_origin = glGetUniformLocation(v->prog, "u_origin");
    v->loc_extent = glGetUniformLocation(v->prog, "u_extent");
    v->loc_dim    = glGetUniformLocation(v->prog, "u_dim");
    v->loc_albedo = glGetUniformLocation(v->prog, "u_albedo");
    v->loc_axis   = glGetUniformLocation(v->prog, "u_axis");
    v->loc_vol    = glGetUniformLocation(v->prog, "u_vol");

    /* Attachment-less FBO: we rasterise purely to generate fragments; all output
     * goes through imageStore, so the framebuffer only needs a default size. */
    v->GenFramebuffers(1, &v->fbo);
    v->BindFramebuffer(GL_FRAMEBUFFER, v->fbo);
    v->FramebufferParameteri(GL_FRAMEBUFFER, GV_GL_FRAMEBUFFER_DEFAULT_WIDTH, 64);
    v->FramebufferParameteri(GL_FRAMEBUFFER, GV_GL_FRAMEBUFFER_DEFAULT_HEIGHT, 64);
    v->BindFramebuffer(GL_FRAMEBUFFER, 0);
    v->ready = true;
    return true;
}

void gi_voxelize_destroy(gi_voxelize_t *v)
{
    if (v == NULL) return;
    if (v->prog) glDeleteProgram(v->prog);
    if (v->fbo && v->DeleteFramebuffers) v->DeleteFramebuffers(1, &v->fbo);
    memset(v, 0, sizeof *v);
}
