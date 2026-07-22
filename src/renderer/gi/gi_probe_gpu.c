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
#define GI_GL_READ_WRITE 0x88BA
#define GI_SDF_UNIT_BASE 8   /* SDF resident textures bound to units 8.. */

static const char *CS_SRC =
    "#version 430\n"
    "layout(local_size_x=64) in;\n"
    "uniform int u_nprobes,u_nlights,u_nboxes,u_ncones;\n"
    /* Staggered updates (rpg-iuiy): only probes in the active group this frame\n"
     * re-trace; a spatial-DITHER hash of the probe index scatters each group\n"
     * across the whole volume so every region gets a few refreshes per frame.\n"
     * u_ngroups<=1 disables (every probe every dispatch). */
    "uniform int u_ngroups,u_group;\n"
    "uniform ivec3 u_grid_dim;\n"    /* probe grid dims (0 -> not a grid, hash instead). */
    "uniform vec3 u_grid_origin, u_grid_cell;\n" /* probe grid layout (for the field gather). */
    "uniform int u_field_on;\n"      /* 1 = DDGI recurrent gather from the probe field. */
    "uniform float u_near_dist;\n"   /* hits closer than this sample voxels+direct; farther rely on the field. */
    /* Two-pass stochastic probe-radiosity (rpg-3c6g). Pass 0 classifies every probe\n"
     * (octahedral occlusion + nearest-surface distance) and injects near-field direct\n"
     * light, compacting SOURCE probes (near a surface / emissive) into a scratch\n"
     * list; pass 1 has every probe stochastically gather bounced radiance from a\n"
     * random visible subset of that list -> light propagates GLOBALLY per pass, not\n"
     * cell-by-cell. u_field_on switches this on (0 = old full-march baseline). */
    "uniform int u_pass;\n"          /* 0 = classify+inject, 1 = stochastic gather. */
    "uniform int u_seed;\n"          /* RNG salt (per dispatch tick). */
    "uniform float u_dmax;\n"        /* nearest-surface distance under which a probe is a SOURCE. */
    "uniform float u_emin;\n"        /* emission luminance over which a probe is a SOURCE. */
    "uniform int u_nsamp;\n"         /* stochastic source samples per gather. */
    "uniform float u_bounce;\n"      /* bounce gain (GI_BOUNCE). */
    "uniform float u_ray_clamp;\n"   /* per-ray radiance cap (firefly clamp). */
    "uniform float u_soft, u_albedo;\n"
    "uniform float u_temporal;\n"   /* EMA blend: 1 = replace, <1 = smooth over time. */
    /* MIS-importance-sampled march directions (GI_MIS): build a per-probe pdf over the\n"
     * 64 octahedral texels from hit-likelihood (depth) x N.L (sample dir . the probe's\n"
     * OWN surface normal) x a radiance guide, and draw the SVO march rays from it so\n"
     * they concentrate where light actually arrives instead of a uniform sphere. */
    "uniform int u_mis;\n"
    "uniform float u_norm_gate;\n" /* |scene_sdf(probe)| below this => probe is a SURFACE probe (has a normal). */
    /* Hybrid field gather (GI_HYBRID): cheap probe-field bounce PLUS u_hero DETERMINISTIC\n"
     * 'hero' SDF marches toward the brightest, unoccluded, high-N.L source probes --\n"
     * accurate first-bounce detail the diffuse field smears, and deterministic so it\n"
     * doesn't flicker frame-to-frame. Heroes are excluded from the field average so\n"
     * their energy isn't double-counted. */
    "uniform int u_hybrid;\n"
    "uniform int u_hero;\n"          /* number of hero SDF marches (0..4). */
    "uniform sampler3D u_sdf[16];\n"
    "uniform vec3 u_sdf_origin[16];\n"
    "uniform vec3 u_sdf_dim[16];\n"
    "uniform float u_sdf_vox[16];\n"
    "uniform int u_sdf_active[16];\n"
    /* GLOBAL low-res ZONE SDF: the page-fault fallback. Sampled wherever no fine\n"
     * chunk is resident so occlusion + albedo never read as empty space. */
    "uniform sampler3D u_zone_sdf;\n"
    "uniform vec3 u_zone_origin;\n"
    "uniform vec3 u_zone_dim;\n"
    "uniform float u_zone_vox;\n"
    "uniform int u_zone_on;\n"
    /* Brick field lookup: voxel -> brick -> 8 of its 64 probes (rpg-pjkb). */
    "uniform int u_cbrick_on;\n"
    "uniform isampler3D u_cbrick_index;\n"
    "uniform samplerBuffer u_cbrick_meta;\n"
    "uniform usamplerBuffer u_cbrick_pidx;\n"
    "uniform usamplerBuffer u_cbrick_valid;\n"
    "uniform vec3 u_cbrick_origin;\n"
    "uniform float u_cbrick_voxel;\n"
    "uniform vec3 u_cbrick_dim;\n"
    /* Static irradiance volume (rpg-pau4): the baked lightmap E, splatted to a\n"
     * coarse world grid. A cone that hits a surface gathers this so the probe\n"
     * carries the STATIC bounced ambience (one bounce beyond the lightmapper),\n"
     * not just the dynamic-light term. u_static_k boosts it. */
    /* SPARSE DYNAMIC ALBEDO VOLUME: a low-res (probe-scale) RGBA volume over the GI\n"
     * AABB, cleared + injected each probe update with the DYNAMIC objects' real\n"
     * material albedo (rgb) + coverage (a). Dynamic geometry is deliberately NOT in\n"
     * the baked SDF, so without this a trace that hits a dynamic object only sees the\n"
     * collider's distance -- i.e. occlusion only -- and bounces the neutral grey\n"
     * fallback. Sampling this gives the hit its true colour, so a red cloth banner\n"
     * bleeds RED into the probe field. */
    "uniform sampler3D u_dyn_alb;\n"
    "uniform vec3 u_dyn_origin;\n"
    "uniform vec3 u_dyn_dim;\n"
    "uniform float u_dyn_vox;\n"
    "uniform int u_dyn_on;\n"
    "uniform float u_dyn_gain;\n"  /* dynamic-albedo bleed gain (art lever). */
    "uniform sampler3D u_static_irr;\n"
    "uniform vec3 u_static_origin;\n"
    "uniform vec3 u_static_dim;\n"
    "uniform float u_static_vox;\n"
    "uniform int u_static_on;\n"
    "uniform float u_static_k;\n"
    /* Scale on the probe's STATIC bounce gather (rstat): with the baked lightmap\n"
     * re-enabled the static GI is already on-screen, so dim (don't hard-cut) what the\n"
     * probes re-gather -- they keep the DYNAMIC color bleed, add only a little static. */
    "uniform float u_stat_scale;\n"
    "layout(std430,binding=0) readonly buffer PP { vec4 ppos[]; };\n"
    "layout(std430,binding=1) buffer PS { float psh[]; };\n"        /* r/w for temporal blend. */
    "layout(std430,binding=2) readonly buffer LB { vec4 lt[]; };\n"
    "layout(std430,binding=3) readonly buffer BX { vec4 bx[]; };\n"
    /* DDGI depth probe: OCTRES^2 texels/probe, each (mean dist, mean sq dist)\n"
     * for the Chebyshev visibility test at interpolation time. OCTRES fixed at 8\n"
     * (must match the forward+ sampler). */
    "layout(std430,binding=4) buffer PD { float pdepth[]; };\n"       /* r/w for temporal blend. */
    /* One spherical-Gaussian specular lobe per probe (rpg-hw75): 8 floats =\n"
     * axis.xyz + sharpness, then rgb amplitude + pad. Fit by moment-matching the\n"
     * traced radiance so glossy surfaces get a cheap directional reflection. */
    "layout(std430,binding=5) buffer PG { float psg[]; };\n"          /* r/w for temporal blend. */
    /* Stochastic-radiosity scratch: a compact list of SOURCE probe indices (those\n"
     * near a surface / emissive), rebuilt each pass-0 via an atomic counter. */
    "layout(std430,binding=6) buffer AL { uint acount; uint alist[]; };\n"
    /* Per-probe direct-light injection SH (24 floats: dyn[0..11]+stat[12..23]),\n"
     * written in pass 0, used as the gather's starting radiance in pass 1. */
    "layout(std430,binding=7) buffer EM { float emit[]; };\n"
    /* Per-probe surface normal (xyz) + validity (w): the SDF normal at the probe,\n"
     * cached in the depth prepass only when the probe is near a surface (w=1). Gates\n"
     * the MIS march pdf (N.L) and is the shading normal for cheap field bounces. */
    "layout(std430,binding=8) buffer PN { vec4 pnrm[]; };\n"
    /* Hardware-filterable mirror of the depth (image unit 0): the compute writes\n"
     * the same octahedral (mean, meanSq) it puts in pdepth here, so the forward+\n"
     * pass can sample it with GL_LINEAR (one bilinear tap in probe_vis). */
    /* Oct-depth ATLAS: 10x10-texel tiles (8x8 interior + 1px oct-wrap gutter),\n"
     * 256 tiles per row. A 2D texture has no 2048-layer cap (the 2D-array\n"
     * version silently broke ALL visibility past GL_MAX_ARRAY_TEXTURE_LAYERS). */
    "layout(rg32f,binding=0) uniform image2D u_depth_img;\n"
    "#define DEPTH_TILES_X 256\n"
    "ivec2 depth_tile_origin(uint gid){ return ivec2(int(gid%uint(DEPTH_TILES_X))*10, int(gid/uint(DEPTH_TILES_X))*10); }\n"
    "const float PI=3.14159265;\n"
    "float box_sdf(vec3 p,vec3 c,vec3 h){ vec3 q=abs(p-c)-h; return length(max(q,vec3(0.0)))+min(max(q.x,max(q.y,q.z)),0.0); }\n"
    /* Capsule = distance to segment a..b minus radius r (posed bone limbs, rpg-85as). */
    "float capsule_sdf(vec3 p,vec3 a,vec3 b,float r){ vec3 pa=p-a, ba=b-a;\n"
    "  float h=clamp(dot(pa,ba)/max(dot(ba,ba),1e-6),0.0,1.0); return length(pa-ba*h)-r; }\n"
    /* Distance is the ALPHA of the RGBA voxel texture (rgb = static albedo). */
    "float scene_sdf(vec3 p){ float d=1e30; bool cov=false;\n"
    "  for(int i=0;i<16;++i){ if(u_sdf_active[i]==0) continue;\n"
    "    vec3 g=(p-u_sdf_origin[i])/u_sdf_vox[i];\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_sdf_dim[i]))){\n"
    "      cov=true; vec3 uvw=(g+0.5)/u_sdf_dim[i]; d=min(d, texture(u_sdf[i],uvw).a); } }\n"
    /* PAGE FAULT: no resident fine chunk covers p -> the coarse zone field keeps\n"
     * occlusion alive instead of returning empty space. */
    "  if(!cov && u_zone_on!=0){ vec3 g=(p-u_zone_origin)/u_zone_vox;\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_zone_dim))){\n"
    "      vec3 uvw=(g+0.5)/u_zone_dim; d=min(d, texture(u_zone_sdf,uvw).a); } }\n"
    /* Colliders: bx[i*2].w = kind (0=sphere,1=box,2=capsule). Sphere: radius in\n"
     * bx[i*2+1].w. Box: half-extents in bx[i*2+1].xyz. Capsule: endpoint B in\n"
     * bx[i*2+1].xyz, radius in .w. (rpg-85as: posed capsule/sphere proxies.) */
    "  for(int i=0;i<u_nboxes;++i){ int bk=int(bx[i*2].w+0.5);\n"
    "    if(bk==2) d=min(d, capsule_sdf(p, bx[i*2].xyz, bx[i*2+1].xyz, bx[i*2+1].w));\n"
    "    else if(bk==0) d=min(d, length(p-bx[i*2].xyz)-bx[i*2+1].w);\n"
    "    else d=min(d, box_sdf(p,bx[i*2].xyz,bx[i*2+1].xyz)); }\n"
    "  return d; }\n"
    /* Voxelised static albedo at p, sampled at mip @p lod (cone footprint). Dynamic\n"
     * collider boxes have no baked albedo -> neutral grey fallback. */
    /* Dynamic albedo lookup: rgb = injected albedo, a = coverage (0 = no dynamic
     * geometry in this voxel). Trilinear so a coarse volume still reads smoothly. */
    "vec4 dyn_alb_at(vec3 p){ if(u_dyn_on==0) return vec4(0.0);\n"
    "  vec3 g=(p-u_dyn_origin)/u_dyn_vox;\n"
    "  if(any(lessThan(g,vec3(0.0)))||any(greaterThanEqual(g,u_dyn_dim))) return vec4(0.0);\n"
    /* Writer convention: the voxelizer stamps texel k for world in\n"
     * [origin+k*vox, origin+(k+1)*vox) -- texel k's CONTENT is centred at\n"
     * (k+0.5)*vox. Sampling with a +0.5 texel shift read every voxel HALF A\n"
     * VOXEL off (the banner's bleed landed on the wall beside it). uv = g/dim\n"
     * samples texel centres exactly where the writer put them. */
    "  return texture(u_dyn_alb, g/u_dyn_dim); }\n"
    "vec3 scene_albedo(vec3 p, float lod){\n"
    /* Dynamic objects win: they are not in the baked voxel albedo at all. */
    "  { vec4 dc=dyn_alb_at(p); if(dc.a>0.02) return u_dyn_gain*dc.rgb/max(dc.a,1e-3); }\n"
    "  for(int i=0;i<16;++i){ if(u_sdf_active[i]==0) continue;\n"
    "    vec3 g=(p-u_sdf_origin[i])/u_sdf_vox[i];\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_sdf_dim[i]))){\n"
    "      vec3 uvw=(g+0.5)/u_sdf_dim[i]; return textureLod(u_sdf[i],uvw,lod).rgb; } }\n"
    /* Page-fault albedo: the zone field's nearest-surface colour beats flat grey. */
    "  if(u_zone_on!=0){ vec3 g=(p-u_zone_origin)/u_zone_vox;\n"
    "    if(all(greaterThanEqual(g,vec3(0.0)))&&all(lessThan(g,u_zone_dim))){\n"
    "      vec3 uvw=(g+0.5)/u_zone_dim; return texture(u_zone_sdf,uvw).rgb; } }\n"
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
    /* Cache the probe's OWN surface normal, gated by proximity: a probe within\n"
     * u_norm_gate of a surface is a SURFACE probe -> store sdf_normal(o) (points\n"
     * away from the surface) with w=1; an open-air probe stores w=0 (omni sampling). */
    "void store_probe_normal(uint gid, vec3 o){ float sd=scene_sdf(o);\n"
    "  vec4 r=vec4(0.0); if(abs(sd)<u_norm_gate){ r=vec4(sdf_normal(o),1.0); } pnrm[gid]=r; }\n"
    "vec4 probe_normal(uint gid){ return pnrm[gid]; }\n"
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
    /* Hit distance along @p dir from probe @p self's OWN octahedral depth map\n"
     * (the mean stored last frame -> no per-ray SDF march). This is the Chebyshev\n"
     * depth-probe data we already compute for the forward+ visibility test, reused\n"
     * here as a cheap ray-cast. Nearest texel (8x8 oct); big value == open/miss. */
    "float probe_hit_dist(uint self, vec3 dir){\n"
    "  vec2 uv=oct_encode(dir); int tx=clamp(int(uv.x*8.0),0,7); int ty=clamp(int(uv.y*8.0),0,7);\n"
    "  return pdepth[(int(self)*64 + ty*8+tx)*2]; }\n"
    /* SH4 irradiance (Ramamoorthi cosine convolution) of a probe's stored radiance\n"
     * SH in direction @p n. @p base is the float offset into psh (dyn = probe*24,\n"
     * stat = probe*24+12). This is the radiance a diffuse probe emits toward n. */
    "vec3 psh_irr(int base, vec3 n){\n"
    "  vec4 A=vec4(3.14159265,2.0943951,2.0943951,2.0943951);\n"
    "  vec4 AY=A*vec4(0.282094792,0.488602512*n.y,0.488602512*n.z,0.488602512*n.x);\n"
    "  return vec3(dot(vec4(psh[base],psh[base+1],psh[base+2],psh[base+3]),AY),\n"
    "              dot(vec4(psh[base+4],psh[base+5],psh[base+6],psh[base+7]),AY),\n"
    "              dot(vec4(psh[base+8],psh[base+9],psh[base+10],psh[base+11]),AY)); }\n"
    /* Chebyshev visibility from probe @p self's octahedral depth toward a point at\n"
     * distance @p dist along @p dir: 1 if the line to it is clear, softly ->0 if a\n"
     * surface (the stored mean depth) lies between. Reuses the DDGI depth probe. */
    "float probe_vis(uint self, vec3 dir, float dist){\n"
    "  vec2 uv=oct_encode(dir); int tx=clamp(int(uv.x*8.0),0,7); int ty=clamp(int(uv.y*8.0),0,7);\n"
    "  int di=(int(self)*64 + ty*8+tx)*2; float mean=pdepth[di];\n"
    "  if(dist<=mean+0.15) return 1.0;\n"
    "  float var=max(pdepth[di+1]-mean*mean,1e-3); float d=dist-mean; float ch=var/(var+d*d);\n"
    "  return clamp(ch*ch,0.0,1.0); }\n"
    /* PCG-ish hash RNG (per probe x sample x frame); returns [0,1). */
    "float rnd(inout uint s){ s=s*747796405u+2891336453u; uint w=((s>>((s>>28)+4u))^s)*277803737u; w=(w>>22)^w; return float(w)*2.3283064365e-10; }\n"
    /* Cone-trace the SDF from the probe along dir; at the hit, return the diffuse\n"
     * radiance the surface bounces back toward the probe. Miss -> 0 (no sky).\n"
     * @p rdyn = DYNAMIC-light bounce, @p rstat = baked STATIC (lightmap) bounce --\n"
     * kept separate so the forward+ can weight them per object. Used both by the OLD\n"
     * full-march baseline and, restricted to NEAR hits, by the pass-1 light inject. */
    /* Indirect irradiance at world p (normal n) sampled from the probe FIELD:\n"
     * trilinear over the 8 grid-neighbour probes, each Chebyshev-visibility weighted\n"
     * (occluders block leak). This is last-frame's converged probe irradiance, so\n"
     * adding albedo*field_irr at a trace hit gives the surface's INDIRECT incoming --\n"
     * i.e. one more bounce per gather; temporal recurrence -> infinite bounces. 0 if\n"
     * there is no probe grid (manual-only probes) or p is outside it. */
    "vec3 field_irr(vec3 p, vec3 n){\n"
    /* BRICK path (adaptive sets): one integer fetch resolves the covering brick,\n"
     * its local trilinear cell picks 8 of the 64 probes -- the same structure\n"
     * the forward pass samples, so the recurrent bounce sees the exact field\n"
     * the screen does. Falls through to the dense-grid path when absent. */
    "  if(u_cbrick_on==1){\n"
    "    ivec3 bdim=ivec3(u_cbrick_dim+vec3(0.5));\n"
    "    ivec3 v=ivec3(floor((p-u_cbrick_origin)/u_cbrick_voxel));\n"
    "    v=clamp(v, ivec3(0), bdim-ivec3(1));\n"
    "    int bid=texelFetch(u_cbrick_index, v, 0).r;\n"
    "    if(bid>=0){\n"
    "      vec4 bm=texelFetch(u_cbrick_meta, bid);\n"
    "      float bstep=bm.w/3.0;\n"
    "      vec3 local=clamp((p-bm.xyz)/bstep, vec3(0.0), vec3(2.9999));\n"
    "      ivec3 c0=ivec3(local); vec3 fr=local-vec3(c0);\n"
    "      vec3 E=vec3(0.0); float wsum=0.0;\n"
    "      for(int c=0;c<8;++c){ ivec3 oo=ivec3(c&1,(c>>1)&1,(c>>2)&1);\n"
    "        vec3 wv=mix(vec3(1.0)-fr, fr, vec3(oo));\n"
    "        float w=wv.x*wv.y*wv.z; if(w<=0.0) continue;\n"
    "        ivec3 lc=c0+oo; int li=(lc.z*4+lc.y)*4+lc.x;\n"
    "        int idx=int(texelFetch(u_cbrick_pidx, bid*64+li).r);\n"
    "        if(idx<0||idx>=u_nprobes) continue;\n"
    "        w*=float(texelFetch(u_cbrick_valid, idx).r);\n"
    "        vec3 pd=p-ppos[idx].xyz; float dist=length(pd);\n"
    "        if(dist>1e-3) w*=probe_vis(uint(idx), pd/dist, dist);\n"
    "        if(w<=0.0) continue;\n"
    "        E += w*max(psh_irr(idx*24,n)+psh_irr(idx*24+12,n), vec3(0.0)); wsum+=w; }\n"
    "      return (wsum>1e-4)? E/wsum : vec3(0.0);\n"
    "    }\n"
    "    return vec3(0.0);\n"
    "  }\n"
    "  if(u_grid_dim.x<=0) return vec3(0.0);\n"
    "  vec3 g=(p-u_grid_origin)/u_grid_cell; vec3 f=fract(g); ivec3 b=ivec3(floor(g));\n"
    "  vec3 E=vec3(0.0); float wsum=0.0;\n"
    "  for(int i=0;i<8;++i){ ivec3 od=ivec3(i&1,(i>>1)&1,(i>>2)&1);\n"
    "    ivec3 c=clamp(b+od, ivec3(0), u_grid_dim-ivec3(1));\n"
    "    int idx=(c.z*u_grid_dim.y+c.y)*u_grid_dim.x+c.x; if(idx<0||idx>=u_nprobes) continue;\n"
    "    float tw=(od.x==1?f.x:1.0-f.x)*(od.y==1?f.y:1.0-f.y)*(od.z==1?f.z:1.0-f.z);\n"
    "    vec3 pd=p-ppos[idx].xyz; float dist=length(pd); float vis=1.0;\n"
    "    if(dist>1e-3){ vis=probe_vis(uint(idx), pd/dist, dist); }\n"
    "    float w=tw*vis; if(w<=0.0) continue;\n"
    "    E += w*max(psh_irr(idx*24,n)+psh_irr(idx*24+12,n), vec3(0.0)); wsum+=w; }\n"
    "  return (wsum>1e-4)? E/wsum : vec3(0.0); }\n"
    "void trace(uint self, vec3 o,vec3 dir, out vec3 rdyn, out vec3 rstat){\n"
    "  rdyn=vec3(0.0); rstat=vec3(0.0);\n"
    "  float t=0.12;\n"
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
    /* INDIRECT incoming from the probe field = one more bounce per gather. It is a
     * FEEDBACK path (field -> hit -> back into the probe), so it must carry the same
     * per-bounce decay as the probe-to-probe gather: the recurrence only converges
     * while the total transport gain stays < 1. Feeding it back undecayed (albedo
     * alone) pushes the gain to ~1 and the GI piles up frame over frame -- it reads
     * as everything being far too bright. */
    "      vec3 Ei = u_bounce * field_irr(p + n*0.05, n);\n"
    "      rdyn  = u_albedo * alb * (direct_at(p+n*0.06, n) + Ei) / PI;\n"
    "      rstat = u_stat_scale * u_albedo * alb * (u_static_k*Es) / PI;\n"
    "      return; }\n"
    "    t += max(h,0.04); if(t>25.0) break; }\n"
    "}\n"
    /* Linear SH (band 0 + band 1 = 4 coeffs) -- cheap, ample for diffuse indirect. */
    "void sh_basis(vec3 d, out float y[4]){\n"
    "  y[0]=0.282094792; y[1]=0.488602512*d.y; y[2]=0.488602512*d.z; y[3]=0.488602512*d.x; }\n"
    /* True if probe gid is in this frame's staggered 3D-checkerboard update group. */
    "bool in_group(uint gid){ if(u_ngroups<=1) return true; int grp;\n"
    "  if(u_grid_dim.x>0){ int W=u_grid_dim.x, H=u_grid_dim.y; int g=int(gid);\n"
    "    ivec3 gc=ivec3(g%W,(g/W)%H,g/(W*H)); grp=(gc.x+gc.y+gc.z)%u_ngroups; }\n"
    "  else { uint h=gid*2654435761u; h^=h>>15; h*=2246822519u; grp=int(h%uint(u_ngroups)); }\n"
    "  return grp==u_group; }\n"
    /* Octahedral depth probe: per texel (direction) estimate the depth distribution\n"
     * from 3 cone-traces -- a ray fixes the MEAN, a wide+narrow cone give the near-\n"
     * tail sigma (each cone radius = a normal-CDF quantile). Store mean + meanSq for\n"
     * Chebyshev. Writes pdepth + the hw-filtered image; RETURNS the min mean depth\n"
     * = distance to the nearest surface (huge if the probe floats in open air). */
    "float compute_depth(uint gid, vec3 o){ const int OR=8; float surf_min=1e30;\n"
    "  for(int ty=0;ty<OR;++ty) for(int tx=0;tx<OR;++tx){\n"
    "    vec3 td=oct_decode((vec2(float(tx),float(ty))+0.5)/float(OR));\n"
    "    float dr=cone_block(o,td,0.0);\n"
    "    float rs=clamp(dr*0.4, 0.18, 1.0);\n"
    "    float dw=cone_block(o,td,0.22*rs);\n"
    "    float dn=cone_block(o,td,0.09*rs);\n"
    "    float mean=dr; float sig=0.5*((mean-dw)/1.036 + (mean-dn)/0.524); sig=clamp(sig,0.02,mean);\n"
    "    int di=(int(gid)*OR*OR + ty*OR+tx)*2;\n"
    "    float nm=mix(pdepth[di], mean, u_temporal);\n"
    "    float nq=mix(pdepth[di+1], mean*mean+sig*sig, u_temporal);\n"
    "    pdepth[di]=nm; pdepth[di+1]=nq; surf_min=min(surf_min,nm);\n"
    "    imageStore(u_depth_img, depth_tile_origin(gid)+ivec2(1+tx,1+ty), vec4(nm,nq,0.0,0.0)); }\n"
    /* Gutter fill (standard DDGI oct-wrap border): bilinear taps at the tile edge\n"
     * then read the WRAPPED texel instead of a neighbouring probe's tile. */
    "  { ivec2 to=depth_tile_origin(gid);\n"
    "    for(int e=1;e<=8;++e){\n"
    "      vec2 L=vec2(pdepth[(int(gid)*64 + (8-e)*8+0)*2], pdepth[(int(gid)*64 + (8-e)*8+0)*2+1]);\n"
    "      vec2 R=vec2(pdepth[(int(gid)*64 + (8-e)*8+7)*2], pdepth[(int(gid)*64 + (8-e)*8+7)*2+1]);\n"
    "      vec2 B=vec2(pdepth[(int(gid)*64 + 0*8+(8-e))*2], pdepth[(int(gid)*64 + 0*8+(8-e))*2+1]);\n"
    "      vec2 T=vec2(pdepth[(int(gid)*64 + 7*8+(8-e))*2], pdepth[(int(gid)*64 + 7*8+(8-e))*2+1]);\n"
    "      imageStore(u_depth_img, to+ivec2(0,e),   vec4(L,0.0,0.0));\n"
    "      imageStore(u_depth_img, to+ivec2(9,e),   vec4(R,0.0,0.0));\n"
    "      imageStore(u_depth_img, to+ivec2(e,0),   vec4(B,0.0,0.0));\n"
    "      imageStore(u_depth_img, to+ivec2(e,9),   vec4(T,0.0,0.0)); }\n"
    "    vec2 c00=vec2(pdepth[(int(gid)*64 + 7*8+7)*2], pdepth[(int(gid)*64 + 7*8+7)*2+1]);\n"
    "    vec2 c10=vec2(pdepth[(int(gid)*64 + 7*8+0)*2], pdepth[(int(gid)*64 + 7*8+0)*2+1]);\n"
    "    vec2 c01=vec2(pdepth[(int(gid)*64 + 0*8+7)*2], pdepth[(int(gid)*64 + 0*8+7)*2+1]);\n"
    "    vec2 c11=vec2(pdepth[(int(gid)*64 + 0*8+0)*2], pdepth[(int(gid)*64 + 0*8+0)*2+1]);\n"
    "    imageStore(u_depth_img, to+ivec2(0,0), vec4(c00,0.0,0.0));\n"
    "    imageStore(u_depth_img, to+ivec2(9,0), vec4(c10,0.0,0.0));\n"
    "    imageStore(u_depth_img, to+ivec2(0,9), vec4(c01,0.0,0.0));\n"
    "    imageStore(u_depth_img, to+ivec2(9,9), vec4(c11,0.0,0.0)); }\n"
    "  return surf_min; }\n"
    /* Greedy multi-lobe SG fit over the per-ray radiance rcol[] (glossy reflection). */
    "void fit_sg(uint gid, vec3 rdir[32], vec3 rcol[32]){ const int NR=32; const int NL=3;\n"
    "  int base=int(gid)*NL*8;\n"
    "  for(int L=0;L<NL;++L){ vec3 mvec=vec3(0.0); float mlum=0.0;\n"
    "    for(int s=0;s<NR;++s){ vec3 c=max(rcol[s],vec3(0.0));\n"
    "      float lum=dot(c,vec3(0.2126,0.7152,0.0722)); mvec+=lum*rdir[s]; mlum+=lum; }\n"
    "    vec3 axis=vec3(0.0,1.0,0.0); float kappa=1.0;\n"
    "    if(mlum>1e-5){ float ml=length(mvec); float Rbar=clamp(ml/mlum,0.0,0.999);\n"
    "      axis=(ml>1e-5)? mvec/ml : axis;\n"
    "      kappa=Rbar*(3.0-Rbar*Rbar)/max(1.0-Rbar*Rbar,1e-3); kappa=clamp(kappa,0.6,60.0); }\n"
    "    vec3 num=vec3(0.0); float den=0.0;\n"
    "    for(int s=0;s<NR;++s){ float g=exp(kappa*(dot(rdir[s],axis)-1.0));\n"
    "      num+=max(rcol[s],vec3(0.0))*g; den+=g; }\n"
    "    vec3 amp=num/max(den,1e-4);\n"
    "    for(int s=0;s<NR;++s){ float g=exp(kappa*(dot(rdir[s],axis)-1.0)); rcol[s]-=amp*g; }\n"
    "    int b=base+L*8; float A=u_temporal;\n"
    "    psg[b+0]=mix(psg[b+0],axis.x,A); psg[b+1]=mix(psg[b+1],axis.y,A); psg[b+2]=mix(psg[b+2],axis.z,A);\n"
    "    psg[b+3]=mix(psg[b+3],kappa,A);\n"
    "    psg[b+4]=mix(psg[b+4],amp.r,A); psg[b+5]=mix(psg[b+5],amp.g,A); psg[b+6]=mix(psg[b+6],amp.b,A); } }\n"
    /* --- PASS 0: octahedral occlusion + near-field direct-light injection + cull ---\n"
     * Build this probe's depth map, then shoot NR rays whose HIT distance comes from\n"
     * that depth map (no per-ray march); deposit surface-albedo * (near direct +\n"
     * static ambience) into emit[gid] (the direct injection). Finally classify: a\n"
     * probe near a surface (surf_min<u_dmax) or itself bright (emit>u_emin) is a\n"
     * SOURCE -> atomically appended to the compact scratch list for pass 1. */
    "void pass_classify(uint gid){ vec3 o=ppos[gid].xyz;\n"
    "  float surf_min = compute_depth(gid,o); store_probe_normal(gid,o);\n"
    "  const int NR=32; float ga=2.399963229728653; float w=4.0*PI/float(NR);\n"
    "  float ed[12]; float es[12]; for(int k=0;k<12;++k){ ed[k]=0.0; es[k]=0.0; }\n"
    "  vec3 rdir[32]; vec3 rcol[32];\n"
    "  for(int s=0;s<NR;++s){ float z=1.0-(2.0*float(s)+1.0)/float(NR);\n"
    "    float r=sqrt(max(0.0,1.0-z*z)); float phi=ga*float(s);\n"
    "    vec3 dir=vec3(r*cos(phi), r*sin(phi), z); rdir[s]=dir; rcol[s]=vec3(0.0);\n"
    "    float t=probe_hit_dist(gid,dir); if(t>=24.0||t<=0.02) continue;\n"
    "    vec3 p=o+dir*t; bool near=(t<u_near_dist);\n"
    "    vec3 n=near?sdf_normal(p):-dir; if(dot(n,dir)>0.0) n=-n;\n"
    "    float lod=clamp(log2(1.0+t*1.5),0.0,5.0);\n"
    "    vec3 alb=scene_albedo(p-n*u_sdf_vox[0]*0.5, lod);\n"
    "    vec3 Es=static_irr(p+n*u_static_vox*0.5);\n"
    "    vec3 direct = near ? direct_at(p+n*0.06,n) : vec3(0.0);\n"
    "    vec3 rd=u_albedo*alb*direct/PI; vec3 rs=u_stat_scale*u_albedo*alb*(u_static_k*Es)/PI; rcol[s]=rd+rs;\n"
    "    float y[4]; sh_basis(dir,y);\n"
    "    for(int k=0;k<4;++k){ ed[k]+=rd.r*y[k]*w; ed[4+k]+=rd.g*y[k]*w; ed[8+k]+=rd.b*y[k]*w;\n"
    "                          es[k]+=rs.r*y[k]*w; es[4+k]+=rs.g*y[k]*w; es[8+k]+=rs.b*y[k]*w; } }\n"
    "  for(int k=0;k<12;++k){ emit[gid*24+k]=ed[k]; emit[gid*24+12+k]=es[k]; }\n"
    "  fit_sg(gid, rdir, rcol);\n"
    "  float elum=dot(vec3(ed[0],ed[4],ed[8])+vec3(es[0],es[4],es[8]), vec3(0.2126,0.7152,0.0722));\n"
    "  if(surf_min<u_dmax || elum>u_emin){ uint idx=atomicAdd(acount,1u); if(idx<uint(u_nprobes)) alist[idx]=gid; } }\n"
    /* --- PASS 1: stochastic probe-to-probe gather ---\n"
     * Start from this probe's fresh direct injection, then take u_nsamp RANDOM source\n"
     * probes from the scratch list; for each visible one (octahedral Chebyshev from\n"
     * THIS probe's depth, so occluders block it) add the radiance it emits toward us\n"
     * (its last-frame irradiance SH). MC-weighted by 1/dist^2 * (count/nsamp): light\n"
     * from anywhere in the scene lands in ONE pass. EMA into psh for stability. */
    "void pass_gather(uint gid){ if(!in_group(gid)) return; vec3 o=ppos[gid].xyz;\n"
    "  float shd[12]; float shs[12];\n"
    "  for(int k=0;k<12;++k){ shd[k]=emit[gid*24+k]; shs[k]=emit[gid*24+12+k]; }\n"
    "  uint N=acount;\n"
    "  vec4 pn=probe_normal(gid); vec3 nrm=pn.xyz; float nv=pn.w;\n"
    /* Directional samples for the SG specular fit. pass_classify only ever saw the
     * NEAR-field direct injection, so without re-fitting here the probe's specular
     * lobes miss ALL the gathered indirect -- i.e. probes read diffuse-only once
     * sampled. Collect (dir, radiance) from the gather + hero rays and re-fit. */
    "  vec3 rdir[32]; vec3 rcol[32]; int nrs=0;\n"
    "  for(int i=0;i<32;++i){ rdir[i]=vec3(0.0,1.0,0.0); rcol[i]=vec3(0.0); }\n"
    /* Hero selection (hybrid): scan sources, keep the top-KH by emissive_luminance *\n"
     * unocclusion * N.L. DETERMINISTIC top-K => temporally stable; these get an\n"
     * accurate SDF march below and are skipped in the average (no double-count). */
    "  uint hero[4]; int nh=0; int KH=clamp(u_hero,0,4);\n"
    "  if(u_hybrid!=0 && KH>0 && N>0u){ float hs[4];\n"
    "    for(int c=0;c<4;++c){ hero[c]=0xffffffffu; hs[c]=0.0; }\n"
    /* Scan the WHOLE source list (no RNG) and keep the top-KH by score. The alist is\n"
     * atomic-appended (nondeterministic ORDER), so selecting by list position would\n"
     * pick different sources each frame -> flicker. Selecting by (temporally stable)\n"
     * score is order-independent => the SAME heroes every frame. Capped for cost. */
    "    int SCAN=int(min(N,1024u));\n"
    "    for(int c=0;c<SCAN;++c){ uint m=alist[uint(c)]; if(m==gid) continue;\n"
    "      vec3 pm=ppos[m].xyz; vec3 d=pm-o; float dist=length(d); if(dist<0.35) continue; vec3 dir=d/dist;\n"
    "      float vis=probe_vis(gid,dir,dist); if(vis<0.05) continue;\n"
    "      float ndl=(nv>0.5)? max(dot(dir,nrm),0.0) : 1.0; if(ndl<=0.0) continue;\n"
    "      vec3 Em=max(psh_irr(int(m)*24,-dir),vec3(0.0));\n"
    "      float score=dot(Em,vec3(0.2126,0.7152,0.0722))*vis*ndl;\n"
    "      for(int j=0;j<KH;++j){ if(score>hs[j]){ for(int t=KH-1;t>j;--t){hs[t]=hs[t-1];hero[t]=hero[t-1];} hs[j]=score; hero[j]=m; break; } } }\n"
    "    for(int j=0;j<KH;++j) if(hs[j]>0.0) nh=j+1; }\n"
    /* Cheap probe-field gather: weighted AVERAGE (/wsum, gain u_bounce<1 => converges)\n"
     * of source irradiances, receiver-cosine-weighted by the probe normal (N.L),\n"
     * EXCLUDING the hero sources. When sources are few (N<=u_nsamp) scan them ALL\n"
     * deterministically -> no per-frame RNG jitter (the main temporal-coherence win). */
    "  if(N>0u){ uint seed=gid*9781u ^ uint(u_seed)*6271u ^ 0x9e3779b9u; int K=max(u_nsamp,1);\n"
    "    float cellsq=max(u_grid_cell.x*u_grid_cell.x, 0.25);\n"
    "    bool full=(N<=uint(K)); int iters= full? int(N) : K;\n"
    "    float bd[12]; float bs[12]; for(int k=0;k<12;++k){ bd[k]=0.0; bs[k]=0.0; } float wsum=0.0;\n"
    "    for(int c=0;c<iters;++c){ uint m= full? alist[uint(c)] : alist[uint(rnd(seed)*float(N))%N]; if(m==gid) continue;\n"
    "      bool ish=false; for(int j=0;j<nh;++j) if(hero[j]==m) ish=true; if(ish) continue;\n"
    "      vec3 pm=ppos[m].xyz; vec3 d=pm-o; float dist=length(d); if(dist<0.35) continue; vec3 dir=d/dist;\n"
    "      float vis=probe_vis(gid,dir,dist); if(vis<0.02) continue;\n"
    "      float ndl=(nv>0.5)? max(dot(dir,nrm),0.0)+0.04 : 1.0;\n"
    "      vec3 Lmd=max(psh_irr(int(m)*24, -dir), vec3(0.0));\n"
    "      vec3 Lms=max(psh_irr(int(m)*24+12, -dir), vec3(0.0));\n"
    "      float wt=vis*ndl*(cellsq/max(dist*dist,cellsq)); wsum+=wt;\n"
    "      if(nrs<32){ rdir[nrs]=dir; rcol[nrs]=(Lmd+Lms)*wt; ++nrs; }\n"
    "      float y[4]; sh_basis(dir,y);\n"
    "      for(int j=0;j<4;++j){ bd[j]+=Lmd.r*y[j]*wt; bd[4+j]+=Lmd.g*y[j]*wt; bd[8+j]+=Lmd.b*y[j]*wt;\n"
    "                            bs[j]+=Lms.r*y[j]*wt; bs[4+j]+=Lms.g*y[j]*wt; bs[8+j]+=Lms.b*y[j]*wt; } }\n"
    "    if(wsum>1e-4){ float nrm2=u_bounce/wsum;\n"
    "      for(int k=0;k<12;++k){ shd[k]+=bd[k]*nrm2; shs[k]+=bs[k]*nrm2; }\n"
    /* Same normalisation the diffuse gets, so the specular lobe carries the same
     * energy the SH does rather than an arbitrary scale. */
    "      for(int i=0;i<nrs;++i) rcol[i]*=nrm2; } }\n"
    /* Hero SDF marches: accurate first-bounce radiance toward the top-KH sources,\n"
     * spread over nh with the bounce gain. trace() sphere-marches the resident SDF. */
    "  for(int j=0;j<nh;++j){ uint m=hero[j]; vec3 pm=ppos[m].xyz; vec3 d=pm-o; float dist=length(d);\n"
    "    if(dist<0.35) continue; vec3 dir=d/dist; vec3 rd,rs; trace(gid,o,dir,rd,rs);\n"
    "    float y[4]; sh_basis(dir,y); float w=u_bounce/float(nh);\n"
    /* Hero rays are the brightest directions -- exactly what a specular lobe should
     * lock onto -- so seed the fit with them. */
    "    if(nrs<32){ rdir[nrs]=dir; rcol[nrs]=(rd+rs)*w; ++nrs; }\n"
    "    for(int k=0;k<4;++k){ shd[k]+=rd.r*y[k]*w; shd[4+k]+=rd.g*y[k]*w; shd[8+k]+=rd.b*y[k]*w;\n"
    "                          shs[k]+=rs.r*y[k]*w; shs[4+k]+=rs.g*y[k]*w; shs[8+k]+=rs.b*y[k]*w; } }\n"
    /* Re-fit the SG specular from the GATHERED radiance: the probes now carry a
     * directional (IBL) response, not just the diffuse SH. */
    "  if(nrs>0) fit_sg(gid, rdir, rcol);\n"
    "  for(int k=0;k<12;++k){ psh[gid*24+k]=mix(psh[gid*24+k], shd[k], u_temporal);\n"
    "                         psh[gid*24+12+k]=mix(psh[gid*24+12+k], shs[k], u_temporal); } }\n"
    /* --- OLD full-SDF-march baseline (u_field_on==0): 32 rays cone-march the SDF. --- */
    "void update_baseline(uint gid){ if(!in_group(gid)) return;\n"
    "  vec3 o=ppos[gid].xyz; float shd[12]; float shs[12];\n"
    "  for(int k=0;k<12;++k){ shd[k]=0.0; shs[k]=0.0; }\n"
    "  const int NR=32; float ga=2.399963229728653; float w=4.0*PI/float(NR);\n"
    "  vec3 rdir[32]; vec3 rcol[32];\n"
    "  for(int s=0;s<NR;++s){ float z=1.0-(2.0*float(s)+1.0)/float(NR);\n"
    "    float r=sqrt(max(0.0,1.0-z*z)); float phi=ga*float(s);\n"
    "    vec3 dir=vec3(r*cos(phi), r*sin(phi), z); rdir[s]=dir;\n"
    /* FIREFLY CLAMP: 32 uniform rays credit a hit with 4pi/32 sr, but a small\n"
     * bright surface (the fire-lit red banner from across the hall) subtends\n"
     * ~0.01 sr -- a ~40x over-estimate that paints saturated blotches on\n"
     * whichever probes' fixed rays happen to intersect it. Capping per-ray\n"
     * radiance bounds any single hit's contribution; soft glow survives. */
    "    vec3 rd, rs; trace(gid, o,dir, rd, rs);\n"
    "    rd=min(rd, vec3(u_ray_clamp)); rs=min(rs, vec3(u_ray_clamp)); rcol[s]=rd+rs;\n"
    "    float y[4]; sh_basis(dir,y);\n"
    "    for(int k=0;k<4;++k){ shd[k]+=rd.r*y[k]*w; shd[4+k]+=rd.g*y[k]*w; shd[8+k]+=rd.b*y[k]*w;\n"
    "                          shs[k]+=rs.r*y[k]*w; shs[4+k]+=rs.g*y[k]*w; shs[8+k]+=rs.b*y[k]*w; } }\n"
    "  for(int k=0;k<12;++k){ psh[gid*24+k]=mix(psh[gid*24+k], shd[k], u_temporal);\n"
    "                         psh[gid*24+12+k]=mix(psh[gid*24+12+k], shs[k], u_temporal); }\n"
    "  fit_sg(gid, rdir, rcol);\n"
    "  compute_depth(gid,o); }\n"
    /* --- MIS-sampled SVO march (u_mis==1): importance-sample the 32-ray march over
     * the 64 octahedral texels by w = hit-likelihood(depth) * N.L(dir . probe normal)
     * * radiance-guide(last-frame directional radiance). Draw the rays from that pdf
     * and divide each hit by the pdf (unbiased) -- rays land where light arrives. --- */
    "void update_mis(uint gid){ if(!in_group(gid)) return; vec3 o=ppos[gid].xyz;\n"
    "  store_probe_normal(gid,o); compute_depth(gid,o);\n"       /* fresh normal + oct depth guide */
    "  vec4 pn=probe_normal(gid); vec3 nrm=pn.xyz; float nvalid=pn.w;\n"
    "  const int OR=8, NT=64; float wt[64]; float wsum=0.0;\n"
    "  for(int i=0;i<NT;++i){ int tx=i%OR, ty=i/OR;\n"
    "    vec3 d=oct_decode((vec2(float(tx),float(ty))+0.5)/float(OR));\n"
    "    float mean=pdepth[(int(gid)*NT+i)*2];\n"
    "    float wd=(mean<20.0)? clamp(2.5/(mean+0.4),0.15,6.0) : 0.04;\n"      /* hit-likelihood */
    "    float wl=(nvalid>0.5)? max(dot(d,nrm),0.0)+0.05 : 1.0;\n"            /* N.L about probe normal */
    "    vec3 Lr=max(psh_irr(int(gid)*24,d)+psh_irr(int(gid)*24+12,d),vec3(0.0));\n"
    "    float wr=0.15+dot(Lr,vec3(0.2126,0.7152,0.0722));\n"                 /* radiance guide (temporal) */
    "    wt[i]=wd*wl*wr; wsum+=wt[i]; }\n"
    "  if(wsum<1e-6){ update_baseline(gid); return; }\n"
    "  const int NR=32; float shd[12]; float shs[12]; for(int k=0;k<12;++k){ shd[k]=0.0; shs[k]=0.0; }\n"
    "  vec3 rdir[32]; vec3 rcol[32];\n"
    "  uint seed=gid*7919u ^ uint(u_seed)*2654435761u ^ 0x9e3779b9u;\n"
    "  for(int s=0;s<NR;++s){ float xi=rnd(seed)*wsum; int i=NT-1; float acc=0.0;\n"
    "    for(int j=0;j<NT;++j){ acc+=wt[j]; if(acc>=xi){ i=j; break; } }\n"
    "    vec2 f=(vec2(float(i%OR),float(i/OR))+vec2(rnd(seed),rnd(seed)))/float(OR);\n"
    "    vec3 dir=oct_decode(f);\n"
    "    float pdf=(wt[i]/wsum)*float(NT)/(4.0*PI);\n"                        /* per-texel prob -> solid-angle pdf */
    "    float invp=1.0/max(pdf*float(NR),1e-6);\n"
    "    vec3 rd,rs; trace(gid,o,dir,rd,rs); rdir[s]=dir; rcol[s]=rd+rs;\n"
    "    float y[4]; sh_basis(dir,y);\n"
    "    for(int k=0;k<4;++k){ shd[k]+=rd.r*y[k]*invp; shd[4+k]+=rd.g*y[k]*invp; shd[8+k]+=rd.b*y[k]*invp;\n"
    "                          shs[k]+=rs.r*y[k]*invp; shs[4+k]+=rs.g*y[k]*invp; shs[8+k]+=rs.b*y[k]*invp; } }\n"
    "  for(int k=0;k<12;++k){ psh[gid*24+k]=mix(psh[gid*24+k], shd[k], u_temporal);\n"
    "                         psh[gid*24+12+k]=mix(psh[gid*24+12+k], shs[k], u_temporal); }\n"
    "  fit_sg(gid, rdir, rcol); }\n"
    /* Streaming: the grid stays DENSE (positional addressing) and non-resident
     * probes are marked INACTIVE in ppos.w -- skip their update, keeping their last
     * coefficients, instead of removing them from the array. */
    "void main(){ uint gid=gl_GlobalInvocationID.x; if(gid>=uint(u_nprobes)) return;\n"
    "  if(ppos[gid].w < 0.5) return;\n"
    "  if(u_field_on==0){ if(u_mis!=0) update_mis(gid); else update_baseline(gid); return; }\n"
    "  if(u_pass==0) pass_classify(gid); else pass_gather(gid); }\n";

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
    void *bi = loader->get_proc_address("glBindImageTexture", loader->user_data);
    memcpy(&g->BindImageTexture, &bi, sizeof bi);
    if (dc == NULL || mb == NULL) { fprintf(stderr, "gi_probe_gpu: no compute (need GL 4.3)\n"); return false; }
    memcpy(&g->DispatchCompute, &dc, sizeof dc);
    memcpy(&g->MemoryBarrier, &mb, sizeof mb);

    g->prog = cs_build();
    if (g->prog == 0) return false;
    /* Cache all uniform locations once (the staggered path dispatches every frame). */
    {
        GLuint p = g->prog; char nm[32];
        g->loc.nprobes = glGetUniformLocation(p, "u_nprobes");
        g->loc.nlights = glGetUniformLocation(p, "u_nlights");
        g->loc.nboxes  = glGetUniformLocation(p, "u_nboxes");
        g->loc.soft    = glGetUniformLocation(p, "u_soft");
        g->loc.ncones  = glGetUniformLocation(p, "u_ncones");
        g->loc.albedo  = glGetUniformLocation(p, "u_albedo");
        g->loc.temporal= glGetUniformLocation(p, "u_temporal");
        g->loc.ngroups = glGetUniformLocation(p, "u_ngroups");
        g->loc.group   = glGetUniformLocation(p, "u_group");
        g->loc.grid_dim  = glGetUniformLocation(p, "u_grid_dim");
        g->loc.grid_origin = glGetUniformLocation(p, "u_grid_origin");
        g->loc.grid_cell = glGetUniformLocation(p, "u_grid_cell");
        g->loc.field_on  = glGetUniformLocation(p, "u_field_on");
        g->loc.near_dist = glGetUniformLocation(p, "u_near_dist");
        g->loc.pass   = glGetUniformLocation(p, "u_pass");
        g->loc.seed   = glGetUniformLocation(p, "u_seed");
        g->loc.dmax   = glGetUniformLocation(p, "u_dmax");
        g->loc.emin   = glGetUniformLocation(p, "u_emin");
        g->loc.nsamp  = glGetUniformLocation(p, "u_nsamp");
        g->loc.bounce = glGetUniformLocation(p, "u_bounce");
        g->loc.ray_clamp = glGetUniformLocation(p, "u_ray_clamp");
        g->loc.mis       = glGetUniformLocation(p, "u_mis");
        g->loc.norm_gate = glGetUniformLocation(p, "u_norm_gate");
        g->loc.hybrid    = glGetUniformLocation(p, "u_hybrid");
        g->loc.hero      = glGetUniformLocation(p, "u_hero");
        g->loc.stat_scale= glGetUniformLocation(p, "u_stat_scale");
        g->loc.dyn_alb   = glGetUniformLocation(p, "u_dyn_alb");
        g->loc.dyn_origin= glGetUniformLocation(p, "u_dyn_origin");
        g->loc.dyn_dim   = glGetUniformLocation(p, "u_dyn_dim");
        g->loc.dyn_vox   = glGetUniformLocation(p, "u_dyn_vox");
        g->loc.dyn_on    = glGetUniformLocation(p, "u_dyn_on");
        g->loc.dyn_gain  = glGetUniformLocation(p, "u_dyn_gain");
        g->loc.static_on  = glGetUniformLocation(p, "u_static_on");
        g->loc.static_k   = glGetUniformLocation(p, "u_static_k");
        g->loc.static_irr = glGetUniformLocation(p, "u_static_irr");
        g->loc.zone_on     = glGetUniformLocation(p, "u_zone_on");
        g->loc.zone_sdf    = glGetUniformLocation(p, "u_zone_sdf");
        g->loc.zone_origin = glGetUniformLocation(p, "u_zone_origin");
        g->loc.zone_dim    = glGetUniformLocation(p, "u_zone_dim");
        g->loc.zone_vox    = glGetUniformLocation(p, "u_zone_vox");
        g->loc.cbrick_on     = glGetUniformLocation(p, "u_cbrick_on");
        g->loc.cbrick_index  = glGetUniformLocation(p, "u_cbrick_index");
        g->loc.cbrick_meta   = glGetUniformLocation(p, "u_cbrick_meta");
        g->loc.cbrick_pidx   = glGetUniformLocation(p, "u_cbrick_pidx");
        g->loc.cbrick_valid  = glGetUniformLocation(p, "u_cbrick_valid");
        g->loc.cbrick_origin = glGetUniformLocation(p, "u_cbrick_origin");
        g->loc.cbrick_voxel  = glGetUniformLocation(p, "u_cbrick_voxel");
        g->loc.cbrick_dim    = glGetUniformLocation(p, "u_cbrick_dim");
        g->loc.static_origin = glGetUniformLocation(p, "u_static_origin");
        g->loc.static_dim    = glGetUniformLocation(p, "u_static_dim");
        g->loc.static_vox    = glGetUniformLocation(p, "u_static_vox");
        for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i) {
            snprintf(nm, sizeof nm, "u_sdf_active[%d]", i); g->loc.sdf_active[i] = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf[%d]", i);        g->loc.sdf[i]        = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf_origin[%d]", i); g->loc.sdf_origin[i] = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf_dim[%d]", i);    g->loc.sdf_dim[i]    = glGetUniformLocation(p, nm);
            snprintf(nm, sizeof nm, "u_sdf_vox[%d]", i);    g->loc.sdf_vox[i]    = glGetUniformLocation(p, nm);
        }
    }
    g->max_lights = max_lights ? max_lights : 1u;
    g->max_boxes = max_boxes ? max_boxes : 1u;

    glGenBuffers(1, &g->b_pos);
    glGenBuffers(1, &g->b_sh);
    glGenBuffers(1, &g->b_lights);
    glGenBuffers(1, &g->b_boxes);
    glGenBuffers(1, &g->b_depth);
    glGenBuffers(1, &g->b_sg);
    /* ZERO-INIT every probe-indexed buffer: glBufferData(NULL) is UNDEFINED
     * VRAM, and any probe the update never writes (inactive under chunk
     * streaming, staggered groups before their first pass) SHADES with that
     * garbage -- saturated blotches / a phantom red patch 10 m from anything.
     * Black until first update is the correct start state. */
    float *sh_zero = calloc((size_t)max_probes * 64u * 2u, sizeof(float));
    if (sh_zero == NULL) return false;
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_pos);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 4 * sizeof(float), sh_zero, GL_DYNAMIC_DRAW);
    /* CPU shadow of the packed positions so the ACTIVE mask never needs a read-back. */
    g->pos_shadow = calloc((size_t)max_probes * 4u, sizeof(float));
    g->pos_cap = max_probes;
    if (g->pos_shadow == NULL) return false;
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_sh);
    /* 24 floats/probe: SH4 dynamic [0..11] + SH4 static [12..23] (rpg-pau4). */
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 24 * sizeof(float), sh_zero, GL_DYNAMIC_DRAW);
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
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 64 * 2 * sizeof(float), sh_zero, GL_DYNAMIC_DRAW);
    glGenTextures(1, &g->tbo_depth_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_depth_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, g->b_depth);
    /* Hardware-filterable mirror of the octahedral depth: an RG32F 2D ATLAS of
     * 10x10 tiles (8x8 interior + 1px oct-wrap gutter), 256 tiles per row --
     * GL_LINEAR keeps the forward+ Chebyshev at one bilinear tap. A 2D array
     * was capped by GL_MAX_ARRAY_TEXTURE_LAYERS (2048 here): past the cap the
     * whole array went INCOMPLETE and probe_vis read zeros for EVERY probe
     * (the "coarse voxel grid" look at 30k probes). The atlas caps at
     * 256*rows tiles -- 65k+ probes in a 2560x2560 texture. */
    glGenTextures(1, &g->depth_arr);
    glBindTexture(GL_TEXTURE_2D, g->depth_arr);
    {
        int tiles_y = (int)((max_probes + 255u) / 256u);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, 256 * 10, tiles_y * 10,
                     0, GL_RG, GL_FLOAT, NULL);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* SG specular: 3 lobes/probe * 8 floats = 24 (6 RGBA32F texels). */
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_sg);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 24 * sizeof(float), sh_zero, GL_DYNAMIC_DRAW);
    glGenTextures(1, &g->tbo_sg_tex);
    glBindTexture(GL_TEXTURE_BUFFER, g->tbo_sg_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g->b_sg);

    /* Stochastic-radiosity scratch (rpg-3c6g): active-source list (1 count uint +
     * max_probes index uints) + per-probe direct-injection SH (24 floats/probe). */
    glGenBuffers(1, &g->b_active);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_active);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(max_probes + 1u) * sizeof(uint32_t), sh_zero, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &g->b_emit);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_emit);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 24 * sizeof(float), sh_zero, GL_DYNAMIC_DRAW);
    /* Per-probe surface normal cache (vec4: xyz + validity), gated by proximity. */
    glGenBuffers(1, &g->b_norm);
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_norm);
    glBufferData(GI_GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)max_probes * 4 * sizeof(float), sh_zero, GL_DYNAMIC_DRAW);

    gi_probe_tuning_defaults(&g->tuning);
    g->ready = true;
    free(sh_zero);
    return true;
}

void gi_probe_gpu_set_probes(gi_probe_gpu_t *g, const float *pos, uint32_t n)
{
    if (g == NULL || !g->ready || pos == NULL || g->pos_shadow == NULL) return;
    if (n > g->pos_cap) n = g->pos_cap;
    g->n_probes = n;
    /* Pack xyz -> vec4 straight into the CPU SHADOW copy, so a later ACTIVE-mask
     * flip is a plain upload rather than a read-back (see gi_probe_gpu_active.c). */
    for (uint32_t i = 0; i < n; ++i) {
        g->pos_shadow[i*4+0] = pos[i*3+0];
        g->pos_shadow[i*4+1] = pos[i*3+1];
        g->pos_shadow[i*4+2] = pos[i*3+2];
        /* w = ACTIVE flag. Probe streaming keeps the grid DENSE (the forward+
         * addresses probes positionally) and instead marks non-resident probes
         * inactive, so they stay addressable but are skipped by the update. */
        g->pos_shadow[i*4+3] = 1.0f;
    }
    glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_pos);
    glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)n * 4 * sizeof(float),
                    g->pos_shadow);
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
                           float soft_k, float temporal, int ngroups, int group,
                           const int grid_dim[3], const float grid_origin[3],
                           const float grid_cell[3])
{
    if (g == NULL || !g->ready || g->n_probes == 0) return;
    if (n_lights > g->max_lights) n_lights = g->max_lights;
    if (n_boxes > g->max_boxes) n_boxes = g->max_boxes;
    static unsigned s_tick = 0; ++s_tick;  /* RNG salt so the stochastic samples vary per dispatch. */

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

    /* Pack colliders: 2 vec4 each. vec4_0 = (a.xyz, kind); vec4_1 depends on kind:
     * box -> half-extents.xyz; sphere -> (_, _, _, radius); capsule -> (b.xyz, radius).
     * Kind codes match GI_COLLIDER_* (0=sphere,1=box,2=capsule). (rpg-85as) */
    float *bb = malloc((size_t)(n_boxes ? n_boxes : 1) * 8 * sizeof(float));
    uint32_t nb = 0;
    for (uint32_t i = 0; i < n_boxes; ++i) {
        const gi_collider_t *c = &boxes[i]; float *o = &bb[nb*8];
        o[0]=c->a[0]; o[1]=c->a[1]; o[2]=c->a[2]; o[3]=(float)c->kind;
        if (c->kind == GI_COLLIDER_CAPSULE) {
            o[4]=c->b[0]; o[5]=c->b[1]; o[6]=c->b[2]; o[7]=c->ext[0]; /* B + radius. */
        } else if (c->kind == GI_COLLIDER_SPHERE) {
            o[4]=0.0f; o[5]=0.0f; o[6]=0.0f; o[7]=c->ext[0];         /* radius. */
        } else {
            o[4]=c->ext[0]; o[5]=c->ext[1]; o[6]=c->ext[2]; o[7]=0.0f; /* box half. */
        }
        ++nb;
    }
    if (nb) { glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_boxes);
        glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)nb*8*sizeof(float), bb); }
    free(bb);

    glUseProgram(g->prog);
    glUniform1i(g->loc.nprobes, (GLint)g->n_probes);
    glUniform1i(g->loc.nlights, (GLint)n_lights);
    glUniform1i(g->loc.nboxes, (GLint)nb);
    glUniform1f(g->loc.soft, soft_k);
    glUniform1i(g->loc.ncones, 8);        /* sphere samples. */
    glUniform1f(g->loc.albedo, 1.0f);     /* gain; real albedo now from the voxel map. */
    glUniform1f(g->loc.temporal, temporal);
    glUniform1i(g->loc.ngroups, (GLint)ngroups);
    glUniform1i(g->loc.group, (GLint)group);
    glUniform3i(g->loc.grid_dim, grid_dim ? grid_dim[0] : 0,
                grid_dim ? grid_dim[1] : 0, grid_dim ? grid_dim[2] : 0);
    int field_on = 0;
    {
        int have_grid = (grid_dim && grid_dim[0] > 0 && grid_origin && grid_cell);
        float o3[3] = {0,0,0}, c3[3] = {1,1,1};
        if (have_grid) { for (int i=0;i<3;++i){ o3[i]=grid_origin[i]; c3[i]=grid_cell[i]; } }
        /* Brick sets have no dense lattice, but the brick structure IS the field
         * lookup (u_cbrick_*): let it drive the recurrent gather, and use the
         * brick probe spacing as the transport cell scale (cellsq). */
        else if (g->cbrick.on) {
            float sp = g->cbrick.voxel / 3.0f;
            for (int i = 0; i < 3; ++i) { o3[i] = g->cbrick.origin[i]; c3[i] = sp; }
            have_grid = 1;
        }
        glUniform3fv(g->loc.grid_origin, 1, o3);
        glUniform3fv(g->loc.grid_cell, 1, c3);
        /* DDGI recurrent-irradiance gather (rpg-3c6g): each ray's hit gathers the
         * probe field's bounced irradiance (multi-bounce over frames). Needs a grid;
         * GI_FIELD=0 disables to A/B against the pure per-ray trace. */
        field_on = have_grid && g->tuning.field_on;
        { const char *e = getenv("GI_FIELD"); if (e) field_on = have_grid && (atoi(e) != 0); }
        glUniform1i(g->loc.field_on, field_on);
        /* "Very close" threshold: hits nearer than this get a direct voxel+light
         * sample (accurate 1st bounce); farther hits are dropped from the direct
         * injection and rebuilt by the stochastic gather. ~1 probe cell by default. */
        float near_dist = g->tuning.near_dist;
        { const char *e = getenv("GI_NEAR"); if (e) { float v = (float)atof(e); if (v > 0.0f) near_dist = v; } }
        glUniform1f(g->loc.near_dist, near_dist);
        /* Stochastic probe-radiosity knobs (rpg-3c6g). GI_DMAX: nearest-surface
         * distance under which a probe counts as a light SOURCE. GI_SAMPLES: random
         * source probes each gather. GI_BOUNCE: bounce gain (energy tune). */
        float dmax = g->tuning.dmax; { const char *e = getenv("GI_DMAX"); if (e) { float v=(float)atof(e); if (v>0.0f) dmax=v; } }
        float emin = g->tuning.emin; { const char *e = getenv("GI_EMIN"); if (e) { float v=(float)atof(e); if (v>=0.0f) emin=v; } }
        int nsamp = g->tuning.samples; { const char *e = getenv("GI_SAMPLES"); if (e) { int v=atoi(e); if (v>=1) nsamp=v; } }
        /* Bounce decay/albedo: the gather is a weight-normalised average, so this is
         * the per-bounce transport gain -- keep < ~1 (well under the ~4 divergence
         * ceiling the double-cosine SH convolution allows) so it converges. */
        float bounce = g->tuning.bounce; { const char *e = getenv("GI_BOUNCE"); if (e) { float v=(float)atof(e); if (v>=0.0f) bounce=v; } }
        glUniform1f(g->loc.dmax, dmax);
        glUniform1f(g->loc.emin, emin);
        glUniform1i(g->loc.nsamp, nsamp);
        glUniform1f(g->loc.bounce, bounce);
        {
            float rclamp = g->tuning.ray_clamp > 0.0f ? g->tuning.ray_clamp : 4.0f;
            const char *e = getenv("GI_RAY_CLAMP");
            if (e != NULL) { float v = (float)atof(e); if (v > 0.0f) rclamp = v; }
            glUniform1f(g->loc.ray_clamp, rclamp);
        }
        glUniform1i(g->loc.seed, (GLint)(s_tick & 0x7fffffffu));
    }
    /* MIS-sampled march directions (rpg-3c6g): pdf = depth-hit x N.L(probe normal) x
     * radiance guide. GI_MIS=1 enables (baseline path only). GI_NORM_GATE sets the
     * surface-proximity distance under which a probe gets a normal (else omni). */
    {
        int mis = g->tuning.mis;
        { const char *e = getenv("GI_MIS"); if (e) mis = (atoi(e) != 0); }
        float ngate = g->tuning.norm_gate; { const char *e = getenv("GI_NORM_GATE"); if (e) { float v=(float)atof(e); if (v>0.0f) ngate=v; } }
        glUniform1i(g->loc.mis, mis);
        glUniform1f(g->loc.norm_gate, ngate);
        /* Hybrid: cheap field bounce + u_hero deterministic hero SDF marches. */
        int hybrid = g->tuning.hybrid;
        { const char *e = getenv("GI_HYBRID"); if (e) hybrid = (atoi(e) != 0); }
        int hero = g->tuning.hero; { const char *e = getenv("GI_HERO"); if (e) { int v=atoi(e); if (v>=0 && v<=4) hero=v; } }
        glUniform1i(g->loc.hybrid, hybrid);
        glUniform1i(g->loc.hero, hero);
        /* Probe static-bounce dim (GI_STAT_SCALE, 1 = full). Lower it when the baked
         * lightmap is on so probes add only dynamic bleed, not double-counted static. */
        float sscale = g->tuning.stat_scale; { const char *e = getenv("GI_STAT_SCALE"); if (e) { float v=(float)atof(e); if (v>=0.0f) sscale=v; } }
        glUniform1f(g->loc.stat_scale, sscale);
    }

    /* Static irradiance volume: bound past the SDF slot units. */
    {
        int on = (g->static_tex != 0) ? 1 : 0;
        int unit = GI_SDF_UNIT_BASE + GI_SDF_MAX_RESIDENT; /* first unit after the SDF chunks. */
        glUniform1i(g->loc.static_on, on);
        glUniform1f(g->loc.static_k, g->static_k);
        glUniform1i(g->loc.static_irr, unit);
        glUniform3fv(g->loc.static_origin, 1, g->static_origin);
        glUniform3fv(g->loc.static_dim, 1, g->static_dim);
        glUniform1f(g->loc.static_vox, g->static_vox);
        glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
        glBindTexture(GL_TEXTURE_3D, on ? g->static_tex : 0);
    }

    /* Sparse dynamic albedo volume: one unit past the static irradiance volume. */
    {
        int on = (g->dyn_on && g->dyn_tex != 0) ? 1 : 0;
        int unit = GI_SDF_UNIT_BASE + GI_SDF_MAX_RESIDENT + 1;
        float dimf[3] = { (float)g->dyn_dim[0], (float)g->dyn_dim[1], (float)g->dyn_dim[2] };
        glUniform1i(g->loc.dyn_on, on);
        {
            float dg = g->tuning.dyn_gain > 0.0f ? g->tuning.dyn_gain : 1.0f;
            const char *e = getenv("GI_DYN_GAIN");
            if (e != NULL) { float v = (float)atof(e); if (v > 0.0f) dg = v; }
            glUniform1f(g->loc.dyn_gain, dg);
        }
        glUniform1i(g->loc.dyn_alb, unit);
        glUniform3fv(g->loc.dyn_origin, 1, g->dyn_origin);
        glUniform3fv(g->loc.dyn_dim, 1, dimf);
        glUniform1f(g->loc.dyn_vox, g->dyn_vox > 0.0f ? g->dyn_vox : 1.0f);
        glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
        glBindTexture(GL_TEXTURE_3D, on ? g->dyn_tex : 0);
    }

    /* Bind the resident SDF chunks (one per used slot) + metadata. */
    for (int i = 0; i < GI_SDF_MAX_RESIDENT; ++i) {
        int active = (sdf != NULL && sdf->slot_chunk[i] >= 0) ? 1 : 0;
        glUniform1i(g->loc.sdf_active[i], active);
        glUniform1i(g->loc.sdf[i], GI_SDF_UNIT_BASE + i);
        glActiveTexture(GL_TEXTURE0 + (GLenum)(GI_SDF_UNIT_BASE + i));
        glBindTexture(GL_TEXTURE_3D, active ? sdf->tex[i] : 0);
        if (active) {
            const gi_sdf_chunk_ram_t *r = &sdf->ram[sdf->slot_chunk[i]];
            float o3[3] = { r->origin[0], r->origin[1], r->origin[2] };
            float d3[3] = { (float)r->dims[0], (float)r->dims[1], (float)r->dims[2] };
            glUniform3fv(g->loc.sdf_origin[i], 1, o3);
            glUniform3fv(g->loc.sdf_dim[i], 1, d3);
            glUniform1f(g->loc.sdf_vox[i], r->voxel);
        }
    }

    /* GLOBAL zone SDF (page-fault fallback): always bound while the stream has
     * one, two units past the dynamic albedo volume. */
    {
        int zon = (sdf != NULL && sdf->has_zone && sdf->zone_tex != 0) ? 1 : 0;
        int zunit = GI_SDF_UNIT_BASE + GI_SDF_MAX_RESIDENT + 2;
        glUniform1i(g->loc.zone_on, zon);
        glUniform1i(g->loc.zone_sdf, zunit);
        if (zon) {
            float o3[3] = { sdf->zone_origin[0], sdf->zone_origin[1], sdf->zone_origin[2] };
            float d3[3] = { (float)sdf->zone_dims[0], (float)sdf->zone_dims[1],
                            (float)sdf->zone_dims[2] };
            glUniform3fv(g->loc.zone_origin, 1, o3);
            glUniform3fv(g->loc.zone_dim, 1, d3);
            glUniform1f(g->loc.zone_vox, sdf->zone_voxel);
        }
        glActiveTexture(GL_TEXTURE0 + (GLenum)zunit);
        glBindTexture(GL_TEXTURE_3D, zon ? sdf->zone_tex : 0);
    }

    /* Brick field lookup (units 3..6 past the zone SDF). */
    {
        int on = g->cbrick.on ? 1 : 0;
        int u0 = GI_SDF_UNIT_BASE + GI_SDF_MAX_RESIDENT + 3;
        glUniform1i(g->loc.cbrick_on, on);
        glUniform1i(g->loc.cbrick_index, u0);
        glUniform1i(g->loc.cbrick_meta, u0 + 1);
        glUniform1i(g->loc.cbrick_pidx, u0 + 2);
        glUniform1i(g->loc.cbrick_valid, u0 + 3);
        if (on) {
            float d3[3] = { (float)g->cbrick.dim[0], (float)g->cbrick.dim[1],
                            (float)g->cbrick.dim[2] };
            glUniform3fv(g->loc.cbrick_origin, 1, g->cbrick.origin);
            glUniform1f(g->loc.cbrick_voxel, g->cbrick.voxel);
            glUniform3fv(g->loc.cbrick_dim, 1, d3);
        }
        glActiveTexture(GL_TEXTURE0 + (GLenum)u0);
        glBindTexture(GL_TEXTURE_3D, on ? g->cbrick.index_tex : 0);
        glActiveTexture(GL_TEXTURE0 + (GLenum)(u0 + 1));
        glBindTexture(GL_TEXTURE_BUFFER, on ? g->cbrick.meta_tex : 0);
        glActiveTexture(GL_TEXTURE0 + (GLenum)(u0 + 2));
        glBindTexture(GL_TEXTURE_BUFFER, on ? g->cbrick.pidx_tex : 0);
        glActiveTexture(GL_TEXTURE0 + (GLenum)(u0 + 3));
        glBindTexture(GL_TEXTURE_BUFFER, on ? g->cbrick.valid_tex : 0);
    }

    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 0, g->b_pos);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 1, g->b_sh);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 2, g->b_lights);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 3, g->b_boxes);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 4, g->b_depth);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 5, g->b_sg);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 6, g->b_active);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 7, g->b_emit);
    glBindBufferBase(GI_GL_SHADER_STORAGE_BUFFER, 8, g->b_norm);
    /* Bind the depth 2D-array as an r/w image (unit 0) so the compute mirrors the
     * octahedral depth into it for hardware-filtered sampling in the forward+. */
    if (g->BindImageTexture != NULL)
        g->BindImageTexture(0, g->depth_arr, 0, /*layered=*/0, 0,
                            GI_GL_READ_WRITE, GL_RG32F);

    GLuint ngx = (g->n_probes + 63u) / 64u;
    if (field_on) {
        /* Two-pass stochastic radiosity. Reset the active-source counter, run pass 0
         * (classify + inject) for ALL probes, barrier so its depth/emit/list are
         * visible, then pass 1 (stochastic gather) reads them. */
        GLuint zero = 0;
        glBindBuffer(GI_GL_SHADER_STORAGE_BUFFER, g->b_active);
        glBufferSubData(GI_GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &zero);
        g->MemoryBarrier(GI_GL_SHADER_STORAGE_BARRIER_BIT);
        glUniform1i(g->loc.pass, 0);
        g->DispatchCompute(ngx, 1u, 1u);
        g->MemoryBarrier(GI_GL_SHADER_STORAGE_BARRIER_BIT | GI_GL_TEXTURE_FETCH_BARRIER_BIT);
        glUniform1i(g->loc.pass, 1);
        g->DispatchCompute(ngx, 1u, 1u);
        g->MemoryBarrier(GI_GL_SHADER_STORAGE_BARRIER_BIT | GI_GL_TEXTURE_FETCH_BARRIER_BIT);
    } else {
        glUniform1i(g->loc.pass, 0);
        g->DispatchCompute(ngx, 1u, 1u);
        g->MemoryBarrier(GI_GL_SHADER_STORAGE_BARRIER_BIT | GI_GL_TEXTURE_FETCH_BARRIER_BIT);
    }
}

unsigned int gi_probe_gpu_sh_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_sh_tex : 0u; }
unsigned int gi_probe_gpu_pos_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_pos_tex : 0u; }
unsigned int gi_probe_gpu_depth_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_depth_tex : 0u; }
unsigned int gi_probe_gpu_sg_tbo(const gi_probe_gpu_t *g) { return g ? g->tbo_sg_tex : 0u; }

void gi_probe_gpu_destroy(gi_probe_gpu_t *g)
{
    if (g == NULL) return;
    free(g->pos_shadow); g->pos_shadow = NULL; g->pos_cap = 0;
    if (g->prog) glDeleteProgram(g->prog);
    GLuint b[8] = { g->b_pos, g->b_sh, g->b_lights, g->b_boxes, g->b_depth, g->b_sg,
                    g->b_active, g->b_emit };
    glDeleteBuffers(8, b);
    if (g->tbo_sh_tex) glDeleteTextures(1, &g->tbo_sh_tex);
    if (g->tbo_pos_tex) glDeleteTextures(1, &g->tbo_pos_tex);
    if (g->tbo_depth_tex) glDeleteTextures(1, &g->tbo_depth_tex);
    if (g->depth_arr) glDeleteTextures(1, &g->depth_arr);
    if (g->tbo_sg_tex) glDeleteTextures(1, &g->tbo_sg_tex);
    memset(g, 0, sizeof *g);
}
