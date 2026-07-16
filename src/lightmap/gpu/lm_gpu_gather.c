/**
 * @file lm_gpu_gather.c
 * @brief GPU GI gather pipeline (see lm_gpu_gather.h). SVO -> occupancy -> JFA
 *        SDF -> pack -> gather -> SH readback, GL 4.3 loaded via a gl_loader_t.
 */
#define _POSIX_C_SOURCE 199309L /* nanosleep + struct timespec under -std=c11 */
#include "ferrum/lightmap/gpu/lm_gpu_gather.h"

#include <time.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/lightmap/gpu/lm_gpu_pack.h"

/* ── GL constants (no glad; headless lib) ── */
#define GL_COMPUTE_SHADER            0x91B9
#define GL_SHADER_STORAGE_BUFFER     0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_STATIC_DRAW               0x88E4
#define GL_DYNAMIC_COPY              0x88EA

typedef unsigned int  GLenum, GLuint, GLbitfield;
typedef int           GLint, GLsizei;
typedef char          GLchar;
typedef float         GLfloat;
typedef intptr_t      GLintptr;
typedef ptrdiff_t     GLsizeiptr;

/* ── File-static GL entry points + programs ── */
static struct {
    GLuint (*CreateShader)(GLenum);
    void   (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void   (*CompileShader)(GLuint);
    void   (*GetShaderiv)(GLuint, GLenum, GLint *);
    void   (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    GLuint (*CreateProgram)(void);
    void   (*AttachShader)(GLuint, GLuint);
    void   (*LinkProgram)(GLuint);
    void   (*GetProgramiv)(GLuint, GLenum, GLint *);
    void   (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void   (*DeleteShader)(GLuint);
    void   (*DeleteProgram)(GLuint);
    void   (*UseProgram)(GLuint);
    GLint  (*GetUniformLocation)(GLuint, const GLchar *);
    void   (*Uniform1i)(GLint, GLint);
    void   (*Uniform1ui)(GLint, GLuint);
    void   (*Uniform1f)(GLint, GLfloat);
    void   (*Uniform3fv)(GLint, GLsizei, const GLfloat *);
    void   (*Uniform3iv)(GLint, GLsizei, const GLint *);
    void   (*GenBuffers)(GLsizei, GLuint *);
    void   (*DeleteBuffers)(GLsizei, const GLuint *);
    void   (*BindBuffer)(GLenum, GLuint);
    void   (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void   (*GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *);
    void   (*BindBufferBase)(GLenum, GLuint, GLuint);
    void   (*DispatchCompute)(GLuint, GLuint, GLuint);
    void   (*MemoryBarrier)(GLbitfield);
    void   (*Finish)(void);
} gl;
static GLuint g_init, g_step, g_fin, g_gather, g_lightvis;
static bool g_ready;

#define GG_LOAD(field, name) do { \
    void *p_ = loader->get_proc_address((name), loader->user_data); \
    if (p_ == NULL) { fprintf(stderr, "lm_gpu_gather: missing %s\n", name); return false; } \
    memcpy(&gl.field, &p_, sizeof p_); } while (0)

static GLuint gg_compile(const char *src) {
    GLuint sh = gl.CreateShader(GL_COMPUTE_SHADER);
    gl.ShaderSource(sh, 1, &src, NULL); gl.CompileShader(sh);
    GLint ok = 0; gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; gl.GetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_gather compile:\n%s\n", log); return 0; }
    GLuint p = gl.CreateProgram(); gl.AttachShader(p, sh); gl.LinkProgram(p); gl.DeleteShader(sh);
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; gl.GetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_gather link:\n%s\n", log); return 0; }
    return p;
}

/* ── Shaders (validated in tests/visual/lm_jfa_sdf.c + lm_gather_gpu.c) ── */
#define JFA_COMMON \
    "layout(local_size_x=4,local_size_y=4,local_size_z=4) in;\n" \
    "uniform ivec3 dims;\n" \
    "int lidx(ivec3 c){ return (c.z*dims.y+c.y)*dims.x+c.x; }\n" \
    "ivec3 lcoord(int i){ int x=i%dims.x; int y=(i/dims.x)%dims.y; int z=i/(dims.x*dims.y); return ivec3(x,y,z); }\n"

static const char *CS_JFA_INIT =
    "#version 430\n" JFA_COMMON
    "layout(std430,binding=0) readonly buffer Occ { uint occ[]; };\n"
    "layout(std430,binding=1) writeonly buffer Seed { int seed[]; };\n"
    "bool solid(ivec3 c){ if(any(lessThan(c,ivec3(0)))||any(greaterThanEqual(c,dims))) return false; return occ[lidx(c)]!=0u; }\n"
    /* Seed only AIR cells that touch solid. The SDF is then distance to the\n"
     * air-side boundary: solid cells resolve strictly negative (>= a voxel), air\n"
     * >= 0, so the gather's `sdf < 0` hit test catches even single-voxel walls. */
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int i=lidx(c); bool s=occ[i]!=0u; bool surf=false;\n"
    "  ivec3 o[6]=ivec3[6](ivec3(1,0,0),ivec3(-1,0,0),ivec3(0,1,0),ivec3(0,-1,0),ivec3(0,0,1),ivec3(0,0,-1));\n"
    "  if(!s){ for(int k=0;k<6;++k){ if(solid(c+o[k])){ surf=true; break; } } } seed[i]=surf?i:-1; }\n";
static const char *CS_JFA_STEP =
    "#version 430\n" JFA_COMMON "uniform int step;\n"
    "layout(std430,binding=0) readonly buffer SeedIn { int sin[]; };\n"
    "layout(std430,binding=1) writeonly buffer SeedOut { int sout[]; };\n"
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int i=lidx(c); int best=sin[i]; float bd=best>=0?distance(vec3(c),vec3(lcoord(best))):1e30;\n"
    "  for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){\n"
    "    ivec3 n=c+ivec3(dx,dy,dz)*step; if(any(lessThan(n,ivec3(0)))||any(greaterThanEqual(n,dims))) continue;\n"
    "    int s=sin[lidx(n)]; if(s<0) continue; float d=distance(vec3(c),vec3(lcoord(s))); if(d<bd){bd=d;best=s;} }\n"
    "  sout[i]=best; }\n";
static const char *CS_JFA_FIN =
    "#version 430\n" JFA_COMMON "uniform float voxel;\n"
    "layout(std430,binding=0) readonly buffer SeedIn { int sin[]; };\n"
    "layout(std430,binding=1) readonly buffer Occ { uint occ[]; };\n"
    "layout(std430,binding=2) writeonly buffer Dist { float dist[]; };\n"
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int i=lidx(c); int se=sin[i]; float d=se>=0?distance(vec3(c),vec3(lcoord(se)))*voxel:1e30;\n"
    "  dist[i]=occ[i]!=0u?-d:d; }\n";

/* Per-cell, per-light visibility (rpg-yfa4). For each near-grid cell, cast a
 * shadow ray through the coarse SDF to every static light and store 1 (lit) or 0
 * (occluded). Directional lights march to the region edge; point/spot to the
 * light. Computed ONCE per bake region, then read by the gather's direct(). */
static const char *CS_LIGHTVIS =
    "#version 430\n"
    "layout(local_size_x=4,local_size_y=4,local_size_z=4) in;\n"
    "layout(std430,binding=0) readonly buffer Sdf { float sdf[]; };\n"
    "layout(std430,binding=1) writeonly buffer Vis { float vis[]; };\n"
    "layout(std430,binding=2) readonly buffer Lit { vec4 lit[]; };\n"
    "uniform ivec3 dims; uniform float voxel; uniform vec3 origin; uniform int nlights;\n"
    "int cidx(ivec3 c){ return (c.z*dims.y+c.y)*dims.x+c.x; }\n"
    "bool inb(ivec3 c){ return all(greaterThanEqual(c,ivec3(0)))&&all(lessThan(c,dims)); }\n"
    /* SDF sphere-march from p toward dir; true if it enters solid before maxT or\n"
     * the region edge (edge/exhaustion => reached the light unoccluded). */
    "bool occluded(vec3 p, vec3 dir, float maxT){ float t=voxel*1.5;\n"
    "  for(int k=0;k<512 && t<maxT;++k){ vec3 q=p+dir*t; ivec3 c=ivec3(floor((q-origin)/voxel));\n"
    "    if(!inb(c)) return false; float sd=sdf[cidx(c)]; if(sd<0.0) return true;\n"
    "    t += max(sd, voxel); } return false; }\n"
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int ci=cidx(c); vec3 p=origin+(vec3(c)+0.5)*voxel;\n"
    "  for(int i=0;i<nlights;++i){ int kind=int(lit[4*i].w); vec3 lp=lit[4*i].xyz; vec3 ld=lit[4*i+1].xyz;\n"
    "    vec3 toL; float maxT;\n"
    "    if(kind==1){ toL=normalize(-ld); maxT=1e5; }\n"
    "    else { vec3 dd=lp-p; float dist=length(dd); if(dist<1e-4){ vis[ci*nlights+i]=1.0; continue; } toL=dd/dist; maxT=dist; }\n"
    "    vis[ci*nlights+i] = occluded(p,toL,maxT) ? 0.0 : 1.0; } }\n";

static const char *CS_GATHER =
    "#version 430\n"
    "layout(local_size_x=64) in;\n"
    "layout(std430,binding=0) readonly buffer Sdf { float sdf[]; };\n"
    "layout(std430,binding=2) readonly buffer Lux { vec4 lux[]; };\n"
    /* Not 'writeonly': NVIDIA returns zero from glGetBufferSubData on a\n"
     * write-only-qualified SSBO. Plain buffer reads back correctly. */
    "layout(std430,binding=3) buffer Out { float osh[]; };\n"
    "layout(std430,binding=4) readonly buffer Lit { vec4 lit[]; };\n"
    "struct Node { uint children[8]; vec4 diffuse; vec4 emissive; };\n"
    "layout(std430,binding=5) readonly buffer Svo { Node nodes[]; };\n"
    /* Coarser MEDIUM + FAR distance fields (rpg-fzht) a chunk ray marches into\n"
     * once it leaves its (small) near box, so distant geometry occludes/reflects\n"
     * instead of the ray escaping straight to sky. MEDIUM is a per-chunk ~3x box;\n"
     * FAR is a coarse field shared by a neighbourhood of chunks. Either may be\n"
     * absent (hasMed/hasFar == 0). */
    "layout(std430,binding=6) readonly buffer FarSdf { float fsdf[]; };\n"
    "layout(std430,binding=7) readonly buffer MedSdf { float msdf[]; };\n"
    /* Precomputed per-cell, per-LIGHT visibility store over the near grid\n"
     * (rpg-yfa4): lightvis[cell*nlights + i] = 1 if static light i reaches this\n"
     * cell, else 0 (shadowed). Cast once per cell to every static directional /\n"
     * point / spot light at bake time, so the gather reads it at a bounce hit\n"
     * instead of firing a per-hit shadow ray (noisy + coarse-SDF inaccurate). */
    "layout(std430,binding=8) readonly buffer LightVis { float lightvis[]; };\n"
    "uniform ivec3 dims; uniform float voxel; uniform vec3 origin; uniform vec3 sky;\n"
    "uniform int samples; uniform int bounces; uniform uint seed; uniform int nlux; uniform int nlights;\n"
    "uniform float transition; uniform float finevox;\n"
    "uniform vec3 svomin; uniform vec3 svomax; uniform int svodepth; uniform int nodeCount;\n"
    "uniform int hasMed; uniform ivec3 mdims; uniform vec3 morigin; uniform float mvoxel;\n"
    "uniform int hasFar; uniform ivec3 fdims; uniform vec3 forigin; uniform float fvoxel;\n"
    "const float TAU=6.28318530718, INVPI=0.31830988618, PI=3.14159265359, HALFPI=1.5707963268;\n"
    "uint pcg(inout uint s){ s=s*747796405u+2891336453u; uint w=((s>>((s>>28)+4u))^s)*277803737u; return (w>>22)^w; }\n"
    "float rnd(inout uint s){ return float(pcg(s))*(1.0/4294967296.0); }\n"
    /* Machine-INDEPENDENT sin/cos: hardware sin/cos/normalize differ across GPUs\n"
     * (Intel Xe vs NVIDIA), and in an enclosed scene the path tracer amplifies a\n"
     * sub-ULP ray-direction difference into a flipped grazing hit -> per-luxel\n"
     * splotches. A `precise` minimax polynomial on a deterministic range reduction\n"
     * (floor, not the impl-defined round) evaluates bit-identically on both. */
    /* `precise` on a LOCAL (not the return type -- Mesa rejects that) prevents\n"
     * FMA contraction so the polynomial evaluates the same on every GPU. */
    "float dsin(float x){ float k=floor(x*(1.0/TAU)+0.5); x=x-TAU*k; float x2=x*x;\n"
    "  precise float r = x*(0.9999966+x2*(-0.16664824+x2*(0.00830629+x2*(-0.00018363)))); return r; }\n"
    "float dcos(float x){ return dsin(x+HALFPI); }\n"
    /* IEEE sqrt is correctly-rounded (bit-identical); rsqrt/normalize is not. */
    "vec3 dnorm(vec3 v){ float m=dot(v,v); precise vec3 r = m>0.0 ? v*(1.0/sqrt(m)) : v; return r; }\n"
    "int cidx(ivec3 c){ return (c.z*dims.y+c.y)*dims.x+c.x; }\n"
    "bool inb(ivec3 c){ return all(greaterThanEqual(c,ivec3(0)))&&all(lessThan(c,dims)); }\n"
    "ivec3 toCell(vec3 p){ return ivec3(floor((p-origin)/voxel)); }\n"
    /* Multi-level SDF sample: the fine per-chunk NEAR field where it covers p,\n"
     * else the per-chunk MEDIUM field, else the shared coarse FAR field, else\n"
     * outside all fields. Returns the signed distance and reports the level used\n"
     * (0 near, 1 medium, 2 far, -1 none) so the tracer refines against the SVO\n"
     * only in the fine near field and does plain coarse hits further out. */
    "float mSDF(vec3 p, out int lvl){\n"
    "  ivec3 c=toCell(p);\n"
    "  if(inb(c)){ lvl=0; return sdf[cidx(c)]; }\n"
    "  if(hasMed==1){ ivec3 mc=ivec3(floor((p-morigin)/mvoxel));\n"
    "    if(all(greaterThanEqual(mc,ivec3(0)))&&all(lessThan(mc,mdims))){\n"
    "      lvl=1; return msdf[(mc.z*mdims.y+mc.y)*mdims.x+mc.x]; } }\n"
    "  if(hasFar==1){ ivec3 fc=ivec3(floor((p-forigin)/fvoxel));\n"
    "    if(all(greaterThanEqual(fc,ivec3(0)))&&all(lessThan(fc,fdims))){\n"
    "      lvl=2; return fsdf[(fc.z*fdims.y+fc.y)*fdims.x+fc.x]; } }\n"
    "  lvl=-1; return 1e9; }\n"
    "float mvox(int lvl){ return lvl==0 ? voxel : (lvl==1 ? mvoxel : fvoxel); }\n"
    "float sSDF(vec3 p){ int l; float d=mSDF(p,l); return l<0 ? 1e9 : d; }\n"
    "void basis(vec3 n, out vec3 t, out vec3 b){ vec3 u=abs(n.z)<0.999?vec3(0,0,1):vec3(1,0,0); t=dnorm(cross(u,n)); b=cross(n,t); }\n"
    "void shb(vec3 d, out float y[9]){ float x=d.x,yy=d.y,z=d.z;\n"
    "  y[0]=0.282094792; y[1]=0.488602512*yy; y[2]=0.488602512*z; y[3]=0.488602512*x;\n"
    "  y[4]=1.092548431*x*yy; y[5]=1.092548431*yy*z; y[6]=0.315391565*(3.0*z*z-1.0);\n"
    "  y[7]=1.092548431*x*z; y[8]=0.546274215*(x*x-yy*yy); }\n"
    /* Exact fine-SVO occupancy at p: descend the octree; a point is solid iff it\n"
     * reaches a max-depth leaf (any absent child => air). This is the ground-truth\n"
     * geometry the coarse SDF only approximates -- the refine marches on THIS. */
    /* svodepth is max_depth+1; descend max_depth levels like npc_svo_query_point,\n"
     * stopping at the first absent child, and report the stopping node's SOLID\n"
     * bit (packed into diffuse.w by the packer). */
    "bool svoSolid(vec3 p){ if(any(lessThan(p,svomin))||any(greaterThan(p,svomax))) return false;\n"
    "  uint node=0u; vec3 lo=svomin, hi=svomax;\n"
    "  for(int d=0; d<svodepth-1; ++d){ if(node>=uint(nodeCount)) return false; vec3 mid=(lo+hi)*0.5;\n"
    "    uint cx=p.x>=mid.x?1u:0u, cy=p.y>=mid.y?1u:0u, cz=p.z>=mid.z?1u:0u;\n"
    "    uint child=nodes[node].children[(cz<<2)|(cy<<1)|cx];\n"
    "    if(child==0xFFFFFFFFu) return nodes[node].diffuse.w>0.5;\n"
    "    if(cx==1u) lo.x=mid.x; else hi.x=mid.x; if(cy==1u) lo.y=mid.y; else hi.y=mid.y;\n"
    "    if(cz==1u) lo.z=mid.z; else hi.z=mid.z; node=child; }\n"
    "  if(node>=uint(nodeCount)) return false; return nodes[node].diffuse.w>0.5; }\n"
    /* Sphere-trace the SDF. Past `transition` we CONE-trace: the sampling\n"
     * footprint widens with distance (fp = voxel + (t-transition)*CONE), so far\n"
     * geometry is marched coarsely in a few big steps and the hit test triggers\n"
     * on a fatter shell. Sky is sampled by the CALLER only when this returns\n"
     * false, which happens solely on physically escaping the volume (inb) or\n"
     * exhausting maxT/steps -- never on a far hit. */
    "bool trace(vec3 o, vec3 dir, float maxT, out vec3 hp, out vec3 hn, out float ht){\n"
    "  float t=voxel; ht=0.0; const float CONE=0.06;\n"
    "  for(int i=0;i<512 && t<maxT;++i){ vec3 p=o+dir*t;\n"
    "    int lvl; float d=mSDF(p,lvl); if(lvl<0) return false;\n"  /* escaped all fields -> sky */
    "    float lv=mvox(lvl);\n"
    "    float fp = t>transition ? lv + (t-transition)*CONE : lv;\n"
    "    if(lvl==0){\n"
    /* NEAR field: refine against the true fine SVO. Step in fine voxel increments\n"
     * and test exact occupancy so the hit lands on the real surface voxel (thin\n"
     * emissive panels, coloured walls) rather than the coarse-SDF isosurface. */
    "      if(d<fp*1.5){ float rs=finevox*0.5; for(int j=0;j<48 && t<maxT;++j){ t+=rs; p=o+dir*t;\n"
    "          int l2; float dd=mSDF(p,l2); if(l2<0) return false;\n"
    "          if(svoSolid(p)){ hp=p; ht=t; float e=voxel;\n"
    "            vec3 g=vec3(sSDF(p+vec3(e,0,0))-sSDF(p-vec3(e,0,0)), sSDF(p+vec3(0,e,0))-sSDF(p-vec3(0,e,0)), sSDF(p+vec3(0,0,e))-sSDF(p-vec3(0,0,e)));\n"
    "            hn = dot(g,g)>1e-10 ? dnorm(g) : -dir; return true; }\n"
    "          if(l2==0 && dd>fp*2.0) break; } }\n"
    "    } else {\n"
    /* MEDIUM / FAR field: no SVO refine (leaves are sub-coarse-voxel); the coarse\n"
     * SDF sign IS the occluder. A negative sample means inside distant solid ->\n"
     * hit, shade by the SVO material at that (approximate) point. */
    "      if(d<0.0){ hp=p; ht=t; float e=lv;\n"
    "        vec3 g=vec3(sSDF(p+vec3(e,0,0))-sSDF(p-vec3(e,0,0)), sSDF(p+vec3(0,e,0))-sSDF(p-vec3(0,e,0)), sSDF(p+vec3(0,0,e))-sSDF(p-vec3(0,0,e)));\n"
    "        hn = dot(g,g)>1e-10 ? dnorm(g) : -dir; return true; }\n"
    "    }\n"
    "    t += max(d, fp); } return false; }\n"
    "bool shadow(vec3 o, vec3 dir, float maxT){ vec3 hp,hn; float ht; return trace(o,dir,maxT,hp,hn,ht); }\n"
    "vec3 direct(vec3 p, vec3 n){ vec3 e=vec3(0); vec3 o=p+n*voxel*1.5;\n"
    "  for(int i=0;i<nlights;++i){ vec3 lp=lit[4*i].xyz; int kind=int(lit[4*i].w);\n"
    "    vec3 ld=lit[4*i+1].xyz; vec3 col=lit[4*i+2].xyz; float ci=lit[4*i+2].w; float co=lit[4*i+3].x;\n"
    "    vec3 toL; float atten=1.0; float md;\n"
    "    if(kind==1){ toL=dnorm(-ld); md=1e5; }\n"
    "    else { vec3 dd=lp-p; float dist=length(dd); if(dist<1e-4) continue; toL=dd/dist; atten=1.0/(dist*dist); md=dist-voxel*1.5; }\n"
    "    float cosNL=dot(n,toL); if(cosNL<=0.0) continue;\n"
    "    if(kind==2){ float ca=dot(-toL,dnorm(ld)); float sp=clamp((ca-co)/max(ci-co,1e-4),0.0,1.0); atten*=sp; if(atten<=0.0) continue; }\n"
    /* Read this light's visibility from the precomputed store at the offset\n"
     * origin's near cell; outside the near grid fall back to a live shadow ray. */
    "    ivec3 sc=toCell(o); float vis;\n"
    "    if(inb(sc)) vis = lightvis[cidx(sc)*nlights + i];\n"
    "    else vis = shadow(o,toL,md) ? 0.0 : 1.0;\n"
    "    e += col*(cosNL*atten*vis); } return e; }\n"
    "void svoMat(vec3 p, out vec3 diff, out vec3 emis){ diff=vec3(0); emis=vec3(0);\n"
    "  if(any(lessThan(p,svomin))||any(greaterThan(p,svomax))) return;\n"
    "  uint node=0u; vec3 lo=svomin, hi=svomax;\n"
    "  for(int d=0; d<svodepth; ++d){ if(node>=uint(nodeCount)) return; vec3 mid=(lo+hi)*0.5;\n"
    "    uint cx=p.x>=mid.x?1u:0u, cy=p.y>=mid.y?1u:0u, cz=p.z>=mid.z?1u:0u;\n"
    "    uint child=nodes[node].children[(cz<<2)|(cy<<1)|cx]; if(child==0xFFFFFFFFu) break;\n"
    "    if(cx==1u) lo.x=mid.x; else hi.x=mid.x; if(cy==1u) lo.y=mid.y; else hi.y=mid.y;\n"
    "    if(cz==1u) lo.z=mid.z; else hi.z=mid.z; node=child; }\n"
    "  if(node>=uint(nodeCount)) return; diff=nodes[node].diffuse.rgb; emis=nodes[node].emissive.rgb; }\n"
    "void main(){ uint li=gl_GlobalInvocationID.x; if(li>=uint(nlux)) return;\n"
    "  vec3 pos=lux[2u*li].xyz; vec3 nr=lux[2u*li+1u].xyz;\n"
    /* Stabilise a degenerate luxel normal (never normalize a zero vector). */
    "  vec3 nrm = dot(nr,nr)>1e-12 ? dnorm(nr) : vec3(0.0,1.0,0.0);\n"
    /* Match the CPU: if the offset origin is inside solid (concave shell overlap\n"
     * or a flipped normal), march along the normal to the nearest air. */
    "  vec3 obase = pos + nrm*finevox*1.5;\n"
    "  for(int s=0; s<16 && svoSolid(obase); ++s) obase += nrm*finevox;\n"
    "  vec3 tn,bt; basis(nrm,tn,bt);\n"
    "  int n=int(sqrt(float(samples))); if(n<1) n=1; float weight=TAU/float(n*n);\n"
    /* Seed the per-luxel RNG from the luxel's WORLD POSITION, not its (chunk-\n"
     * local) index: per-chunk atlas bakes (rpg-yfa4) index luxels differently, so\n"
     * an index seed decorrelates the noise across a chunk border -> a visible\n"
     * blotchy seam. A world-position hash is identical no matter which chunk owns\n"
     * the luxel, so adjacent chunks' noise fields line up (and match a whole-scene\n"
     * bake exactly). */
    "  uint rs = seed ^ (floatBitsToUint(pos.x)*73856093u ^ floatBitsToUint(pos.y)*19349663u ^ floatBitsToUint(pos.z)*83492791u);\n"
    "  float sh[27]; for(int k=0;k<27;++k) sh[k]=0.0;\n"
    "  for(int sy=0;sy<n;++sy) for(int sx=0;sx<n;++sx){\n"
    "    float u1=(float(sx)+rnd(rs))/float(n); float u2=(float(sy)+rnd(rs))/float(n);\n"
    "    float z=u1; float r=sqrt(max(1.0-z*z,0.0)); float phi=TAU*u2;\n"
    "    vec3 dir=tn*(r*dcos(phi))+bt*(r*dsin(phi))+nrm*z;\n"
    "    vec3 Li=vec3(0.0), thr=vec3(1.0); vec3 o=obase, d=dir;\n"
    "    for(int b=0;b<=bounces;++b){ vec3 hp,hn; float ht;\n"
    "      if(!trace(o,d,1e5,hp,hn,ht)){ Li+=thr*sky; break; }\n"
    "      vec3 diff,emis; svoMat(hp,diff,emis);\n"
    "      Li += thr*emis; Li += thr*diff*direct(hp,hn)*INVPI; thr *= diff;\n"
    "      if(b==bounces) break;\n"
    "      vec3 ct,cb; basis(hn,ct,cb); float a=rnd(rs),e2=rnd(rs);\n"
    "      float cz=sqrt(a), cr=sqrt(1.0-a), cp=TAU*e2;\n"
    "      d=ct*(cr*dcos(cp))+cb*(cr*dsin(cp))+hn*cz; o=hp+hn*finevox*1.5; }\n"
    "    float y[9]; shb(dnorm(dir),y);\n"
    "    for(int c=0;c<3;++c) for(int i=0;i<9;++i) sh[c*9+i]+=Li[c]*weight*y[i]; }\n"
    /* One batch's estimate; the HOST reads this back and sums across batches\n"
     * (cross-dispatch SSBO read-modify-write is not reliable across drivers). */
    "  for(int k=0;k<27;++k) osh[27u*li+uint(k)]=sh[k]; }\n";

bool lm_gpu_gather_init(const gl_loader_t *loader) {
    if (loader == NULL || loader->get_proc_address == NULL) return false;
    GG_LOAD(CreateShader,"glCreateShader"); GG_LOAD(ShaderSource,"glShaderSource");
    GG_LOAD(CompileShader,"glCompileShader"); GG_LOAD(GetShaderiv,"glGetShaderiv");
    GG_LOAD(GetShaderInfoLog,"glGetShaderInfoLog"); GG_LOAD(CreateProgram,"glCreateProgram");
    GG_LOAD(AttachShader,"glAttachShader"); GG_LOAD(LinkProgram,"glLinkProgram");
    GG_LOAD(GetProgramiv,"glGetProgramiv"); GG_LOAD(GetProgramInfoLog,"glGetProgramInfoLog");
    GG_LOAD(DeleteShader,"glDeleteShader"); GG_LOAD(DeleteProgram,"glDeleteProgram");
    GG_LOAD(UseProgram,"glUseProgram"); GG_LOAD(GetUniformLocation,"glGetUniformLocation");
    GG_LOAD(Uniform1i,"glUniform1i"); GG_LOAD(Uniform1ui,"glUniform1ui");
    GG_LOAD(Uniform1f,"glUniform1f"); GG_LOAD(Uniform3fv,"glUniform3fv"); GG_LOAD(Uniform3iv,"glUniform3iv");
    GG_LOAD(GenBuffers,"glGenBuffers"); GG_LOAD(DeleteBuffers,"glDeleteBuffers");
    GG_LOAD(BindBuffer,"glBindBuffer"); GG_LOAD(BufferData,"glBufferData");
    GG_LOAD(GetBufferSubData,"glGetBufferSubData"); GG_LOAD(BindBufferBase,"glBindBufferBase");
    GG_LOAD(DispatchCompute,"glDispatchCompute"); GG_LOAD(MemoryBarrier,"glMemoryBarrier");
    GG_LOAD(Finish,"glFinish");
    g_init = gg_compile(CS_JFA_INIT); g_step = gg_compile(CS_JFA_STEP);
    g_fin = gg_compile(CS_JFA_FIN);  g_gather = gg_compile(CS_GATHER);
    g_lightvis = gg_compile(CS_LIGHTVIS);
    g_ready = g_init && g_step && g_fin && g_gather && g_lightvis;
    return g_ready;
}

void lm_gpu_gather_shutdown(void) {
    if (!g_ready) return;
    gl.DeleteProgram(g_init); gl.DeleteProgram(g_step);
    gl.DeleteProgram(g_fin); gl.DeleteProgram(g_gather); gl.DeleteProgram(g_lightvis);
    g_init = g_step = g_fin = g_gather = g_lightvis = 0; g_ready = false;
}

static void u3f(GLuint p, const char *n, const float v[3]) { gl.Uniform3fv(gl.GetUniformLocation(p,n),1,v); }

/* --- Conservative triangle-vs-AABB overlap (Akenine-Moller SAT) -------------
 * True if triangle (a,b,c) overlaps the box centred at @p bc with half-extents
 * @p bh. Used to rasterise every scene triangle into the coarse occupancy grid:
 * a cell is solid iff a triangle actually intersects it, which is watertight by
 * construction (unlike point-sampling, a thin wall cannot slip between cells). */
static bool plane_box_overlap(const float n[3], const float v[3], const float h[3]) {
    float vmin[3], vmax[3];
    for (int q = 0; q < 3; ++q) {
        if (n[q] > 0.0f) { vmin[q] = -h[q] - v[q]; vmax[q] = h[q] - v[q]; }
        else             { vmin[q] =  h[q] - v[q]; vmax[q] = -h[q] - v[q]; }
    }
    if (n[0]*vmin[0]+n[1]*vmin[1]+n[2]*vmin[2] > 0.0f) return false;
    if (n[0]*vmax[0]+n[1]*vmax[1]+n[2]*vmax[2] >= 0.0f) return true;
    return false;
}
static bool tri_box_overlap(const float bc[3], const float bh[3],
                            const float a[3], const float b[3], const float c[3]) {
    float v0[3], v1[3], v2[3];
    for (int i = 0; i < 3; ++i) { v0[i]=a[i]-bc[i]; v1[i]=b[i]-bc[i]; v2[i]=c[i]-bc[i]; }
    float e0[3], e1[3], e2[3];
    for (int i = 0; i < 3; ++i) { e0[i]=v1[i]-v0[i]; e1[i]=v2[i]-v1[i]; e2[i]=v0[i]-v2[i]; }
    /* 9 edge-cross-axis tests (SAT). Each: project the 3 verts on the axis and
     * the box radius; separated if the intervals don't overlap. */
    const float *E[3] = { e0, e1, e2 };
    const float *V[3] = { v0, v1, v2 };
    for (int ei = 0; ei < 3; ++ei) {
        float fx = fabsf(E[ei][0]), fy = fabsf(E[ei][1]), fz = fabsf(E[ei][2]);
        /* axis = e x X = (0,-ez,ey) */
        {   float p0=-E[ei][2]*V[0][1]+E[ei][1]*V[0][2];
            float p1=-E[ei][2]*V[1][1]+E[ei][1]*V[1][2];
            float p2=-E[ei][2]*V[2][1]+E[ei][1]*V[2][2];
            float mn2=fminf(p0,fminf(p1,p2)), mx2=fmaxf(p0,fmaxf(p1,p2));
            float rad=fz*bh[1]+fy*bh[2]; if (mn2>rad || mx2<-rad) return false; }
        /* axis = e x Y = (ez,0,-ex) */
        {   float p0= E[ei][2]*V[0][0]-E[ei][0]*V[0][2];
            float p1= E[ei][2]*V[1][0]-E[ei][0]*V[1][2];
            float p2= E[ei][2]*V[2][0]-E[ei][0]*V[2][2];
            float mn2=fminf(p0,fminf(p1,p2)), mx2=fmaxf(p0,fmaxf(p1,p2));
            float rad=fz*bh[0]+fx*bh[2]; if (mn2>rad || mx2<-rad) return false; }
        /* axis = e x Z = (-ey,ex,0) */
        {   float p0=-E[ei][1]*V[0][0]+E[ei][0]*V[0][1];
            float p1=-E[ei][1]*V[1][0]+E[ei][0]*V[1][1];
            float p2=-E[ei][1]*V[2][0]+E[ei][0]*V[2][1];
            float mn2=fminf(p0,fminf(p1,p2)), mx2=fmaxf(p0,fmaxf(p1,p2));
            float rad=fy*bh[0]+fx*bh[1]; if (mn2>rad || mx2<-rad) return false; }
    }
    /* 3 box-face axes: triangle AABB vs box. */
    for (int q = 0; q < 3; ++q) {
        float mn2=fminf(v0[q],fminf(v1[q],v2[q])), mx2=fmaxf(v0[q],fmaxf(v1[q],v2[q]));
        if (mn2>bh[q] || mx2<-bh[q]) return false;
    }
    /* Triangle-plane axis. */
    float nrm[3] = { e0[1]*e1[2]-e0[2]*e1[1], e0[2]*e1[0]-e0[0]*e1[2], e0[0]*e1[1]-e0[1]*e1[0] };
    return plane_box_overlap(nrm, v0, bh);
}

/* --- Coarse JFA signed-distance field over an arbitrary box --------------------
 * Conservatively rasterise every scene triangle into a dense occupancy grid over
 * [mn,mx] (longest axis <= @p max_dim cells, never finer than @p fine_voxel),
 * then Jump-Flood it into a signed distance field. On success returns the SDF in
 * a freshly-created SSBO (@p out_sdf, dims^3 floats, negative inside solid; the
 * caller deletes it) and writes the grid @p out_dims + @p out_voxel. Used for
 * both the per-chunk NEAR field and the whole-scene FAR field. Returns false on
 * allocation failure. Uses the file-static JFA programs (init/step/fin). */
static bool build_sdf(const lm_mesh_scene_t *scene, float fine_voxel,
                      const float mn[3], const float mx[3], int max_dim,
                      GLuint *out_sdf, int out_dims[3], float *out_voxel) {
    float ext[3] = { mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2] };
    float longest = fmaxf(ext[0], fmaxf(ext[1], ext[2]));
    float svoxel = longest / (float)max_dim; if (svoxel < fine_voxel) svoxel = fine_voxel;
    int dims[3];
    for (int a = 0; a < 3; ++a) { dims[a] = (int)ceilf(ext[a] / svoxel); if (dims[a] < 1) dims[a] = 1; }
    size_t cells = (size_t)dims[0] * dims[1] * dims[2];

    uint32_t *occ = malloc(cells * sizeof(uint32_t));
    if (!occ) return false;
    memset(occ, 0, cells * sizeof(uint32_t));
    /* Conservative triangle rasterisation: stamp every scene triangle into every
     * coarse cell it intersects -> watertight walls (the SDF then truly encloses). */
    float bh[3] = { svoxel*0.5f + 1e-4f, svoxel*0.5f + 1e-4f, svoxel*0.5f + 1e-4f };
    for (uint32_t mi = 0; mi < (scene ? scene->n_meshes : 0u); ++mi) {
        const lm_mesh_t *me = &scene->meshes[mi];
        for (uint32_t t = 0; t + 2 < me->index_count; t += 3) {
            const float *a = &me->positions[me->indices[t]*3];
            const float *b = &me->positions[me->indices[t+1]*3];
            const float *c = &me->positions[me->indices[t+2]*3];
            float tmn[3], tmx[3];
            for (int k = 0; k < 3; ++k) {
                tmn[k] = fminf(a[k], fminf(b[k], c[k]));
                tmx[k] = fmaxf(a[k], fmaxf(b[k], c[k]));
            }
            int lo[3], hi[3];
            for (int k = 0; k < 3; ++k) {
                lo[k] = (int)floorf((tmn[k] - mn[k]) / svoxel);
                hi[k] = (int)floorf((tmx[k] - mn[k]) / svoxel);
                if (lo[k] < 0) lo[k] = 0;
                if (hi[k] >= dims[k]) hi[k] = dims[k] - 1;
            }
            for (int z = lo[2]; z <= hi[2]; ++z)
            for (int y = lo[1]; y <= hi[1]; ++y)
            for (int x = lo[0]; x <= hi[0]; ++x) {
                size_t idx = (size_t)(z*dims[1]+y)*dims[0]+x;
                if (occ[idx]) continue;
                float bc[3] = { mn[0]+((float)x+0.5f)*svoxel,
                                mn[1]+((float)y+0.5f)*svoxel,
                                mn[2]+((float)z+0.5f)*svoxel };
                if (tri_box_overlap(bc, bh, a, b, c)) occ[idx] = 1u;
            }
        }
    }

    GLuint bo[4]; gl.GenBuffers(4, bo); /* [0]=occ [1]/[2]=JFA ping-pong seeds [3]=sdf */
    #define UPB(b,sz,ptr) do{ gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,b); gl.BufferData(GL_SHADER_STORAGE_BUFFER,(GLsizeiptr)(sz),ptr,GL_DYNAMIC_COPY);}while(0)
    UPB(bo[0], cells*sizeof(uint32_t), occ);
    UPB(bo[1], cells*sizeof(int32_t), NULL); UPB(bo[2], cells*sizeof(int32_t), NULL);
    UPB(bo[3], cells*sizeof(float), NULL);
    #undef UPB
    free(occ);

    GLuint gx = (GLuint)((dims[0]+3)/4), gy=(GLuint)((dims[1]+3)/4), gz=(GLuint)((dims[2]+3)/4);
    /* JFA: init -> ping-pong passes -> finalise. Full barrier between dispatches
     * (NVIDIA needs it; harmless on Intel Xe). */
    gl.UseProgram(g_init); gl.Uniform3iv(gl.GetUniformLocation(g_init,"dims"),1,dims);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,bo[0]); gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,bo[1]);
    gl.DispatchCompute(gx,gy,gz); gl.MemoryBarrier(0xFFFFFFFFu);
    int maxd = dims[0]>dims[1]?(dims[0]>dims[2]?dims[0]:dims[2]):(dims[1]>dims[2]?dims[1]:dims[2]);
    GLuint src=bo[1], dst=bo[2];
    gl.UseProgram(g_step); gl.Uniform3iv(gl.GetUniformLocation(g_step,"dims"),1,dims);
    for (int step = maxd/2; step >= 1; step /= 2) {
        gl.Uniform1i(gl.GetUniformLocation(g_step,"step"),step);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,src); gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,dst);
        gl.DispatchCompute(gx,gy,gz); gl.MemoryBarrier(0xFFFFFFFFu);
        GLuint t=src; src=dst; dst=t;
    }
    gl.UseProgram(g_fin); gl.Uniform3iv(gl.GetUniformLocation(g_fin,"dims"),1,dims);
    gl.Uniform1f(gl.GetUniformLocation(g_fin,"voxel"),svoxel);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,src); gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,bo[0]);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,2,bo[3]);
    gl.DispatchCompute(gx,gy,gz); gl.MemoryBarrier(0xFFFFFFFFu);

    GLuint discard[3] = { bo[0], bo[1], bo[2] }; gl.DeleteBuffers(3, discard);
    *out_sdf = bo[3]; out_dims[0]=dims[0]; out_dims[1]=dims[1]; out_dims[2]=dims[2]; *out_voxel = svoxel;
    return true;
}

bool lm_gpu_field_build(const lm_mesh_scene_t *scene, float fine_voxel,
                        const phys_aabb_t *box, int max_dim, lm_gpu_field_t *out) {
    if (!g_ready || out == NULL || box == NULL) return false;
    float mn[3] = { box->min.x, box->min.y, box->min.z };
    float mx[3] = { box->max.x, box->max.y, box->max.z };
    GLuint buf; int dims[3]; float voxel;
    if (!build_sdf(scene, fine_voxel, mn, mx, max_dim, &buf, dims, &voxel)) return false;
    out->buf = (uint32_t)buf;
    out->dims[0]=dims[0]; out->dims[1]=dims[1]; out->dims[2]=dims[2];
    out->origin[0]=mn[0]; out->origin[1]=mn[1]; out->origin[2]=mn[2];
    out->voxel = voxel;
    return true;
}

void lm_gpu_field_free(lm_gpu_field_t *field) {
    if (field == NULL || field->buf == 0) return;
    GLuint b = (GLuint)field->buf; gl.DeleteBuffers(1, &b);
    field->buf = 0;
}

bool lm_gpu_gather_run(const lm_lightmap_t *lm, lm_sh9_t *accum,
                       const npc_svo_grid_t *svo, const lm_mesh_scene_t *scene,
                       const phys_aabb_t *region, const lm_gpu_field_t *far,
                       const lm_light_t *lights, uint32_t n_lights,
                       const lm_sky_t *sky, float transition, float maxdist,
                       uint32_t samples, uint32_t bounces, uint32_t seed) {
    (void)maxdist;
    if (!g_ready || lm == NULL || accum == NULL || svo == NULL) return false;
    uint32_t nlux = lm->res_u * lm->res_v;

    /* Three trace levels (rpg-fzht). The SVO descent (svoSolid/svoMat) always
     * spans the whole scene; the fine NEAR SDF covers @p region (a chunk's outer
     * box) when given, else the whole SVO. When a small chunk region is given we
     * ALSO build a per-chunk MEDIUM field (~3x the region) so mid-range geometry
     * resolves, and take an optional coarse shared FAR field @p far (built by the
     * caller, one per chunk neighbourhood). A ray escapes to sky only after
     * leaving all present fields -- otherwise small chunks read far too bright. */
    float svo_mn[3] = { svo->world_bounds.min.x, svo->world_bounds.min.y, svo->world_bounds.min.z };
    float svo_mx[3] = { svo->world_bounds.max.x, svo->world_bounds.max.y, svo->world_bounds.max.z };

    float mn[3], mx[3];
    if (region) {
        mn[0]=region->min.x; mn[1]=region->min.y; mn[2]=region->min.z;
        mx[0]=region->max.x; mx[1]=region->max.y; mx[2]=region->max.z;
    } else {
        for (int a=0;a<3;++a){ mn[a]=svo_mn[a]; mx[a]=svo_mx[a]; }
    }

    /* NEAR field over the region (fine, 128^3). */
    GLuint b_sdf = 0; int dims[3]; float svoxel = 0.0f;
    if (!build_sdf(scene, svo->voxel_size, mn, mx, 128, &b_sdf, dims, &svoxel))
        return false;

    /* MEDIUM field: a ~MED_MULT x region box (chunked bakes only), clamped to the
     * scene, 128^3 -> coarser than near since it spans a bigger box. */
    lm_gpu_field_t med = {0}; int hasMed = 0;
    if (region) {
        float med_mult = getenv("LM_MED_MULT") ? (float)atof(getenv("LM_MED_MULT")) : 3.0f;
        phys_aabb_t mbox;
        float mc[3] = { (mn[0]+mx[0])*0.5f, (mn[1]+mx[1])*0.5f, (mn[2]+mx[2])*0.5f };
        float mh[3] = { (mx[0]-mn[0])*0.5f*med_mult, (mx[1]-mn[1])*0.5f*med_mult, (mx[2]-mn[2])*0.5f*med_mult };
        mbox.min.x=fmaxf(mc[0]-mh[0],svo_mn[0]); mbox.min.y=fmaxf(mc[1]-mh[1],svo_mn[1]); mbox.min.z=fmaxf(mc[2]-mh[2],svo_mn[2]);
        mbox.max.x=fminf(mc[0]+mh[0],svo_mx[0]); mbox.max.y=fminf(mc[1]+mh[1],svo_mx[1]); mbox.max.z=fminf(mc[2]+mh[2],svo_mx[2]);
        if (lm_gpu_field_build(scene, svo->voxel_size, &mbox, 128, &med)) hasMed = 1;
        else { gl.DeleteBuffers(1, &b_sdf); return false; }
    }
    int hasFar = (far && far->buf) ? 1 : 0;
    if (getenv("LM_GPU_DEBUG"))
        fprintf(stderr, "[gpugather] near %dx%dx%d vox=%.3f  med %s %dx%dx%d vox=%.3f  far %s vox=%.3f  nlux=%u\n",
                dims[0],dims[1],dims[2],svoxel, hasMed?"on":"off", med.dims[0],med.dims[1],med.dims[2],med.voxel,
                hasFar?"on":"off", hasFar?far->voxel:0.0f, nlux);

    lm_gpu_node_t *nodes = malloc((size_t)svo->node_count * sizeof(lm_gpu_node_t));
    lm_gpu_luxel_t *plux = malloc((size_t)nlux * sizeof(lm_gpu_luxel_t));
    lm_gpu_light_t *plit = malloc((size_t)(n_lights ? n_lights : 1) * sizeof(lm_gpu_light_t));
    float *osh = malloc((size_t)nlux * 27 * sizeof(float));
    if (!nodes || !plux || !plit || !osh) {
        gl.DeleteBuffers(1, &b_sdf); lm_gpu_field_free(&med);
        free(nodes); free(plux); free(plit); free(osh); return false;
    }
    lm_gpu_pack_nodes(svo, nodes, svo->node_count);
    lm_gpu_pack_luxels(lm, plux, nlux);
    lm_gpu_pack_lights(lights, n_lights, plit, n_lights ? n_lights : 1);

    GLuint b_nodes, b_lux, b_out, b_lit;
    GLuint all[4]; gl.GenBuffers(4, all);
    b_nodes=all[0]; b_lux=all[1]; b_out=all[2]; b_lit=all[3];
    #define UP(bo,sz,ptr) do{ gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,bo); gl.BufferData(GL_SHADER_STORAGE_BUFFER,(GLsizeiptr)(sz),ptr,GL_DYNAMIC_COPY);}while(0)
    UP(b_nodes, (size_t)svo->node_count*sizeof(lm_gpu_node_t), nodes);
    UP(b_lux, (size_t)nlux*sizeof(lm_gpu_luxel_t), plux);
    UP(b_out, (size_t)nlux*27*sizeof(float), NULL);
    UP(b_lit, (size_t)(n_lights?n_lights:1)*sizeof(lm_gpu_light_t), plit);

    /* Precompute the per-cell, per-light visibility store (rpg-yfa4): cast a
     * shadow ray from each near-grid cell to every static light ONCE, so the
     * gather reads visibility at a bounce hit instead of tracing per-hit. */
    size_t ncells = (size_t)dims[0] * dims[1] * dims[2];
    uint32_t nlv = n_lights ? n_lights : 1u;
    GLuint b_lightvis; gl.GenBuffers(1, &b_lightvis);
    UP(b_lightvis, ncells * nlv * sizeof(float), NULL);
    if (n_lights > 0) {
        gl.UseProgram(g_lightvis);
        gl.Uniform3iv(gl.GetUniformLocation(g_lightvis,"dims"),1,dims);
        gl.Uniform1f(gl.GetUniformLocation(g_lightvis,"voxel"),svoxel);
        u3f(g_lightvis,"origin",mn);
        gl.Uniform1i(gl.GetUniformLocation(g_lightvis,"nlights"),(int)n_lights);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,b_sdf);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,1,b_lightvis);
        gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,2,b_lit);
        gl.DispatchCompute((GLuint)((dims[0]+3)/4),(GLuint)((dims[1]+3)/4),(GLuint)((dims[2]+3)/4));
        gl.MemoryBarrier(0xFFFFFFFFu);
    }

    /* Gather. */
    float origin[3]={mn[0],mn[1],mn[2]};
    float skycol[3] = { sky?sky->color.x:0.0f, sky?sky->color.y:0.0f, sky?sky->color.z:0.0f };
    float svomn[3]={svo_mn[0],svo_mn[1],svo_mn[2]}, svomx[3]={svo_mx[0],svo_mx[1],svo_mx[2]};
    gl.UseProgram(g_gather);
    gl.Uniform3iv(gl.GetUniformLocation(g_gather,"dims"),1,dims);
    gl.Uniform1f(gl.GetUniformLocation(g_gather,"voxel"),svoxel);
    u3f(g_gather,"origin",origin); u3f(g_gather,"sky",skycol);
    u3f(g_gather,"svomin",svomn); u3f(g_gather,"svomax",svomx);
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"bounces"),(int)bounces);
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"nlux"),(int)nlux);
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"nlights"),(int)n_lights);
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"svodepth"),(int)svo->max_depth+1);
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"nodeCount"),(int)svo->node_count);
    gl.Uniform1f(gl.GetUniformLocation(g_gather,"transition"),transition);
    gl.Uniform1f(gl.GetUniformLocation(g_gather,"finevox"),svo->voxel_size);
    /* MEDIUM + FAR levels (rpg-fzht): absent levels bind the near SDF to their
     * slot so the binding is always valid; hasMed/hasFar gate their use. */
    int med_dims[3] = { med.dims[0], med.dims[1], med.dims[2] };
    float med_origin[3] = { med.origin[0], med.origin[1], med.origin[2] };
    int far_dims[3] = { hasFar?far->dims[0]:1, hasFar?far->dims[1]:1, hasFar?far->dims[2]:1 };
    float far_origin[3] = { hasFar?far->origin[0]:0.0f, hasFar?far->origin[1]:0.0f, hasFar?far->origin[2]:0.0f };
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"hasMed"),hasMed);
    gl.Uniform3iv(gl.GetUniformLocation(g_gather,"mdims"),1,med_dims);
    u3f(g_gather,"morigin",med_origin);
    gl.Uniform1f(gl.GetUniformLocation(g_gather,"mvoxel"),med.voxel);
    gl.Uniform1i(gl.GetUniformLocation(g_gather,"hasFar"),hasFar);
    gl.Uniform3iv(gl.GetUniformLocation(g_gather,"fdims"),1,far_dims);
    u3f(g_gather,"forigin",far_origin);
    gl.Uniform1f(gl.GetUniformLocation(g_gather,"fvoxel"),hasFar?far->voxel:1.0f);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,0,b_sdf);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,2,b_lux);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,3,b_out);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,4,b_lit);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,5,b_nodes);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,6,hasFar?(GLuint)far->buf:b_sdf);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,7,hasMed?(GLuint)med.buf:b_sdf);
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,8,b_lightvis);
    /* Sample-batch the dispatch. A single dispatch of all samples over every
     * luxel trips the GPU watchdog (TDR) at high sample counts and comes back
     * zero; splitting into <=PER-sample batches keeps each dispatch short (the
     * synchronous readback bounds it) and averages the per-batch SH estimates. */
    int PER = getenv("LM_GPU_BATCH") ? atoi(getenv("LM_GPU_BATCH")) : 64;
    if (PER < 1) PER = 64;
    int throttle_us = getenv("LM_GPU_THROTTLE_US") ? atoi(getenv("LM_GPU_THROTTLE_US")) : 0;
    int per = (int)samples < PER ? (int)samples : PER; if (per < 1) per = 1;
    int nbatch = ((int)samples + per - 1) / per; if (nbatch < 1) nbatch = 1;
    /* Host-side accumulation: each dispatch OVERWRITES osh with its own batch
     * estimate; we read it back and sum on the CPU. Cross-dispatch SSBO
     * read-modify-write is not reliable across drivers (races/incoherence gave
     * non-deterministic darkening on both Intel and NVIDIA), so the buffer is
     * write-only per batch and the accumulator lives in host memory. The
     * readback also bounds each dispatch (TDR-safe). */
    double *osum = calloc((size_t)nlux * 27, sizeof(double));
    if (!osum) {
        gl.DeleteBuffers(4, all); gl.DeleteBuffers(1, &b_sdf); gl.DeleteBuffers(1, &b_lightvis); lm_gpu_field_free(&med);
        free(nodes); free(plux); free(plit); free(osh); return false;
    }
    gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER,3,b_out);
    for (int bi = 0; bi < nbatch; ++bi) {
        gl.Uniform1i(gl.GetUniformLocation(g_gather,"samples"), per);
        gl.Uniform1ui(gl.GetUniformLocation(g_gather,"seed"), seed ^ ((uint32_t)bi * 0x9E3779B9u + 0x85EBCA6Bu));
        gl.DispatchCompute((GLuint)((nlux+63)/64),1,1);
        /* GL_BUFFER_UPDATE_BARRIER_BIT (0x200): make the compute shader's SSBO
         * writes visible to the client glGetBufferSubData readback below. */
        gl.MemoryBarrier(0xFFFFFFFFu); /* GL_ALL_BARRIER_BITS */
        /* NVIDIA does not implicitly flush the compute SSBO writes for the client
         * readback below (it returned all-zero without this); glFinish forces the
         * dispatch to complete + writes to land. Also bounds each dispatch under
         * the GPU watchdog / keeps a shared iGPU's compositor alive. */
        gl.Finish();
        /* Synchronous readback of this batch's estimate; summed on the host. */
        gl.BindBuffer(GL_SHADER_STORAGE_BUFFER,b_out);
        gl.GetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,(GLsizeiptr)((size_t)nlux*27*sizeof(float)),osh);
        for (size_t k = 0; k < (size_t)nlux * 27; ++k) osum[k] += (double)osh[k];
        if (getenv("LM_GPU_DEBUG") && (bi==0 || (bi+1)%16==0 || bi+1==nbatch))
            fprintf(stderr, "[gpugather] batch %d/%d (%d spp)\n", bi+1, nbatch, per);
        /* Optional cooperative yield so the compositor gets the GPU between
         * batches on a heavily-loaded shared device. */
        if (throttle_us > 0) {
            struct timespec ts = { 0, (long)throttle_us * 1000L };
            nanosleep(&ts, NULL);
        }
        if (getenv("LM_GPU_DEBUG") && (bi==0 || (bi+1)%32==0 || bi+1==nbatch)) {
            fprintf(stderr, "[gpugather] batch %d/%d (%d spp)\n", bi+1, nbatch, per); fflush(stderr);
        }
    }
    double invb = 1.0 / (double)nbatch;
    for (uint32_t i = 0; i < nlux; ++i)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 9; ++k)
                accum[i*3+c].c[k] = (float)(osum[i*27 + c*9 + k] * invb);
    free(osum);

    gl.DeleteBuffers(4, all); gl.DeleteBuffers(1, &b_sdf); gl.DeleteBuffers(1, &b_lightvis); lm_gpu_field_free(&med);
    free(nodes); free(plux); free(plit); free(osh);
    return true;
    #undef UP
}
