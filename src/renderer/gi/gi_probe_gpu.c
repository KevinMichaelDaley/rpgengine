/**
 * @file gi_probe_gpu.c
 * @brief GPU probe update compute (see gi_probe_gpu.h).
 */
#include "ferrum/renderer/gi/gi_probe_gpu.h"

#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 4.3 enums glad (3.3) lacks. */
#define GI_GL_COMPUTE_SHADER            0x91B9
#define GI_GL_SHADER_STORAGE_BUFFER     0x90D2
#define GI_GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GI_GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#define GI_SDF_UNIT_BASE 8   /* SDF resident textures bound to units 8.. */

static const char *CS_SRC =
    "#version 430\n"
    "layout(local_size_x=64) in;\n"
    "uniform int u_nprobes,u_nlights,u_nboxes,u_soft_i;\n"
    "uniform float u_soft;\n"
    "uniform sampler3D u_sdf[8];\n"
    "uniform vec3 u_sdf_origin[8];\n"
    "uniform vec3 u_sdf_dim[8];\n"
    "uniform float u_sdf_vox[8];\n"
    "uniform int u_sdf_active[8];\n"
    "layout(std430,binding=0) readonly buffer PP { vec4 ppos[]; };\n"
    "layout(std430,binding=1) writeonly buffer PS { float psh[]; };\n"
    "layout(std430,binding=2) readonly buffer LB { vec4 lt[]; };\n"
    "layout(std430,binding=3) readonly buffer BX { vec4 bx[]; };\n"
    "float box_sdf(vec3 p,vec3 c,vec3 h){ vec3 q=abs(p-c)-h; return length(max(q,vec3(0.0)))+min(max(q.x,max(q.y,q.z)),0.0); }\n"
    "float scene_sdf(vec3 p){ float d=1e30;\n"
    "  for(int i=0;i<8;++i){ if(u_sdf_active[i]==0) continue;\n"
    "    vec3 g=(p-u_sdf_origin[i])/u_sdf_vox[i];\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_sdf_dim[i]))){\n"
    "      vec3 uvw=(g+0.5)/u_sdf_dim[i]; d=min(d, texture(u_sdf[i],uvw).r); } }\n"
    "  for(int i=0;i<u_nboxes;++i){ d=min(d, box_sdf(p,bx[i*2].xyz,bx[i*2+1].xyz)); }\n"
    "  return d; }\n"
    "float soft_vis(vec3 o,vec3 dir,float maxd){ float res=1.0,t=0.05;\n"
    "  for(int i=0;i<48 && t<maxd;++i){ float h=scene_sdf(o+dir*t);\n"
    "    if(h<0.001) return 0.0; res=min(res,u_soft*h/t); t+=clamp(h,0.02,0.5); }\n"
    "  return clamp(res,0.0,1.0); }\n"
    "void sh_basis(vec3 d, out float y[9]){\n"
    "  y[0]=0.282094792; y[1]=0.488602512*d.y; y[2]=0.488602512*d.z; y[3]=0.488602512*d.x;\n"
    "  y[4]=1.092548431*d.x*d.y; y[5]=1.092548431*d.y*d.z; y[6]=0.315391565*(3.0*d.z*d.z-1.0);\n"
    "  y[7]=1.092548431*d.x*d.z; y[8]=0.546274215*(d.x*d.x-d.y*d.y); }\n"
    "void main(){ uint gid=gl_GlobalInvocationID.x; if(gid>=uint(u_nprobes)) return;\n"
    "  vec3 p=ppos[gid].xyz; float sh[27]; for(int k=0;k<27;++k) sh[k]=0.0;\n"
    "  for(int l=0;l<u_nlights;++l){ vec4 a=lt[l*4],b=lt[l*4+1],c=lt[l*4+2],e=lt[l*4+3];\n"
    "    int kind=int(a.x); vec3 lpos=a.yzw; vec3 ldir=b.xyz; float range=b.w; vec3 col=c.xyz; float ci=c.w, co=e.x;\n"
    "    vec3 dir; float maxd; float atten=1.0;\n"
    "    if(kind==0){ float ll=length(ldir); if(ll<1e-6) continue; dir=-ldir/ll; maxd=1e4; }\n"
    "    else { vec3 to=lpos-p; float dd=length(to); if(dd<1e-5) continue; dir=to/dd; maxd=dd;\n"
    "      float x=dd/max(range,1e-4); if(x>=1.0) continue; float f=1.0-x*x; atten=f*f;\n"
    "      if(kind==2){ float ll=length(ldir); if(ll<1e-6) continue; float cd=dot(-dir,ldir)/ll;\n"
    "        float t=(cd-co)/(ci-co+1e-6); if(t<=0.0) continue; t=clamp(t,0.0,1.0); atten*=t*t*(3.0-2.0*t); } }\n"
    "    float vis=soft_vis(p,dir,maxd); if(vis<=0.0) continue; float s=vis*atten;\n"
    "    float y[9]; sh_basis(dir,y);\n"
    "    for(int k=0;k<9;++k){ sh[k]+=col.r*s*y[k]; sh[9+k]+=col.g*s*y[k]; sh[18+k]+=col.b*s*y[k]; } }\n"
    "  for(int k=0;k<27;++k) psh[gid*27+k]=sh[k]; }\n";

static GLuint cs_build(void)
{
    GLuint sh = glCreateShader(GI_GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &CS_SRC, NULL); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(sh, sizeof log, NULL, log);
               fprintf(stderr, "gi_probe_gpu compile:\n%s\n", log); glDeleteShader(sh); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, sh); glLinkProgram(p); glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, NULL, log);
               fprintf(stderr, "gi_probe_gpu link:\n%s\n", log); return 0; }
    return p;
}

bool gi_probe_gpu_init(gi_probe_gpu_t *g, const gl_loader_t *loader,
                       uint32_t max_probes, uint32_t max_lights, uint32_t max_boxes)
{
    if (g == NULL || loader == NULL || loader->get_proc_address == NULL)
        return false;
    memset(g, 0, sizeof *g);
    void *dc = loader->get_proc_address("glDispatchCompute", loader->user_data);
    void *mb = loader->get_proc_address("glMemoryBarrier", loader->user_data);
    if (dc == NULL || mb == NULL) { fprintf(stderr, "gi_probe_gpu: no compute (need GL 4.3)\n"); return false; }
    memcpy(&g->DispatchCompute, &dc, sizeof dc);
    memcpy(&g->MemoryBarrier, &mb, sizeof mb);

    g->prog = cs_build();
    if (g->prog == 0) return false;
    g->max_lights = max_lights ? max_lights : 1u;
    g->max_boxes = max_boxes ? max_boxes : 1u;

    glGenBuffers(1, &g->b_pos);
    glGenBuffers(1, &g->b_sh);
    glGenBuffers(1, &g->b_lights);
    glGenBuffers(1, &g->b_boxes);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_pos);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_sh);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 27 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_lights);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)g->max_lights * 4 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_boxes);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)g->max_boxes * 2 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

    /* Texture buffers so the forward+ material can sample probe SH + positions. */
    glGenTextures(1, &g->tbo_sh_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_sh_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, g->b_sh);
    glGenTextures(1, &g->tbo_pos_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_pos_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g->b_pos);

    g->ready = true;
    return true;
}

void gi_probe_gpu_set_probes(gi_probe_gpu_t *g, const float *pos, uint32_t n)
{
    if (g == NULL || !g->ready || pos == NULL) return;
    g->n_probes = n;
    /* Pack xyz -> vec4 (w unused). */
    float *tmp = malloc((size_t)n * 4 * sizeof(float));
    if (tmp == NULL) return;
    for (uint32_t i = 0; i < n; ++i) {
        tmp[i*4+0] = pos[i*3+0]; tmp[i*4+1] = pos[i*3+1];
        tmp[i*4+2] = pos[i*3+2]; tmp[i*4+3] = 0.0f;
    }
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_pos);
    glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)n * 4 * sizeof(float), tmp);
    free(tmp);
}

void gi_probe_gpu_dispatch(gi_probe_gpu_t *g, const gi_sdf_stream_t *sdf,
                           const gi_light_t *lights, uint32_t n_lights,
                           const gi_collider_t *boxes, uint32_t n_boxes,
                           float soft_k)
{
    if (g == NULL || !g->ready || g->n_probes == 0) return;
    if (n_lights > g->max_lights) n_lights = g->max_lights;
    if (n_boxes > g->max_boxes) n_boxes = g->max_boxes;

    /* Pack lights: 4 vec4/light. */
    float *lb = malloc((size_t)(n_lights ? n_lights : 1) * 16 * sizeof(float));
    for (uint32_t l = 0; l < n_lights; ++l) {
        const gi_light_t *L = &lights[l]; float *o = &lb[l*16];
        o[0]=(float)L->kind; o[1]=L->pos[0]; o[2]=L->pos[1]; o[3]=L->pos[2];
        o[4]=L->dir[0]; o[5]=L->dir[1]; o[6]=L->dir[2]; o[7]=L->range;
        o[8]=L->color[0]; o[9]=L->color[1]; o[10]=L->color[2]; o[11]=L->cos_inner;
        o[12]=L->cos_outer; o[13]=0; o[14]=0; o[15]=0;
    }
    if (n_lights) { glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_lights);
        glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)n_lights*16*sizeof(float), lb); }
    free(lb);

    /* Pack boxes: 2 vec4/box (centre, half). Spheres -> box of half=radius. */
    float *bb = malloc((size_t)(n_boxes ? n_boxes : 1) * 8 * sizeof(float));
    uint32_t nb = 0;
    for (uint32_t i = 0; i < n_boxes; ++i) {
        const gi_collider_t *c = &boxes[i]; float *o = &bb[nb*8];
        o[0]=c->a[0]; o[1]=c->a[1]; o[2]=c->a[2]; o[3]=0;
        if (c->kind == GI_COLLIDER_BOX) { o[4]=c->ext[0]; o[5]=c->ext[1]; o[6]=c->ext[2]; }
        else { o[4]=c->ext[0]; o[5]=c->ext[0]; o[6]=c->ext[0]; } /* sphere approx. */
        o[7]=0; ++nb;
    }
    if (nb) { glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_boxes);
        glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)nb*8*sizeof(float), bb); }
    free(bb);

    glUseProgram(g->prog);
    glUniform1i(glGetUniformLocation(g->prog, "u_nprobes"), (GLint)g->n_probes);
    glUniform1i(glGetUniformLocation(g->prog, "u_nlights"), (GLint)n_lights);
    glUniform1i(glGetUniformLocation(g->prog, "u_nboxes"), (GLint)nb);
    glUniform1f(glGetUniformLocation(g->prog, "u_soft"), soft_k);

    /* Bind the resident SDF chunks (one per used slot) + metadata. */
    char nm[32];
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i) {
        int active = (sdf != NULL && sdf->slot_chunk[i] >= 0) ? 1 : 0;
        snprintf(nm, sizeof nm, "u_sdf_active[%d]", i);
        glUniform1i(glGetUniformLocation(g->prog, nm), active);
        snprintf(nm, sizeof nm, "u_sdf[%d]", i);
        glUniform1i(glGetUniformLocation(g->prog, nm), GI_SDF_UNIT_BASE + i);
        glActiveTexture(GL_TEXTURE0 + (GLenum)(GI_SDF_UNIT_BASE + i));
        glBindTexture(GL_TEXTURE_3D, active ? sdf->tex[i] : 0);
        if (active) {
            const gi_sdf_chunk_ram_t *r = &sdf->ram[sdf->slot_chunk[i]];
            float o3[3] = { r->origin[0], r->origin[1], r->origin[2] };
            float d3[3] = { (float)r->dims[0], (float)r->dims[1], (float)r->dims[2] };
            snprintf(nm, sizeof nm, "u_sdf_origin[%d]", i);
            glUniform3fv(glGetUniformLocation(g->prog, nm), 1, o3);
            snprintf(nm, sizeof nm, "u_sdf_dim[%d]", i);
            glUniform3fv(glGetUniformLocation(g->prog, nm), 1, d3);
            snprintf(nm, sizeof nm, "u_sdf_vox[%d]", i);
            glUniform1f(glGetUniformLocation(g->prog, nm), r->voxel);
        }
    }

    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 0, g->b_pos);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 1, g->b_sh);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 2, g->b_lights);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 3, g->b_boxes);

    g->DispatchCompute((g->n_probes + 63u) / 64u, 1u, 1u);
    g->MemoryBarrier(GI_GL_SHADER_STORAGE_BARRIER_BIT | GI_GL_TEXTURE_FETCH_BARRIER_BIT);
}

unsigned int gi_probe_gpu_sh_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_sh_tex : 0u; }
unsigned int gi_probe_gpu_pos_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_pos_tex : 0u; }

void gi_probe_gpu_destroy(gi_probe_gpu_t *g)
{
    if (g == NULL) return;
    if (g->prog) glDeleteProgram(g->prog);
    GLuint b[4] = { g->b_pos, g->b_sh, g->b_lights, g->b_boxes };
    glDeleteBuffers(4, b);
    if (g->tbo_sh_tex) glDeleteTextures(1, &g->tbo_sh_tex);
    if (g->tbo_pos_tex) glDeleteTextures(1, &g->tbo_pos_tex);
    memset(g, 0, sizeof *g);
}
