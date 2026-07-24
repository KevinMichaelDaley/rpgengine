/**
 * @file shadow_caustics_init.c
 * @brief Caustics compute setup: program compilation, accumulator + map
 *        allocation, SDF chunk registration (see shadow_caustics.h).
 */
#include "ferrum/renderer/shadow_caustics.h"

#include <glad/glad.h>
#include <stdio.h>
#include <string.h>

/* 4.2/4.3 enums glad (3.3) lacks. */
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif

/* Fixed-point splat scale: energy per texel stays far below 2^32/65536. */
#define SC_SCALE "65536.0"

/* Trace pass: one invocation per mask texel. Reconstruct the translucent
 * surface point along this texel's light ray, fire u_samples jittered rays,
 * march the resident SDF chunks, splat alpha*tint where each ray lands. */
static const char *const CS_TRACE =
    "#version 430\n"
    "layout(local_size_x=8, local_size_y=8) in;\n"
    "layout(r32ui, binding=0) uniform coherent uimage2DArray u_accum;\n"
    "uniform sampler2DArray u_mask_color;\n"   /* rgb tint, a coverage. */
    "uniform sampler2DArray u_mask_depth;\n"   /* r = |p - eye| / far. */
    "uniform mat4 u_vp;\n"
    "uniform mat4 u_inv_vp;\n"
    "uniform vec3 u_eye;\n"
    "uniform float u_far;\n"
    "uniform int u_res;\n"
    "uniform int u_samples;\n"
    "uniform float u_scatter;\n"
    "uniform float u_scatter_dist;\n"
    "uniform float u_max_dist;\n"
    "uniform int u_cascade;\n"
    "uniform int u_seed;\n"
    "uniform int u_mask_layer;\n"
    "uniform int u_sdf_count;\n"
    "uniform sampler3D u_sdf[16];\n"
    "uniform vec3 u_sdf_origin[16];\n"
    "uniform vec3 u_sdf_dim[16];\n"            /* cells per axis. */
    "uniform float u_sdf_vox[16];\n"
    "uniform sampler3D u_zone;\n"
    "uniform int u_zone_on;\n"
    "uniform vec3 u_zone_origin;\n"
    "uniform vec3 u_zone_dim;\n"
    "uniform float u_zone_vox;\n"
    /* Union of the resident chunk SDFs, with the GI trace's page-fault
     * semantics: where NO fine chunk covers the point, the coarse global zone
     * field keeps occlusion alive; outside both, +big so rays in truly empty
     * space march straight to u_max_dist. */
    "float scene_sdf(vec3 p){ float d=1e30; bool cov=false;\n"
    "  for(int i=0;i<u_sdf_count;++i){\n"
    "    vec3 g=(p-u_sdf_origin[i])/u_sdf_vox[i];\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_sdf_dim[i]))){\n"
    "      cov=true; vec3 uvw=(g+0.5)/u_sdf_dim[i]; d=min(d, texture(u_sdf[i],uvw).a); } }\n"
    "  if(!cov && u_zone_on!=0){ vec3 g=(p-u_zone_origin)/u_zone_vox;\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_zone_dim))){\n"
    "      vec3 uvw=(g+0.5)/u_zone_dim; d=min(d, texture(u_zone,uvw).a); } }\n"
    "  return d; }\n"
    "float rnd(inout uint s){ s=s*747796405u+2891336453u;\n"
    "  uint w=((s>>((s>>28u)+4u))^s)*277803737u; return float((w>>22u)^w)*(1.0/4294967296.0); }\n"
    "void main(){\n"
    "  ivec2 px=ivec2(gl_GlobalInvocationID.xy);\n"
    "  if(px.x>=u_res||px.y>=u_res) return;\n"
    "  vec2 uv=(vec2(px)+0.5)/float(u_res);\n"
    "  vec4 m=texture(u_mask_color, vec3(uv, float(u_mask_layer)));\n"
    "  if(m.a<0.004) return;\n"
    "  float D=texture(u_mask_depth, vec3(uv, float(u_mask_layer))).r*u_far;\n"
    /* Texel light ray from the (ortho) light matrix: unproject the near and\n"
     * far NDC points, then place the surface at the CSM's SPHERICAL distance\n"
     * D from the virtual eye (solve |p0 + t*dir - eye| = D). */
    "  vec2 ndc=uv*2.0-1.0;\n"
    "  vec4 pn4=u_inv_vp*vec4(ndc,-1.0,1.0); vec3 p0=pn4.xyz/pn4.w;\n"
    "  vec4 pf4=u_inv_vp*vec4(ndc, 1.0,1.0); vec3 p1=pf4.xyz/pf4.w;\n"
    "  vec3 dir=normalize(p1-p0);\n"
    "  vec3 oe=p0-u_eye;\n"
    "  float b=dot(dir,oe); float cq=dot(oe,oe)-D*D;\n"
    "  float disc=max(b*b-cq,0.0);\n"
    "  float t0=-b+sqrt(disc);\n"                       /* far root: along +dir. */
    "  vec3 origin=p0+dir*t0;\n"
    /* Orthonormal frame around the ray for the scatter disk. */
    "  vec3 up=abs(dir.y)<0.99?vec3(0,1,0):vec3(1,0,0);\n"
    "  vec3 tx=normalize(cross(up,dir)); vec3 ty=cross(dir,tx);\n"
    "  uint seed=uint(px.x)*7919u ^ uint(px.y)*104729u ^ uint(u_seed)*2654435761u ^ 0x9e3779b9u;\n"
    "  float k=(u_scatter_dist>0.0)?(u_scatter/u_scatter_dist):0.0;\n"
    /* Mask rgb is PREMULTIPLIED transmission (coverage * tint * transmitted\n"
     * fraction) since the stained-glass fix -- it IS the energy to splat. */
    "  uint eR=uint(m.r*" SC_SCALE "/float(u_samples)+0.5);\n"
    "  uint eG=uint(m.g*" SC_SCALE "/float(u_samples)+0.5);\n"
    "  uint eB=uint(m.b*" SC_SCALE "/float(u_samples)+0.5);\n"
    "  for(int s=0;s<u_samples;++s){\n"
    /* Uniform disk jitter scaled so radius u_scatter is reached at\n"
     * u_scatter_dist metres along the ray. */
    "    float a=rnd(seed)*6.2831853; float r=sqrt(rnd(seed))*k;\n"
    "    vec3 d2=normalize(dir + (tx*cos(a)+ty*sin(a))*r);\n"
    /* Sphere-trace the SDF; land at the first surface or at u_max_dist. */
    "    float t=0.05;\n"
    "    for(int i=0;i<64&&t<u_max_dist;++i){\n"
    "      float h=scene_sdf(origin+d2*t);\n"
    "      if(h<0.05) break;\n"
    "      t+=clamp(h,0.08,6.0);\n"
    "    }\n"
    "    vec3 land=origin+d2*min(t,u_max_dist);\n"
    /* Splat into the light-space map; CLAMP inside so no energy is lost. */
    "    vec4 lc=u_vp*vec4(land,1.0);\n"
    "    vec2 luv=clamp(lc.xy/lc.w*0.5+0.5, 0.0, 0.999999);\n"
    "    ivec2 lp=ivec2(luv*float(u_res));\n"
    "    imageAtomicAdd(u_accum, ivec3(lp, u_cascade*3+0), eR);\n"
    "    imageAtomicAdd(u_accum, ivec3(lp, u_cascade*3+1), eG);\n"
    "    imageAtomicAdd(u_accum, ivec3(lp, u_cascade*3+2), eB);\n"
    "  }\n"
    "}\n";

/* Resolve pass: mode 0 clears the cascade's accumulator layers; mode 1
 * converts fixed point -> the filterable RGBA16F map layer. */
static const char *const CS_RESOLVE =
    "#version 430\n"
    "layout(local_size_x=8, local_size_y=8) in;\n"
    "layout(r32ui, binding=0) uniform coherent uimage2DArray u_accum;\n"
    "layout(rgba16f, binding=1) uniform image2DArray u_map;\n"
    "uniform int u_mode;\n"
    "uniform int u_cascade;\n"
    "uniform int u_res;\n"
    "void main(){\n"
    "  ivec2 px=ivec2(gl_GlobalInvocationID.xy);\n"
    "  if(px.x>=u_res||px.y>=u_res) return;\n"
    "  if(u_mode==0){\n"
    "    imageStore(u_accum, ivec3(px, u_cascade*3+0), uvec4(0u));\n"
    "    imageStore(u_accum, ivec3(px, u_cascade*3+1), uvec4(0u));\n"
    "    imageStore(u_accum, ivec3(px, u_cascade*3+2), uvec4(0u));\n"
    "  } else {\n"
    "    vec3 e=vec3(float(imageLoad(u_accum, ivec3(px, u_cascade*3+0)).r),\n"
    "                float(imageLoad(u_accum, ivec3(px, u_cascade*3+1)).r),\n"
    "                float(imageLoad(u_accum, ivec3(px, u_cascade*3+2)).r))\n"
    "           /" SC_SCALE ";\n"
    "    imageStore(u_map, ivec3(px, u_cascade), vec4(e, 1.0));\n"
    "  }\n"
    "}\n";

/* Compile + link one compute program; 0 on failure (message to stderr). */
static GLuint sc_build(const char *src, const char *tag)
{
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    if (sh == 0)
        return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "shadow_caustics %s compile:\n%s\n", tag, log);
        glDeleteShader(sh);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, sh);
    glLinkProgram(p);
    glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "shadow_caustics %s link:\n%s\n", tag, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool shadow_caustics_init(shadow_caustics_t *c,
                          const shadow_caustics_config_t *config)
{
    if (c == NULL)
        return false;
    memset(c, 0, sizeof *c);
    if (config == NULL || config->loader == NULL ||
        config->loader->get_proc_address == NULL || config->resolution == 0u ||
        config->cascades == 0u || config->cascades > 8u)
        return false;
    const gl_loader_t *loader = config->loader;
    void *dc = loader->get_proc_address("glDispatchCompute", loader->user_data);
    void *mb = loader->get_proc_address("glMemoryBarrier", loader->user_data);
    void *bi = loader->get_proc_address("glBindImageTexture", loader->user_data);
    if (dc == NULL || mb == NULL || bi == NULL) {
        fprintf(stderr, "shadow_caustics: no compute (need GL 4.3)\n");
        return false;
    }
    memcpy(&c->DispatchCompute, &dc, sizeof dc);
    memcpy(&c->MemoryBarrier, &mb, sizeof mb);
    memcpy(&c->BindImageTexture, &bi, sizeof bi);

    c->prog_trace = sc_build(CS_TRACE, "trace");
    c->prog_resolve = sc_build(CS_RESOLVE, "resolve");
    if (c->prog_trace == 0 || c->prog_resolve == 0) {
        shadow_caustics_destroy(c);
        return false;
    }

    c->resolution = config->resolution;
    c->cascades = config->cascades;
    c->samples = config->samples ? config->samples : 8u;
    c->scatter = config->scatter;
    c->scatter_dist = config->scatter_dist > 0.0f ? config->scatter_dist : 1.0f;
    c->max_dist = config->max_dist > 0.0f ? config->max_dist : 64.0f;

    /* Fixed-point accumulator: 3 channel layers per cascade. */
    glGenTextures(1, &c->accum_tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, c->accum_tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R32UI, (GLsizei)c->resolution,
                 (GLsizei)c->resolution, (GLsizei)(c->cascades * 3u), 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* Resolved filterable map: one layer per cascade. */
    glGenTextures(1, &c->map_tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, c->map_tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, (GLsizei)c->resolution,
                 (GLsizei)c->resolution, (GLsizei)c->cascades, 0, GL_RGBA,
                 GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Cache uniform locations once. */
    {
        GLuint p = c->prog_trace;
        char nm[32];
        c->loc.vp = glGetUniformLocation(p, "u_vp");
        c->loc.inv_vp = glGetUniformLocation(p, "u_inv_vp");
        c->loc.eye = glGetUniformLocation(p, "u_eye");
        c->loc.far = glGetUniformLocation(p, "u_far");
        c->loc.res = glGetUniformLocation(p, "u_res");
        c->loc.samples = glGetUniformLocation(p, "u_samples");
        c->loc.scatter = glGetUniformLocation(p, "u_scatter");
        c->loc.scatter_dist = glGetUniformLocation(p, "u_scatter_dist");
        c->loc.max_dist = glGetUniformLocation(p, "u_max_dist");
        c->loc.cascade = glGetUniformLocation(p, "u_cascade");
        c->loc.seed = glGetUniformLocation(p, "u_seed");
        c->loc.mask_layer = glGetUniformLocation(p, "u_mask_layer");
        c->loc.sdf_count = glGetUniformLocation(p, "u_sdf_count");
        for (int i = 0; i < SHADOW_CAUSTICS_MAX_SDF; ++i) {
            snprintf(nm, sizeof nm, "u_sdf[%d]", i);
            c->loc.sdf[i] = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf_origin[%d]", i);
            c->loc.sdf_origin[i] = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf_dim[%d]", i);
            c->loc.sdf_dim[i] = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf_vox[%d]", i);
            c->loc.sdf_vox[i] = glGetUniformLocation(p, nm);
        }
        c->loc.zone = glGetUniformLocation(p, "u_zone");
        c->loc.zone_on = glGetUniformLocation(p, "u_zone_on");
        c->loc.zone_origin = glGetUniformLocation(p, "u_zone_origin");
        c->loc.zone_dim = glGetUniformLocation(p, "u_zone_dim");
        c->loc.zone_vox = glGetUniformLocation(p, "u_zone_vox");
        c->loc.rz_mode = glGetUniformLocation(c->prog_resolve, "u_mode");
        c->loc.rz_cascade = glGetUniformLocation(c->prog_resolve, "u_cascade");
        c->loc.rz_res = glGetUniformLocation(c->prog_resolve, "u_res");
    }
    return true;
}

void shadow_caustics_destroy(shadow_caustics_t *c)
{
    if (c == NULL)
        return;
    if (c->prog_trace)
        glDeleteProgram(c->prog_trace);
    if (c->prog_resolve)
        glDeleteProgram(c->prog_resolve);
    if (c->accum_tex)
        glDeleteTextures(1, &c->accum_tex);
    if (c->map_tex)
        glDeleteTextures(1, &c->map_tex);
    memset(c, 0, sizeof *c);
}

void shadow_caustics_set_sdf(shadow_caustics_t *c, const uint32_t *textures,
                             const float (*origins)[3], const float (*dims)[3],
                             const float *voxels, uint32_t count)
{
    if (c == NULL)
        return;
    if (textures == NULL || origins == NULL || dims == NULL || voxels == NULL)
        count = 0;
    if (count > SHADOW_CAUSTICS_MAX_SDF)
        count = SHADOW_CAUSTICS_MAX_SDF;
    for (uint32_t i = 0; i < count; ++i) {
        c->sdf_tex[i] = textures[i];
        memcpy(c->sdf_origin[i], origins[i], sizeof c->sdf_origin[i]);
        memcpy(c->sdf_dim[i], dims[i], sizeof c->sdf_dim[i]);
        c->sdf_vox[i] = voxels[i];
    }
    c->sdf_count = count;
}

void shadow_caustics_set_zone(shadow_caustics_t *c, uint32_t tex,
                              const float origin[3], const float dims[3],
                              float voxel)
{
    if (c == NULL)
        return;
    if (tex == 0u || origin == NULL || dims == NULL || voxel <= 0.0f) {
        c->zone_tex = 0u;
        return;
    }
    c->zone_tex = tex;
    memcpy(c->zone_origin, origin, sizeof c->zone_origin);
    memcpy(c->zone_dim, dims, sizeof c->zone_dim);
    c->zone_vox = voxel;
}
