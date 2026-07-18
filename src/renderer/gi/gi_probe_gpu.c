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
    "uniform int u_nprobes,u_nlights,u_nboxes,u_ncones;\n"
    "uniform float u_soft, u_albedo;\n"
    "uniform sampler3D u_sdf[8];\n"
    "uniform vec3 u_sdf_origin[8];\n"
    "uniform vec3 u_sdf_dim[8];\n"
    "uniform float u_sdf_vox[8];\n"
    "uniform int u_sdf_active[8];\n"
    /* Static irradiance volume (rpg-pau4): the baked lightmap E, splatted to a\n"
     * coarse world grid. A cone that hits a surface gathers this so the probe\n"
     * carries the STATIC bounced ambience (one bounce beyond the lightmapper),\n"
     * not just the dynamic-light term. u_static_k boosts it. */
    "uniform sampler3D u_static_irr;\n"
    "uniform vec3 u_static_origin;\n"
    "uniform vec3 u_static_dim;\n"
    "uniform float u_static_vox;\n"
    "uniform int u_static_on;\n"
    "uniform float u_static_k;\n"
    "layout(std430,binding=0) readonly buffer PP { vec4 ppos[]; };\n"
    "layout(std430,binding=1) writeonly buffer PS { float psh[]; };\n"
    "layout(std430,binding=2) readonly buffer LB { vec4 lt[]; };\n"
    "layout(std430,binding=3) readonly buffer BX { vec4 bx[]; };\n"
    /* DDGI depth probe: OCTRES^2 texels/probe, each (mean dist, mean sq dist)\n"
     * for the Chebyshev visibility test at interpolation time. OCTRES fixed at 8\n"
     * (must match the forward+ sampler). */
    "layout(std430,binding=4) writeonly buffer PD { float pdepth[]; };\n"
    /* One spherical-Gaussian specular lobe per probe (rpg-hw75): 8 floats =\n"
     * axis.xyz + sharpness, then rgb amplitude + pad. Fit by moment-matching the\n"
     * traced radiance so glossy surfaces get a cheap directional reflection. */
    "layout(std430,binding=5) writeonly buffer PG { float psg[]; };\n"
    "const float PI=3.14159265;\n"
    "float box_sdf(vec3 p,vec3 c,vec3 h){ vec3 q=abs(p-c)-h; return length(max(q,vec3(0.0)))+min(max(q.x,max(q.y,q.z)),0.0); }\n"
    /* Distance is the ALPHA of the RGBA voxel texture (rgb = static albedo). */
    "float scene_sdf(vec3 p){ float d=1e30;\n"
    "  for(int i=0;i<8;++i){ if(u_sdf_active[i]==0) continue;\n"
    "    vec3 g=(p-u_sdf_origin[i])/u_sdf_vox[i];\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_sdf_dim[i]))){\n"
    "      vec3 uvw=(g+0.5)/u_sdf_dim[i]; d=min(d, texture(u_sdf[i],uvw).a); } }\n"
    "  for(int i=0;i<u_nboxes;++i){ d=min(d, box_sdf(p,bx[i*2].xyz,bx[i*2+1].xyz)); }\n"
    "  return d; }\n"
    /* Voxelised static albedo at p, sampled at mip @p lod (cone footprint). Dynamic\n"
     * collider boxes have no baked albedo -> neutral grey fallback. */
    "vec3 scene_albedo(vec3 p, float lod){\n"
    "  for(int i=0;i<8;++i){ if(u_sdf_active[i]==0) continue;\n"
    "    vec3 g=(p-u_sdf_origin[i])/u_sdf_vox[i];\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_sdf_dim[i]))){\n"
    "      vec3 uvw=(g+0.5)/u_sdf_dim[i]; return textureLod(u_sdf[i],uvw,lod).rgb; } }\n"
    "  return vec3(0.5); }\n"
    /* Baked static irradiance E at world p (trilinear); 0 outside the volume. */
    "vec3 static_irr(vec3 p){ if(u_static_on==0) return vec3(0.0);\n"
    "  vec3 g=(p-u_static_origin)/u_static_vox;\n"
    "  if(any(lessThan(g,vec3(0.0)))||any(greaterThanEqual(g,u_static_dim))) return vec3(0.0);\n"
    "  vec3 uvw=(g+0.5)/u_static_dim; return texture(u_static_irr,uvw).rgb; }\n"
    "vec3 sdf_normal(vec3 p){ float e=0.06;\n"
    "  return normalize(vec3(scene_sdf(p+vec3(e,0,0))-scene_sdf(p-vec3(e,0,0)),\n"
    "                        scene_sdf(p+vec3(0,e,0))-scene_sdf(p-vec3(0,e,0)),\n"
    "                        scene_sdf(p+vec3(0,0,e))-scene_sdf(p-vec3(0,0,e)))); }\n"
    /* Hard-ish shadow ray (sphere-march) from a surface point to a light. */
    "float shadow(vec3 o,vec3 dir,float maxd){ float res=1.0,t=0.08;\n"
    "  for(int i=0;i<32 && t<maxd;++i){ float h=scene_sdf(o+dir*t);\n"
    "    if(h<0.02) return 0.0; res=min(res,u_soft*h/t); t+=clamp(h,0.03,0.4); }\n"
    "  return clamp(res,0.0,1.0); }\n"
    /* Distance along dir at which a CONE of footprint-slope k (radius = k*t) is\n"
     * first blocked by the SDF (clearance h < k*t). k=0 -> a plain ray (exact hit\n"
     * distance). A wider cone is blocked nearer than a ray, so a few cones of\n"
     * increasing k sample the depth distribution's near tail cheaply -- no dense\n"
     * ray fan needed to estimate mean + variance for the DDGI depth probe. */
    "float cone_block(vec3 o, vec3 dir, float k){ float t=0.1;\n"
    "  for(int i=0;i<48;++i){ vec3 p=o+dir*t; float h=scene_sdf(p);\n"
    "    if(h < k*t + 0.015) return t;\n"
    "    t += max(h,0.05); if(t>30.0) break; }\n"
    "  return 30.0; }\n"
    /* Octahedral map: unit direction <-> [0,1]^2 (equal-area-ish, seamless). */
    "vec2 oct_encode(vec3 d){ d/=(abs(d.x)+abs(d.y)+abs(d.z));\n"
    "  vec2 o=(d.z>=0.0)? d.xy : (vec2(1.0)-abs(d.yx))*vec2(d.x>=0.0?1.0:-1.0, d.y>=0.0?1.0:-1.0);\n"
    "  return o*0.5+0.5; }\n"
    "vec3 oct_decode(vec2 f){ vec2 e=f*2.0-1.0; vec3 v=vec3(e, 1.0-abs(e.x)-abs(e.y));\n"
    "  if(v.z<0.0) v.xy=(vec2(1.0)-abs(v.yx))*vec2(v.x>=0.0?1.0:-1.0, v.y>=0.0?1.0:-1.0);\n"
    "  return normalize(v); }\n"
    /* Direct irradiance at a surface point p (normal n) from all dynamic lights. */
    "vec3 direct_at(vec3 p,vec3 n){ vec3 sum=vec3(0.0);\n"
    "  for(int l=0;l<u_nlights;++l){ vec4 a=lt[l*4],b=lt[l*4+1],c=lt[l*4+2],e=lt[l*4+3];\n"
    "    int kind=int(a.x); vec3 lpos=a.yzw; vec3 ldir=b.xyz; float range=b.w; vec3 col=c.xyz; float ci=c.w,co=e.x;\n"
    "    vec3 dir; float maxd; float atten=1.0;\n"
    "    if(kind==0){ float ll=length(ldir); if(ll<1e-6) continue; dir=-ldir/ll; maxd=1e4; }\n"
    "    else { vec3 to=lpos-p; float dd=length(to); if(dd<1e-4) continue; dir=to/dd; maxd=dd;\n"
    "      float x=dd/max(range,1e-4); if(x>=1.0) continue; float f=1.0-x*x; atten=f*f;\n"
    "      if(kind==2){ float ll=length(ldir); if(ll<1e-6) continue; float cd=dot(-dir,ldir)/ll;\n"
    "        float tt=(cd-co)/(ci-co+1e-6); if(tt<=0.0) continue; tt=clamp(tt,0.0,1.0); atten*=tt*tt*(3.0-2.0*tt); } }\n"
    "    float ndl=max(dot(n,dir),0.0); if(ndl<=0.0) continue;\n"
    "    float vis=shadow(p,dir,maxd); if(vis<=0.0) continue;\n"
    "    sum += col*atten*ndl*vis; }\n"
    "  return sum; }\n"
    /* Cone-trace the SDF from the probe along dir; at the hit, return the diffuse\n"
     * radiance the surface bounces back toward the probe. Miss -> 0 (no sky). */
    /* @p rdyn = the DYNAMIC-light bounce, @p rstat = the baked STATIC (lightmap)\n"
     * bounce. Kept separate so the forward+ can weight them per object: static\n"
     * surfaces already carry the lightmap (so want little/none of rstat), while\n"
     * dynamic objects have NO baked GI and rely on rstat entirely. */
    "void trace(vec3 o,vec3 dir, out vec3 rdyn, out vec3 rstat){\n"
    "  rdyn=vec3(0.0); rstat=vec3(0.0); float t=0.12;\n"
    "  for(int i=0;i<64;++i){ vec3 p=o+dir*t; float h=scene_sdf(p);\n"
    "    if(h<0.03){ vec3 n=sdf_normal(p); if(dot(n,dir)>0.0) n=-n;\n"
    /* Tint the bounce by the surface's voxelised albedo; the cone footprint\n"
     * widens with t, so read a coarser albedo mip further out. u_albedo scales. */
    "      float lod = clamp(log2(1.0 + t*1.5), 0.0, 5.0);\n"
    "      vec3 alb = scene_albedo(p - n*u_sdf_vox[0]*0.5, lod);\n"
    /* SDF occlusion: the cone sphere-marched the SDF to reach p, so this surface\n"
     * is already visible to the probe. The irradiance volume is sampled on the\n"
     * PROBE-FACING side (p + n*..., n points back toward the probe) so a thin\n"
     * wall's far (unlit/occluded) side can never bleed through -- reading into\n"
     * the solid (-n) would cross the wall and leak. */
    "      vec3 Es = static_irr(p + n*u_static_vox*0.5);\n"
    "      rdyn  = u_albedo * alb * direct_at(p+n*0.06, n) / PI;\n"
    "      rstat = u_albedo * alb * (u_static_k*Es) / PI;\n"
    "      return; }\n"
    "    t += max(h,0.04); if(t>25.0) break; }\n"
    "}\n"
    /* Linear SH (band 0 + band 1 = 4 coeffs) -- cheap, ample for diffuse indirect. */
    "void sh_basis(vec3 d, out float y[4]){\n"
    "  y[0]=0.282094792; y[1]=0.488602512*d.y; y[2]=0.488602512*d.z; y[3]=0.488602512*d.x; }\n"
    "void main(){ uint gid=gl_GlobalInvocationID.x; if(gid>=uint(u_nprobes)) return;\n"
    /* Two SH4 sets per probe: [0..11] dynamic, [12..23] static. */
    "  vec3 o=ppos[gid].xyz; float shd[12]; float shs[12];\n"
    "  for(int k=0;k<12;++k){ shd[k]=0.0; shs[k]=0.0; }\n"
    "  float ga=2.399963229728653; float w=4.0*PI/float(u_ncones);\n"
    /* SG specular lobe moments: luminance-weighted mean direction + total colour. */
    "  vec3 sgVec=vec3(0.0), sgCol=vec3(0.0); float sgLum=0.0;\n"
    /* Fibonacci sphere of directions: gather one-bounce indirect over the sphere. */
    "  for(int s=0;s<u_ncones;++s){ float z=1.0-(2.0*float(s)+1.0)/float(u_ncones);\n"
    "    float r=sqrt(max(0.0,1.0-z*z)); float phi=ga*float(s);\n"
    "    vec3 dir=vec3(r*cos(phi), r*sin(phi), z);\n"
    "    vec3 rd, rs; trace(o,dir, rd, rs);\n"
    "    vec3 col=rd+rs; float lum=dot(col, vec3(0.2126,0.7152,0.0722));\n"
    "    sgVec+=lum*dir; sgCol+=col; sgLum+=lum;\n"
    "    float y[4]; sh_basis(dir,y);\n"
    "    for(int k=0;k<4;++k){ shd[k]+=rd.r*y[k]*w; shd[4+k]+=rd.g*y[k]*w; shd[8+k]+=rd.b*y[k]*w;\n"
    "                          shs[k]+=rs.r*y[k]*w; shs[4+k]+=rs.g*y[k]*w; shs[8+k]+=rs.b*y[k]*w; } }\n"
    "  for(int k=0;k<12;++k){ psh[gid*24+k]=shd[k]; psh[gid*24+12+k]=shs[k]; }\n"
    /* Moment-match one SG lobe: mean resultant length Rbar -> vMF sharpness; axis =\n"
     * dominant radiance direction; amplitude = mean radiance scaled by the lobe's\n"
     * peakiness so the SG reconstructs the environment's bright direction. */
    "  vec3 axis=vec3(0.0,1.0,0.0); float kappa=1.0;\n"
    "  if(sgLum>1e-5){ float ml=length(sgVec); float Rbar=clamp(ml/sgLum,0.0,0.999);\n"
    "    axis=(ml>1e-5)? sgVec/ml : axis;\n"
    "    kappa=Rbar*(3.0-Rbar*Rbar)/max(1.0-Rbar*Rbar,1e-3); kappa=clamp(kappa,0.5,40.0); }\n"
    "  vec3 amp=sgCol*(kappa/(float(u_ncones)))*2.0;\n"   /* heuristic peak scaling. */
    "  int gi5=int(gid)*8;\n"
    "  psg[gi5+0]=axis.x; psg[gi5+1]=axis.y; psg[gi5+2]=axis.z; psg[gi5+3]=kappa;\n"
    "  psg[gi5+4]=amp.r; psg[gi5+5]=amp.g; psg[gi5+6]=amp.b; psg[gi5+7]=0.0;\n"
    /* DDGI depth probe. Per octahedral texel (a direction), estimate the depth\n"
     * distribution from 3 cone-traces: a ray (k=0) gives the MEAN (median sample),\n"
     * a wide + a narrow cone are blocked in the near tail. Treat each cone radius\n"
     * as a normal-CDF quantile (z = probit(p)); the ray is z=0 so it fixes the\n"
     * mean, and each cone gives sigma = (mean - d_cone)/|z|. Store mean + meanSq\n"
     * (= mean^2 + sigma^2) so the sampler can form the variance for Chebyshev. */
    "  const int OR=8;\n"
    "  for(int ty=0;ty<OR;++ty) for(int tx=0;tx<OR;++tx){\n"
    "    vec3 td=oct_decode((vec2(float(tx),float(ty))+0.5)/float(OR));\n"
    "    float dr=cone_block(o,td,0.0);\n"     /* ray  -> mean. */
    /* Shrink the cones when geometry is CLOSE in this direction: a fixed-slope\n"
     * cone at a tight crease (roof edge) is blocked almost immediately and wildly\n"
     * over-estimates variance -> leaks. Scale the radius by the ray distance so\n"
     * near creases use tight cones and don't subsample the SDF. Lowered base\n"
     * slopes too. */
    "    float rs=clamp(dr*0.4, 0.18, 1.0);\n"
    "    float dw=cone_block(o,td,0.22*rs);\n" /* wide -> p~0.15, z~-1.036. */
    "    float dn=cone_block(o,td,0.09*rs);\n" /* narrow -> p~0.30, z~-0.524. */
    "    float mean=dr;\n"
    "    float sig=0.5*((mean-dw)/1.036 + (mean-dn)/0.524); sig=clamp(sig,0.02,mean);\n"
    "    int di=(int(gid)*OR*OR + ty*OR+tx)*2;\n"
    "    pdepth[di]=mean; pdepth[di+1]=mean*mean+sig*sig; } }\n";

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
    glGenBuffers(1, &g->b_depth);
    glGenBuffers(1, &g->b_sg);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_pos);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_sh);
    /* 24 floats/probe: SH4 dynamic [0..11] + SH4 static [12..23] (rpg-pau4). */
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 24 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_lights);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)g->max_lights * 4 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_boxes);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)g->max_boxes * 2 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);

    /* Texture buffers so the forward+ material can sample probe SH + positions. */
    glGenTextures(1, &g->tbo_sh_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_sh_tex);
    /* RGBA32F view: each probe is 24 floats = 6 vec4 texels -- 3 for the dynamic
     * SH4 (R,G,B coeffs) then 3 for the static SH4 (rpg-pau4). */
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g->b_sh);
    glGenTextures(1, &g->tbo_pos_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_pos_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g->b_pos);

    /* DDGI depth: 8x8 octahedral texels/probe, 2 floats (mean, meanSq) each. */
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_depth);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 64 * 2 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glGenTextures(1, &g->tbo_depth_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_depth_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, g->b_depth);

    /* SG specular lobe: 8 floats/probe (2 RGBA32F texels). */
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_sg);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 8 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glGenTextures(1, &g->tbo_sg_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_sg_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g->b_sg);

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

void gi_probe_gpu_set_static(gi_probe_gpu_t *g, unsigned int tex,
                             const float origin[3], const float dim[3],
                             float vox, float k)
{
    if (g == NULL) return;
    g->static_tex = tex;
    g->static_vox = vox;
    g->static_k = k;
    for (int i = 0; i < 3; ++i) {
        g->static_origin[i] = origin ? origin[i] : 0.0f;
        g->static_dim[i] = dim ? dim[i] : 0.0f;
    }
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
    glUniform1i(glGetUniformLocation(g->prog, "u_ncones"), 8);    /* sphere samples. */
    glUniform1f(glGetUniformLocation(g->prog, "u_albedo"), 1.0f); /* gain; real albedo now from the voxel map. */

    /* Static irradiance volume: bound past the SDF slot units. */
    {
        int on = (g->static_tex != 0) ? 1 : 0;
        int unit = GI_SDF_UNIT_BASE + GI_SDF_MAX_RESIDENT; /* first unit after the SDF chunks. */
        glUniform1i(glGetUniformLocation(g->prog, "u_static_on"), on);
        glUniform1f(glGetUniformLocation(g->prog, "u_static_k"), g->static_k);
        glUniform1i(glGetUniformLocation(g->prog, "u_static_irr"), unit);
        glUniform3fv(glGetUniformLocation(g->prog, "u_static_origin"), 1, g->static_origin);
        glUniform3fv(glGetUniformLocation(g->prog, "u_static_dim"), 1, g->static_dim);
        glUniform1f(glGetUniformLocation(g->prog, "u_static_vox"), g->static_vox);
        glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
        glBindTexture(GL_TEXTURE_3D, on ? g->static_tex : 0);
    }

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
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 4, g->b_depth);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 5, g->b_sg);

    g->DispatchCompute((g->n_probes + 63u) / 64u, 1u, 1u);
    g->MemoryBarrier(GI_GL_SHADER_STORAGE_BARRIER_BIT | GI_GL_TEXTURE_FETCH_BARRIER_BIT);
}

unsigned int gi_probe_gpu_sh_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_sh_tex : 0u; }
unsigned int gi_probe_gpu_pos_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_pos_tex : 0u; }
unsigned int gi_probe_gpu_depth_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_depth_tex : 0u; }
unsigned int gi_probe_gpu_sg_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_sg_tex : 0u; }

void gi_probe_gpu_destroy(gi_probe_gpu_t *g)
{
    if (g == NULL) return;
    if (g->prog) glDeleteProgram(g->prog);
    GLuint b[6] = { g->b_pos, g->b_sh, g->b_lights, g->b_boxes, g->b_depth, g->b_sg };
    glDeleteBuffers(6, b);
    if (g->tbo_sh_tex) glDeleteTextures(1, &g->tbo_sh_tex);
    if (g->tbo_pos_tex) glDeleteTextures(1, &g->tbo_pos_tex);
    if (g->tbo_depth_tex) glDeleteTextures(1, &g->tbo_depth_tex);
    if (g->tbo_sg_tex) glDeleteTextures(1, &g->tbo_sg_tex);
    memset(g, 0, sizeof *g);
}
