#include "ferrum/renderer/pbr_shader.h"

/* ── Vertex shader ──────────────────────────────────────────────────────
 * Static-mesh attribute layout: 0=pos 1=normal 2=tangent.xyzw 3=uv0 4=uv1.
 * Emits world position, a world-space TBN basis, and both UV sets. */
static const char *const PBR_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "layout(location=1) in vec3 in_normal;\n"
    "layout(location=2) in vec4 in_tangent;\n"
    "layout(location=3) in vec2 in_uv0;\n"
    "layout(location=4) in vec2 in_uv1;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec3 v_world_pos;\n"
    "out vec3 v_normal;\n"
    "out vec3 v_tangent;\n"
    "out vec3 v_bitangent;\n"
    "out vec2 v_uv0;\n"
    "out vec2 v_uv1;\n"
    "out float v_view_z;\n"
    "void main() {\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_world_pos = wp.xyz;\n"
    "  v_view_z = (u_view * wp).z;\n"
    "  mat3 nm = mat3(u_model);\n" /* uniform-scale assumption */
    "  v_normal = normalize(nm * in_normal);\n"
    "  v_tangent = normalize(nm * in_tangent.xyz);\n"
    "  v_bitangent = normalize(cross(v_normal, v_tangent) * in_tangent.w);\n"
    "  v_uv0 = in_uv0;\n"
    "  v_uv1 = in_uv1;\n"
    "  gl_Position = u_projection * u_view * wp;\n"
    "}\n";

/* ── Fragment shader ────────────────────────────────────────────────────
 * Metallic-roughness Cook-Torrance: GGX distribution, Smith height-correlated
 * visibility (Schlick-GGX), Schlick Fresnel; tangent-space normal mapping, AO,
 * roughness min/max remap, tint, specular strength, emissive self-shading.
 * Lit by one directional sun + a constant ambient (extended later). */
static const char *const PBR_FS =
    "#version 330 core\n"
    "#extension GL_ARB_texture_cube_map_array : require\n" /* samplerCubeArray (shadows). */
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "in vec3 v_tangent;\n"
    "in vec3 v_bitangent;\n"
    "in vec2 v_uv0;\n"
    "in vec2 v_uv1;\n"
    "in float v_view_z;\n"
    "out vec4 frag;\n"
    "uniform sampler2D u_albedo_map;\n"
    "uniform sampler2D u_normal_map;\n"
    "uniform sampler2D u_metallic_map;\n"
    "uniform sampler2D u_roughness_map;\n"
    "uniform sampler2D u_ao_map;\n"
    "uniform sampler2D u_emissive_map;\n"
    "uniform int u_has_albedo;\n"
    "uniform int u_has_normal;\n"
    "uniform int u_has_metallic;\n"
    "uniform int u_has_roughness;\n"
    "uniform int u_has_ao;\n"
    "uniform int u_orm_packed;\n"  /* 1 = roughness map is ORM (R=ao, G=roughness). */
    "uniform int u_has_emissive;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_contrast;\n"
    "uniform vec3 u_emissive_color;\n"
    "uniform float u_specular_strength;\n"
    "uniform float u_metalness;\n"
    "uniform float u_roughness_min;\n"
    "uniform float u_roughness_max;\n"
    "uniform float u_emissive_strength;\n"
    "uniform float u_normal_scale;\n"
    "uniform vec2 u_uv_scale;\n"
    "uniform float u_ao_strength;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform vec3 u_sun_dir;\n"
    "uniform vec3 u_sun_color;\n"
    "uniform vec3 u_ambient;\n"
    /* Current-pass punctual light set (point/spot/directional). In the clustered\n"
     * pipeline this is the per-cluster subset; here it is uploaded directly. */
    "#define PBR_MAX_LIGHTS 32\n"
    "uniform int u_light_count;\n"
    "uniform int u_light_type[PBR_MAX_LIGHTS];\n"    /* 0=point 1=directional 2=spot */
    "uniform vec3 u_light_pos[PBR_MAX_LIGHTS];\n"    /* world position (point/spot) */
    "uniform vec3 u_light_dir[PBR_MAX_LIGHTS];\n"    /* forward axis (dir/spot) */
    "uniform vec3 u_light_color[PBR_MAX_LIGHTS];\n"  /* colour * intensity */
    "uniform float u_light_range[PBR_MAX_LIGHTS];\n"
    "uniform float u_light_cos_inner[PBR_MAX_LIGHTS];\n"
    "uniform float u_light_cos_outer[PBR_MAX_LIGHTS];\n"
    /* Clustered forward+: per-fragment cluster lookup into GPU light buffers.\n"
     * u_cluster_range: RG32I (offset,count) per cluster; u_light_index: R32I\n"
     * flat index list; u_light_data: RGBA32F, 4 texels/light. */
    "uniform int u_clustered;\n"
    "uniform isamplerBuffer u_cluster_offset;\n"
    "uniform isamplerBuffer u_cluster_count;\n"
    "uniform isamplerBuffer u_light_index;\n"
    "uniform samplerBuffer u_light_data;\n"
    "uniform vec3 u_cluster_dims;\n"     /* tiles_x, tiles_y, slices (as floats) */
    "uniform vec3 u_screen;\n"           /* viewport pixels in .xy */
    "uniform float u_cluster_near;\n"
    "uniform float u_cluster_far;\n"
    "uniform float u_inv_log_farnear;\n" /* 1/log(far/near), precomputed CPU-side. */
    /* Baked SH9 lightmap: 9 coefficient maps (RGB = the coeff for R,G,B),\n"
     * sampled by uv1; u_sh_enabled toggles it as the diffuse indirect term. */
    /* Per-chunk lightmaps (rpg-yfa4): 9 SH coeff atlases as texture ARRAYS, one\n"
     * layer per chunk; each mesh samples its own page via u_sh_layer. A single-\n"
     * atlas bake just uploads a 1-layer array with u_sh_layer=0. */
    "uniform sampler2DArray u_sh0;\n"
    "uniform sampler2DArray u_sh1;\n"
    "uniform sampler2DArray u_sh2;\n"
    "uniform sampler2DArray u_sh3;\n"
    "uniform sampler2DArray u_sh4;\n"
    "uniform sampler2DArray u_sh5;\n"
    "uniform sampler2DArray u_sh6;\n"
    "uniform sampler2DArray u_sh7;\n"
    "uniform sampler2DArray u_sh8;\n"
    "uniform int u_sh_enabled;\n"
    /* Per-mesh RESIDENT lightmap array layer (rpg-ojuq streaming): the CPU sets it\n"
     * each frame to the mesh's chunk's paged-in layer, or -1 when that chunk isn't\n"
     * resident (-> no baked term this frame). */
    "uniform int u_sh_layer;\n"
    "uniform float u_sh_scale;\n" /* baked-lightmap intensity multiplier (default 1). */
    "uniform float u_sh_normal_bias;\n" /* 0=geometric N, 1=full normal-mapped N for
                                         * the baked-lightmap diffuse (avoids the detail
                                         * normal OVER-shading the baked directional sun). */
    "#define SHUV vec3(v_uv1, float(max(u_sh_layer,0)))\n"
    "uniform float u_sh_object;\n" /* 1 for static (lightmapped) objects, 0 for dynamic. */
    /* Debug visualisation: 0=off, 1=raw SH DC term (coeff0) at v_uv1,
     * 2=reconstructed SH irradiance E(N), 3=lightmap uv1 as colour. */
    "uniform int u_debug_mode;\n"
    "const float PI = 3.14159265359;\n"
    /* Irradiance from the baked SH lightmap in direction n (Ramamoorthi cosine\n"
     * lobe: A0=pi, A1=2pi/3, A2=pi/4). Matches lm_sh9_irradiance exactly. */
    /* SH4 (bands 0-1) only: 4 lightmap fetches instead of 9. The band-2 detail is\n"
     * dropped at sample time -- the baked maps still hold it, we just don't read\n"
     * u_sh4..u_sh8. Ample for smooth diffuse indirect. */
    /* Full SH9 real basis evaluated along d (bands 0,1,2). */
    "void sh9_basis(vec3 d, out float y[9]){\n"
    "  y[0]=0.282094792;\n"
    "  y[1]=0.488602512*d.y; y[2]=0.488602512*d.z; y[3]=0.488602512*d.x;\n"
    "  y[4]=1.092548431*d.x*d.y; y[5]=1.092548431*d.y*d.z; y[6]=0.315391565*(3.0*d.z*d.z-1.0);\n"
    "  y[7]=1.092548431*d.x*d.z; y[8]=0.546274215*(d.x*d.x-d.y*d.y);\n"
    "}\n"
    /* Fetch this luxel's 9 SH coefficients (the lightmap bakes the full SH9). */
    "void sh9_fetch(out vec3 L[9]){\n"
    "  L[0]=texture(u_sh0,SHUV).rgb; L[1]=texture(u_sh1,SHUV).rgb; L[2]=texture(u_sh2,SHUV).rgb;\n"
    "  L[3]=texture(u_sh3,SHUV).rgb; L[4]=texture(u_sh4,SHUV).rgb; L[5]=texture(u_sh5,SHUV).rgb;\n"
    "  L[6]=texture(u_sh6,SHUV).rgb; L[7]=texture(u_sh7,SHUV).rgb; L[8]=texture(u_sh8,SHUV).rgb;\n"
    "}\n"
    /* Cosine-convolved irradiance along n (A0=pi, A1=2pi/3, A2=pi/4) -- diffuse. */
    "vec3 sh9_irradiance(vec3 L[9], vec3 n){ float y[9]; sh9_basis(n,y);\n"
    "  vec3 E = 3.14159265*L[0]*y[0];\n"
    "  E += 2.09439510*(L[1]*y[1]+L[2]*y[2]+L[3]*y[3]);\n"
    "  E += 0.785398163*(L[4]*y[4]+L[5]*y[5]+L[6]*y[6]+L[7]*y[7]+L[8]*y[8]);\n"
    "  return E; }\n"
    /* Raw radiance reconstruction along d (no cosine convolution) -- the incident\n"
     * environment radiance for the split-sum specular IBL (band-limited/blurry,\n"
     * which is exactly a very-rough prefiltered reflection). */
    "vec3 sh9_radiance(vec3 L[9], vec3 d){ float y[9]; sh9_basis(d,y);\n"
    "  vec3 R=vec3(0.0); for(int i=0;i<9;++i) R+=L[i]*y[i]; return R; }\n"
    /* Back-compat wrapper (debug view): full-SH9 irradiance along n. */
    "vec3 pbr_sh_irradiance(vec3 n){ vec3 L[9]; sh9_fetch(L); return sh9_irradiance(L,n); }\n"
    "float D_GGX(float NoH, float a){ float a2=a*a; float d=(NoH*NoH*(a2-1.0)+1.0); return a2/max(PI*d*d,1e-7); }\n"
    "float G_SchlickGGX(float NoX, float k){ return NoX/(NoX*(1.0-k)+k); }\n"
    "float G_Smith(float NoV, float NoL, float r){ float k=(r+1.0); k=k*k/8.0; return G_SchlickGGX(NoV,k)*G_SchlickGGX(NoL,k); }\n"
    "vec3 F_Schlick(float VoH, vec3 F0){ return F0 + (1.0-F0)*pow(clamp(1.0-VoH,0.0,1.0),5.0); }\n"
    /* Split-sum environment BRDF (Karis' analytic fit, no LUT): integrates the\n"
     * specular BRDF over the hemisphere for IBL so the probe reflection responds\n"
     * to F0, roughness and grazing angle (the NdotH-dependent term we can't do\n"
     * per-fragment for environment light). Returns the specular scale for F0. */
    "vec3 env_brdf_approx(vec3 F0, float rough, float NoV){\n"
    "  const vec4 c0=vec4(-1.0,-0.0275,-0.572,0.022); const vec4 c1=vec4(1.0,0.0425,1.04,-0.04);\n"
    "  vec4 r=rough*c0+c1; float a004=min(r.x*r.x, exp2(-9.28*NoV))*r.x + r.y;\n"
    "  vec2 ab=vec2(-1.04,1.04)*a004 + r.zw; return F0*ab.x + ab.y; }\n"
    /* Outgoing radiance for one light of unit colour arriving along L (includes\n"
     * the N.L cosine): Cook-Torrance specular + Lambert diffuse. */
    "vec3 pbr_light(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough, float metal, vec3 F0){\n"
    "  float NoL = max(dot(N,L),0.0);\n"
    "  if(NoL<=0.0) return vec3(0.0);\n"
    "  vec3 H = normalize(V+L);\n"
    "  float NoV = max(dot(N,V),1e-4), NoH = max(dot(N,H),0.0), VoH = max(dot(V,H),0.0);\n"
    "  float D = D_GGX(NoH, rough*rough);\n"
    "  float G = G_Smith(NoV, NoL, rough);\n"
    "  vec3  F = F_Schlick(VoH, F0);\n"
    "  vec3 spec = (D*G)*F / max(4.0*NoV*NoL, 1e-4);\n"
    "  vec3 kd = (vec3(1.0)-F)*(1.0-metal);\n"
    "  return (kd*albedo/PI + spec) * NoL;\n"
    "}\n"
    /* Smooth inverse-square attenuation with a range window (matches lm_light). */
    "float pbr_atten(float dist, float range){\n"
    "  float w = clamp(1.0 - pow(dist/max(range,1e-3),4.0), 0.0, 1.0); w*=w;\n"
    "  return w / max(dist*dist, 1e-4);\n"
    "}\n"
    /* Contribution of one punctual light (type/pos/dir/colour/range/cone). */
    "vec3 pbr_accumulate(int ty, vec3 lpos, vec3 ldir, vec3 lcolor, float lrange,\n"
    "                    float lci, float lco, vec3 N, vec3 V, vec3 albedo,\n"
    "                    float rough, float metal, vec3 F0){\n"
    "  vec3 L; float atten = 1.0;\n"
    "  if(ty==1){ L = normalize(-ldir); }\n"
    "  else {\n"
    "    vec3 d = lpos - v_world_pos; float dist = length(d);\n"
    "    L = d / max(dist,1e-4); atten = pbr_atten(dist, lrange);\n"
    "    if(ty==2){ float c = dot(-L, normalize(ldir)); atten *= smoothstep(lco, lci, c); }\n"
    "  }\n"
    "  return pbr_light(N,V,L,albedo,rough,metal,F0) * lcolor * atten;\n"
    "}\n"
    /* Omnidirectional cube shadow for ANY point light tagged for shadows: each\n"
     * such light rendered its 6 faces into a cube-array SLOT (carried in its\n"
     * packed data). PCF-compare the light->fragment distance in 20 directions. */
    "uniform samplerCubeArray u_shadow_cube_arr;\n"
    "uniform float u_shadow_far;\n"
    "uniform float u_shadow_bias;\n"
    "const vec3 SC_OFFS[20] = vec3[](vec3(1,1,1),vec3(1,-1,1),vec3(-1,-1,1),vec3(-1,1,1),\n"
    "  vec3(1,1,-1),vec3(1,-1,-1),vec3(-1,-1,-1),vec3(-1,1,-1),vec3(1,1,0),vec3(1,-1,0),\n"
    "  vec3(-1,-1,0),vec3(-1,1,0),vec3(1,0,1),vec3(-1,0,1),vec3(1,0,-1),vec3(-1,0,-1),\n"
    "  vec3(0,1,1),vec3(0,-1,1),vec3(0,-1,-1),vec3(0,1,-1));\n"
    "float pbr_cube_shadow(vec3 lpos, float slot, mat2 krot){\n"
    "  vec3 ftl = v_world_pos - lpos;\n"
    "  float cur = length(ftl);\n"
    "  if(cur >= u_shadow_far) return 1.0;\n"
    "  float radius = 0.015*cur + 0.01;\n"
    "  float invfar = 1.0/u_shadow_far;\n"
    "  float thresh = (cur - u_shadow_bias) * invfar;\n"  /* compare in normalised units. */
    "  float lit = 0.0;\n"
    /* 8-tap PCF (cube-corner dirs), the 8 taps' xy dithered by the shared\n"
     * per-fragment rotation so the penumbra reads smooth without banding. */
    "  for(int i=0;i<8;++i){\n"
    "    vec3 o = vec3(krot*SC_OFFS[i].xy, SC_OFFS[i].z)*radius;\n"
    "    float d = texture(u_shadow_cube_arr, vec4(ftl + o, slot)).r;\n"
    "    lit += (thresh > d) ? 0.0 : 1.0;\n"
    "  }\n"
    "  return lit * 0.125;\n"
    "}\n"
    /* Spot (2D) shadow: project the fragment through the light-space matrix and\n"
     * PCF-compare stored linear distance. u_spot_light == the light's flat index. */
    "uniform sampler2D u_spot_map;\n"
    "uniform mat4 u_spot_vp;\n"
    "uniform int u_spot_light;\n"
    "uniform float u_spot_far;\n"
    "uniform float u_spot_bias;\n"
    "float pbr_spot_shadow(vec3 fragpos, vec3 lpos){\n"
    "  vec4 lc = u_spot_vp * vec4(fragpos, 1.0);\n"
    "  if(lc.w <= 0.0) return 1.0;\n"
    "  vec3 ndc = lc.xyz / lc.w;\n"
    "  vec2 uv = ndc.xy*0.5 + 0.5;\n"
    "  if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return 1.0;\n"
    "  float cur = length(fragpos - lpos);\n"
    "  float lit = 0.0; float st = 1.5/float(textureSize(u_spot_map,0).x);\n"
    "  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x){\n"
    "    float d = texture(u_spot_map, uv + vec2(x,y)*st).r * u_spot_far;\n"
    "    lit += (cur - u_spot_bias > d) ? 0.0 : 1.0;\n"
    "  }\n"
    "  return lit / 9.0;\n"
    "}\n"
    /* Cascaded directional (sun) shadow via VARIANCE shadow mapping. The view\n"
     * frustum is split into u_csm_count cascades; each has a light matrix +\n"
     * virtual eye + far. Two RG32F moment arrays -- static (baked once) and\n"
     * dynamic (per frame, dynamic casters only) -- are co-sampled and the darker\n"
     * (nearer occluder) wins, so a static wall and a moving prop both shadow.\n"
     * Moments are hardware-filtered (mipmapped), so one tap yields a soft edge. */
    "uniform sampler2DArray u_csm_static;\n"
    "uniform sampler2D u_dyn_map;\n"      /* single ortho distance map, dynamic casters. */
    "uniform mat4 u_dyn_vp;\n"
    "uniform vec3 u_dyn_eye;\n"
    "uniform float u_dyn_far;\n"
    "uniform mat4 u_csm_vp[8];\n"
    "uniform vec3 u_csm_eye[8];\n"
    "uniform float u_csm_far[8];\n"
    "uniform float u_csm_texel[8];\n" /* world size of one LOD-0 texel per cascade. */
    "uniform int u_csm_count;\n"
    "uniform int u_csm_enabled;\n"
    "uniform float u_dir_bias;\n"   /* PCSS depth-compare bias (metres). */
    "uniform float u_csm_soft;\n"   /* sun source size (metres) -> penumbra width. */
    "uniform float u_csm_res;\n"    /* static cascade resolution (texels). */
    "uniform int u_csm_pcss;\n"     /* 1 = PCSS variable penumbra; 0 = fixed-width PCF. */
    /* 16-tap Poisson disk in [-1,1]^2, reused for the blocker search and PCF. */
    "const vec2 PZ[16] = vec2[](\n"
    "  vec2(-0.94,-0.34),vec2(0.95,-0.06),vec2(-0.09,-0.93),vec2(0.34,0.29),\n"
    "  vec2(-0.58,0.42),vec2(0.28,0.79),vec2(-0.79,-0.77),vec2(0.75,0.52),\n"
    "  vec2(-0.30,-0.55),vec2(0.55,-0.55),vec2(-0.42,0.90),vec2(0.10,-0.40),\n"
    "  vec2(0.62,0.02),vec2(-0.16,0.30),vec2(-0.83,0.07),vec2(0.42,-0.91));\n"
    /* Dynamic casters: one ortho distance map, 3x3 PCF. Returns lit fraction. */
    "float pbr_dyn_shadow(vec3 fragpos){\n"
    "  vec4 lc = u_dyn_vp * vec4(fragpos, 1.0);\n"
    "  vec3 ndc = lc.xyz / lc.w;\n"
    "  vec2 uv = ndc.xy*0.5 + 0.5;\n"
    "  if(uv.x<0.0||uv.x>1.0||uv.y<0.0||uv.y>1.0) return 1.0;\n"
    "  float cur = length(fragpos - u_dyn_eye) - 0.03;\n" /* small linear bias (metres). */
    "  float lit = 0.0; float st = 1.0/float(textureSize(u_dyn_map,0).x);\n"
    "  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x){\n"
    "    float d = texture(u_dyn_map, uv + vec2(x,y)*st).r * u_dyn_far;\n"
    "    lit += (cur > d) ? 0.0 : 1.0;\n"
    "  }\n"
    "  return lit / 9.0;\n"
    "}\n"
    /* PCSS soft sun shadow on plain linear-depth cascades. Casters are\n"
     * partitioned across cascades by size, so we sample EVERY cascade the fragment\n"
     * lands in and union the occlusion (min). Per cascade: (1) blocker search over\n"
     * the light-source footprint -> average blocker depth, (2) penumbra width =\n"
     * (d_recv - d_blk)/d_blk * lightSize (PCSS), converted to a uv radius via the\n"
     * cascade's world size so all cascades agree, (3) variable-width PCF. */
    "float pbr_csm_shadow(vec3 fragpos, mat2 rot){\n"
    "  if(u_csm_enabled==0) return 1.0;\n"
    "  float vis = 1.0;\n"
    "  float texuv = 1.0 / u_csm_res;\n"                   /* cascade-independent. */
    "  for(int i=0;i<u_csm_count;++i){\n"
    "    vec4 lc = u_csm_vp[i] * vec4(fragpos, 1.0);\n"
    "    vec3 ndc = lc.xyz / lc.w;\n"        /* ortho: w = 1. */
    "    if(any(greaterThan(abs(ndc), vec3(1.0)))) continue;\n" /* no data here. */
    "    vec2 uv = ndc.xy*0.5 + 0.5;\n"
    "    float invfar = 1.0 / u_csm_far[i];\n"
    "    float d = clamp(length(fragpos - u_csm_eye[i]) * invfar, 0.0, 1.0);\n"
    "    float bias = u_dir_bias * invfar;\n"             /* metres -> normalised. */
    "    float uvPerM = 1.0 / (u_csm_texel[i] * u_csm_res);\n" /* uv per world metre. */
    /* Penumbra radius (uv). Default: a fixed width from the sun source size --\n"
     * one PCF pass, no blocker search. PCSS mode: search the source footprint for\n"
     * the average blocker depth and grow the penumbra with the occluder gap. */
    "    float prad;\n"
    "    if(u_csm_pcss==1){\n"
    "      float srad = max(u_csm_soft * uvPerM, texuv);\n"
    "      float bsum = 0.0, bcnt = 0.0;\n"
    "      for(int s=0;s<16;++s){\n"
    "        float dp = texture(u_csm_static, vec3(uv + rot*PZ[s]*srad, float(i))).r;\n"
    "        if(dp < d - bias){ bsum += dp; bcnt += 1.0; }\n"
    "      }\n"
    "      if(bcnt < 0.5) continue;\n"      /* no blocker in this cascade -> lit. */
    "      float dblk = bsum / bcnt;\n"
    /* Penumbra grows with the occluder->receiver gap, CLAMPED to a small world\n"
     * size so the PCF taps never reach across a wall and leak light. */
    "      float pen = (d - dblk) / max(dblk, 1e-4);\n"     /* scale-invariant ratio. */
    "      float penM = min(pen * u_csm_soft, 0.6);\n"      /* world metres, capped. */
    "      prad = clamp(penM * uvPerM, texuv, 12.0*texuv);\n"
    "    } else {\n"
    "      prad = clamp(u_csm_soft * uvPerM, texuv, 6.0*texuv);\n"  /* fixed width. */
    "    }\n"
    /* PCF: 8 taps (per-pixel rotation dithers them so 8 reads smooth). */
    "    float lit = 0.0;\n"
    "    for(int s=0;s<8;++s){\n"
    "      float dp = texture(u_csm_static, vec3(uv + rot*PZ[s]*prad, float(i))).r;\n"
    "      lit += (dp < d - bias) ? 0.0 : 1.0;\n"
    "    }\n"
    /* Cascades PARTITION casters by size, so a fragment lands in several and each\n"
     * holds different occluders (small props in one, the wall+roof shell in\n"
     * another). Union the occlusion by keeping the most-occluded result across\n"
     * every containing cascade -- do NOT break at the first, or a floor point in\n"
     * the small-prop cascade never sees the roof and the sun leaks through it. */
    "    vis = min(vis, lit * 0.125);\n"
    "  }\n"
    "  return min(vis, pbr_dyn_shadow(fragpos));\n"
    "}\n"
    /* Dynamic-light GI (rpg-fo9r): gather the nearest adaptive probes (accel grid\n"
     * in texture buffers) and inverse-distance blend their SH, reconstructed as\n"
     * cosine irradiance for N -- the dynamic indirect term, added to ambient. */
    "uniform int u_gi_enabled;\n"
    /* Regular probe-grid layout (rpg-pau4): when u_probe_grid_on, indirect is
     * TRILINEARLY interpolated from the 8 surrounding grid probes (smooth), not
     * blended from a froxel's nearest-2 candidates (which seams at froxel edges).
     * Probe index = (z*dim.y + y)*dim.x + x; origin/cell are per-axis. */
    "uniform int u_probe_grid_on;\n"
    "uniform vec3 u_probe_grid_origin;\n"
    "uniform vec3 u_probe_grid_cell;\n"
    "uniform vec3 u_probe_grid_dim;\n"   /* float dims (no ivec3 setter); cast in-shader. */
    /* Per-object weights for the probe STATIC indirect (rpg-pau4): baked surfaces
     * already carry the lightmap so want little of it (extra bounce only), while
     * dynamic objects have NO baked GI and take it (boosted) as their only static
     * ambience. main() picks between them by u_sh_object. */
    "uniform float u_gi_static_baked_w;\n"
    "uniform float u_gi_static_dyn_w;\n"
    /* Sky-openness AO (from the probe depth maps): openness * this colour is added
     * as ambient. u_gi_sky_ref is the overhead distance considered "fully open". */
    "uniform vec3 u_gi_sky_color;\n"
    "uniform float u_gi_sky_ref;\n"
    "uniform float u_gi_ao_mult;\n"   /* 0 = no AO on indirect, 1 = full openness multiply. */
    "uniform samplerBuffer u_probe_pos;\n"
    "uniform samplerBuffer u_probe_sh;\n"
    "uniform sampler2D u_probe_depth;\n" /* DDGI oct-depth ATLAS: 10x10 tiles (8x8 + gutter), 256 tiles/row, GL_LINEAR. */
    "uniform samplerBuffer u_probe_sg;\n"    /* SG specular lobe: 2 texels/probe. */
    /* Brick sampling structure (rpg-pjkb): O(1) fragment -> voxel -> brick ->\n"
     * 8 of its 64 probes. Replaces froxel binning for brick-placed sets. */
    "uniform int u_brick_on;\n"
    "uniform isampler3D u_brick_index;\n"    /* R32I voxel -> brick id (-1 none). */
    "uniform samplerBuffer u_brick_meta;\n"  /* RGBA32F: brick min.xyz + size. */
    "uniform usamplerBuffer u_brick_pidx;\n" /* R32UI: 64 probe ids per brick. */
    "uniform usamplerBuffer u_probe_valid;\n"/* R8UI: per-probe validity. */
    "uniform vec3 u_brick_origin;\n"
    "uniform float u_brick_voxel;\n"
    "uniform vec3 u_brick_dim;\n"
    "uniform float u_gi_spec_gain;\n"        /* master scale for probe specular (0=off). */
    "uniform int u_gi_spec_lobes;\n"         /* SG lobes summed per probe (1..3). */
    "uniform usamplerBuffer u_probe_froxel_off;\n"
    "uniform usamplerBuffer u_probe_froxel_cnt;\n"
    "uniform usamplerBuffer u_probe_froxel_idx;\n"
    /* Fills @p e_dyn (dynamic-light indirect) and @p e_stat (baked static indirect)
     * from the nearest probes' two SH4 sets, so main() can weight the static term
     * per object (rpg-pau4). */
    /* Octahedral encode (matches the probe kernel) for the DDGI depth lookup. */
    "vec2 oct_enc(vec3 d){ d/=(abs(d.x)+abs(d.y)+abs(d.z));\n"
    "  vec2 o=(d.z>=0.0)? d.xy : (vec2(1.0)-abs(d.yx))*vec2(d.x>=0.0?1.0:-1.0, d.y>=0.0?1.0:-1.0);\n"
    "  return o*0.5+0.5; }\n"
    /* DDGI visibility weight: probability the shading point @p wp (dist @p dist to
     * the probe along @p dir) is in front of what the probe sees in that
     * direction. Chebyshev on the stored depth mean/variance. */
    /* HARDWARE-bilinear the octahedral depth ATLAS: one GL_LINEAR tap into the\n"
     * probe's 10x10 tile (8x8 interior + 1px oct-wrap gutter, filled by the\n"
     * compute), so filtering is correct through the octahedron edge seam AND\n"
     * there is no 2048-probe array-layer cap. (mean, meanSq). */
    "vec2 depth_bilinear(int probe, vec3 dir){\n"
    "  vec2 tile=vec2(float(probe%256)*10.0, float(probe/256)*10.0);\n"
    "  vec2 px=tile + vec2(1.0) + oct_enc(dir)*8.0;\n"
    "  return texture(u_probe_depth, px/vec2(textureSize(u_probe_depth,0))).rg; }\n"
    /* Softening controls (GI_VIS_*): a larger self-visible band (u_gi_vis_bias) and\n"
     * variance floor (u_gi_vis_varmin), and a gentler exponent (u_gi_vis_sharp,\n"
     * 1=soft) spread the per-probe light/dark transition so the trilinear corners\n"
     * don't flip near-binary at a dynamic occluder's edge (the probe-lattice\n"
     * "dots"). Falloff starts AT the bias edge (d>=0), so it is C0-continuous with\n"
     * the fully-lit branch -- the old code jumped from 1.0 to var/(var+0.15^2). */
    "uniform float u_gi_vis_bias;\n"
    "uniform float u_gi_vis_varmin;\n"
    "uniform float u_gi_vis_sharp;\n"
    "float probe_vis(int probe, vec3 dir, float dist){\n"
    "  vec2 mm=depth_bilinear(probe,dir);\n"
    "  float mean=mm.x; float var=max(mm.y-mean*mean, u_gi_vis_varmin);\n"
    "  if(dist<=mean+u_gi_vis_bias) return 1.0;\n"       /* self-visible band. */
    "  float d=dist-mean-u_gi_vis_bias; float ch=var/(var+d*d);\n"
    "  return pow(ch, u_gi_vis_sharp);\n"                /* 1=soft transition, 2=old/sharp. */
    "}\n"
    /* Sky openness: how far the probe sees toward the sky (world up). A large mean\n"
     * distance overhead -> open -> lets the constant sky ambient in; close geometry\n"
     * overhead -> occluded (cheap AO from the same depth probe). */
    "float probe_sky(int probe){\n"
    "  float mean=depth_bilinear(probe, vec3(0.0,1.0,0.0)).x;\n"
    "  return smoothstep(0.5, u_gi_sky_ref, mean); }\n"
    /* Reconstruct SH4 irradiance (dyn + stat) from 6 interpolated coeff vectors. */
    "void sh_recon(vec4 dR,vec4 dG,vec4 dB,vec4 sR,vec4 sG,vec4 sB,vec3 nn,out vec3 e_dyn,out vec3 e_stat){\n"
    "  vec4 Y=vec4(0.282094792, 0.488602512*nn.y, 0.488602512*nn.z, 0.488602512*nn.x);\n"
    "  vec4 A=vec4(3.14159265, 2.0943951, 2.0943951, 2.0943951);\n"
    "  e_dyn  = max(vec3(dot(dR, A*Y), dot(dG, A*Y), dot(dB, A*Y)), vec3(0.0));\n"
    "  e_stat = max(vec3(dot(sR, A*Y), dot(sG, A*Y), dot(sB, A*Y)), vec3(0.0));\n"
    "}\n"
    "void gi_probe_indirect2(vec3 wp, vec3 nn, int pcl, out vec3 e_dyn, out vec3 e_stat, out float sky_ao){\n"
    "  e_dyn=vec3(0.0); e_stat=vec3(0.0); sky_ao=0.0;\n"
    "  if(u_gi_enabled==0) return;\n"
    /* BRICK path: one integer-texture fetch resolves the covering brick, its\n"
     * local trilinear cell picks 8 of the brick's 64 probes; SH accumulation,\n"
     * visibility and sky openness match the grid path exactly. An uncovered\n"
     * voxel (-1) falls through to the froxel fallback below. */
    "  if(u_brick_on==1){\n"
    "    vec3 wp_b = wp + nn*(0.35*u_brick_voxel/3.0);\n"
    "    ivec3 bdim=ivec3(u_brick_dim+vec3(0.5));\n"
    "    ivec3 v=ivec3(floor((wp_b-u_brick_origin)/u_brick_voxel));\n"
    "    v=clamp(v, ivec3(0), bdim-ivec3(1));\n"
    "    int bid=texelFetch(u_brick_index, v, 0).r;\n"
    "    if(bid>=0){\n"
    "      vec4 bm=texelFetch(u_brick_meta, bid);\n"
    "      float bstep=bm.w/3.0;\n"
    "      vec3 local=clamp((wp_b-bm.xyz)/bstep, vec3(0.0), vec3(2.9999));\n"
    "      ivec3 c0=ivec3(local); vec3 fr=local-vec3(c0);\n"
    "      vec4 dR=vec4(0.0),dG=vec4(0.0),dB=vec4(0.0),sR=vec4(0.0),sG=vec4(0.0),sB=vec4(0.0);\n"
    "      float wsum=0.0, skyacc=0.0, wtri=0.0;\n"
    "      for(int c=0;c<8;++c){\n"
    "        ivec3 o=ivec3(c&1,(c>>1)&1,(c>>2)&1);\n"
    "        vec3 wv=mix(vec3(1.0)-fr, fr, vec3(o));\n"
    "        float w=wv.x*wv.y*wv.z; if(w<=0.0) continue;\n"
    "        ivec3 lc=c0+o; int li=(lc.z*4+lc.y)*4+lc.x;\n"
    "        int probe=int(texelFetch(u_brick_pidx, bid*64+li).r);\n"
    "        skyacc+=w*probe_sky(probe); wtri+=w;\n"
    "        w*=float(texelFetch(u_probe_valid, probe).r);\n"
    "        vec3 ppos=texelFetch(u_probe_pos, probe).xyz;\n"
    "        vec3 pd=wp_b-ppos; float pl=length(pd);\n"
    "        if(pl>1e-4) w*=max(probe_vis(probe, pd/pl, pl), 0.02);\n"
    "        if(w<=0.0) continue; wsum+=w;\n"
    "        int base=probe*6;\n"
    "        dR+=texelFetch(u_probe_sh,base+0)*w; dG+=texelFetch(u_probe_sh,base+1)*w; dB+=texelFetch(u_probe_sh,base+2)*w;\n"
    "        sR+=texelFetch(u_probe_sh,base+3)*w; sG+=texelFetch(u_probe_sh,base+4)*w; sB+=texelFetch(u_probe_sh,base+5)*w;\n"
    "      }\n"
    "      if(wsum>1e-5){ float inv=1.0/wsum;\n"
    "        dR*=inv;dG*=inv;dB*=inv;sR*=inv;sG*=inv;sB*=inv; }\n"
    "      if(wtri>1e-5) sky_ao=skyacc/wtri;\n"
    "      sh_recon(dR,dG,dB,sR,sG,sB,nn,e_dyn,e_stat); return;\n"
    "    }\n"
    "  }\n"
    /* Regular-grid path: trilinearly blend the 8 surrounding grid probes' SH. */
    "  if(u_probe_grid_on==1){\n"
    "    ivec3 gdim=ivec3(u_probe_grid_dim+vec3(0.5));\n"
    /* Normal bias: push the sample point off the surface so it sits IN FRONT of\n"
     * what the probe recorded (dist < mean) -- otherwise a surface exactly at the\n"
     * recorded depth self-occludes and each probe stamps a dark dot (DDGI). */
    "    vec3 wp_b = wp + nn*(0.35*min(min(u_probe_grid_cell.x,u_probe_grid_cell.y),u_probe_grid_cell.z));\n"
    "    vec3 g=(wp_b - u_probe_grid_origin)/u_probe_grid_cell;\n"
    "    vec3 gmax=vec3(gdim)-vec3(1.0);\n"
    "    g=clamp(g, vec3(0.0), gmax-vec3(0.0001));\n"
    "    ivec3 g0=ivec3(floor(g)); vec3 fr=g-vec3(g0);\n"
    "    vec4 dR=vec4(0.0),dG=vec4(0.0),dB=vec4(0.0),sR=vec4(0.0),sG=vec4(0.0),sB=vec4(0.0);\n"
    "    float wsum=0.0, skyacc=0.0, wtri=0.0;\n"
    "    for(int c=0;c<8;++c){\n"
    "      ivec3 o=ivec3(c&1,(c>>1)&1,(c>>2)&1);\n"
    "      vec3 wv=mix(vec3(1.0)-fr, fr, vec3(o));\n"
    "      float w=wv.x*wv.y*wv.z; if(w<=0.0) continue;\n"
    "      ivec3 gc=min(g0+o, gdim-ivec3(1));\n"
    "      int probe=(gc.z*gdim.y + gc.y)*gdim.x + gc.x;\n"
    /* Sky openness interpolated by the plain trilinear (spatial) weight. */
    "      skyacc+=w*probe_sky(probe); wtri+=w;\n"
    /* Visibility weight: cut probes the fragment can't see (behind geometry from\n"
     * the probe) so their irradiance doesn't leak through walls. */
    "      vec3 ppos=u_probe_grid_origin + vec3(gc)*u_probe_grid_cell;\n"
    "      vec3 pd=wp_b-ppos; float pl=length(pd);\n"
    "      if(pl>1e-4) w*=max(probe_vis(probe, pd/pl, pl), 0.02);\n"
    "      if(w<=0.0) continue; wsum+=w;\n"
    "      int base=probe*6;\n"
    "      dR+=texelFetch(u_probe_sh,base+0)*w; dG+=texelFetch(u_probe_sh,base+1)*w; dB+=texelFetch(u_probe_sh,base+2)*w;\n"
    "      sR+=texelFetch(u_probe_sh,base+3)*w; sG+=texelFetch(u_probe_sh,base+4)*w; sB+=texelFetch(u_probe_sh,base+5)*w;\n"
    "    }\n"
    "    if(wsum>1e-5){ float inv=1.0/wsum;\n"
    "      dR*=inv;dG*=inv;dB*=inv;sR*=inv;sG*=inv;sB*=inv; }\n"
    "    if(wtri>1e-5) sky_ao=skyacc/wtri;\n"
    "    sh_recon(dR,dG,dB,sR,sG,sB,nn,e_dyn,e_stat); return;\n"
    "  }\n"
    /* Probes are binned into the SAME froxels as the forward+ lights; @p pcl is the\n"
     * fragment's cluster (computed once in main), so just read its probe list. */
    "  int poff=int(texelFetch(u_probe_froxel_off,pcl).r);\n"
    "  int pcnt=int(texelFetch(u_probe_froxel_cnt,pcl).r);\n"
    /* Nearest 2 probes among this froxel's candidates (denser probe set -> 2 is\n"
     * enough for a smooth blend). */
    "  int n0=-1,n1=-1; float e0=1e30,e1=1e30;\n"
    "  for(int pit=0; pit<pcnt; ++pit){\n"
    "    int probe=int(texelFetch(u_probe_froxel_idx,poff+pit).r);\n"
    "    vec3 pp=texelFetch(u_probe_pos,probe).xyz;\n"
    "    vec3 dd=wp-pp; float e=dot(dd,dd);\n"
    "    if(e<e0){ e1=e0;n1=n0; e0=e;n0=probe; }\n"
    "    else if(e<e1){ e1=e;n1=probe; }\n"
    "  }\n"
    "  if(n0<0) return;\n"
    /* Each probe is 6 RGBA texels: 3 dynamic SH4 coeff vectors then 3 static.\n"
     * Inverse-distance blend the nearest probes' coeff vectors, reconstruct once. */
    "  vec4 dR=vec4(0.0), dG=vec4(0.0), dB=vec4(0.0);\n"
    "  vec4 sR=vec4(0.0), sG=vec4(0.0), sB=vec4(0.0); float wsum=0.0;\n"
    "  int ni[2]; float ne[2]; ni[0]=n0;ni[1]=n1; ne[0]=e0;ne[1]=e1;\n"
    "  for(int j=0;j<2;++j){ if(ni[j]<0) continue; float w=1.0/(ne[j]+1e-4); wsum+=w;\n"
    "    int base=ni[j]*6;\n"
    "    dR+=texelFetch(u_probe_sh,base+0)*w; dG+=texelFetch(u_probe_sh,base+1)*w; dB+=texelFetch(u_probe_sh,base+2)*w;\n"
    "    sR+=texelFetch(u_probe_sh,base+3)*w; sG+=texelFetch(u_probe_sh,base+4)*w; sB+=texelFetch(u_probe_sh,base+5)*w;\n"
    "  }\n"
    "  if(wsum<=0.0) return;\n"
    "  float inv=1.0/wsum;\n"       /* nn is already normalised (passed from main). */
    /* Linear SH (band 0 + band 1) cosine-convolved irradiance: A0=pi, A1=2pi/3. */
    "  vec4 Y=vec4(0.282094792, 0.488602512*nn.y, 0.488602512*nn.z, 0.488602512*nn.x);\n"
    "  vec4 A=vec4(3.14159265, 2.0943951, 2.0943951, 2.0943951);\n"
    "  e_dyn  = max(vec3(dot(dR, A*Y), dot(dG, A*Y), dot(dB, A*Y))*inv, vec3(0.0));\n"
    "  e_stat = max(vec3(dot(sR, A*Y), dot(sG, A*Y), dot(sB, A*Y))*inv, vec3(0.0));\n"
    "}\n"
    /* Probe SPECULAR (rpg-hw75): trilinearly blend the 8 grid probes' single SG\n"
     * lobe evaluated along the reflection vector R. Roughness widens the lobe\n"
     * (smaller effective sharpness) -> blurrier reflection. Returns radiance. */
    "vec3 gi_probe_specular(vec3 wp, vec3 nn, vec3 R, float rough){\n"
    "  if(u_gi_enabled==0 || u_gi_spec_gain<=0.0) return vec3(0.0);\n"
    "  if(u_probe_grid_on==0 && u_brick_on==0) return vec3(0.0);\n"
    /* Brick mode: same 8 probes the diffuse resolved; SG lobes per probe. */
    "  if(u_brick_on==1){\n"
    "    vec3 wp_b = wp + nn*(0.35*u_brick_voxel/3.0);\n"
    "    ivec3 bdim=ivec3(u_brick_dim+vec3(0.5));\n"
    "    ivec3 v=ivec3(floor((wp_b-u_brick_origin)/u_brick_voxel));\n"
    "    v=clamp(v, ivec3(0), bdim-ivec3(1));\n"
    "    int bid=texelFetch(u_brick_index, v, 0).r;\n"
    "    if(bid<0) return vec3(0.0);\n"
    "    vec4 bm=texelFetch(u_brick_meta, bid);\n"
    "    float bstep=bm.w/3.0;\n"
    "    vec3 local=clamp((wp_b-bm.xyz)/bstep, vec3(0.0), vec3(2.9999));\n"
    "    ivec3 c0=ivec3(local); vec3 fr=local-vec3(c0);\n"
    "    vec3 acc=vec3(0.0); float wsum=0.0;\n"
    "    float rw=clamp(1.0-rough, 0.05, 1.0);\n"
    "    for(int c=0;c<8;++c){\n"
    "      ivec3 o=ivec3(c&1,(c>>1)&1,(c>>2)&1);\n"
    "      vec3 wv=mix(vec3(1.0)-fr, fr, vec3(o));\n"
    "      float w=wv.x*wv.y*wv.z; if(w<=0.0) continue;\n"
    "      ivec3 lc=c0+o; int li=(lc.z*4+lc.y)*4+lc.x;\n"
    "      int probe=int(texelFetch(u_brick_pidx, bid*64+li).r);\n"
    "      w*=float(texelFetch(u_probe_valid, probe).r);\n"
    "      vec3 ppos=texelFetch(u_probe_pos, probe).xyz;\n"
    "      vec3 pd=wp_b-ppos; float pl=length(pd);\n"
    "      if(pl>1e-4) w*=max(probe_vis(probe, pd/pl, pl), 0.02);\n"
    "      if(w<=0.0) continue;\n"
    "      vec3 lobesum=vec3(0.0);\n"
    "      for(int L=0;L<u_gi_spec_lobes;++L){\n"
    "        vec4 a=texelFetch(u_probe_sg, (probe*3+L)*2+0);\n"
    "        vec4 b=texelFetch(u_probe_sg, (probe*3+L)*2+1);\n"
    "        float keff=a.w*rw; vec3 ax=normalize(a.xyz);\n"
    "        lobesum += b.rgb*exp(keff*(dot(R, ax)-1.0));\n"
    "      }\n"
    "      acc+=w*lobesum; wsum+=w;\n"
    "    }\n"
    "    if(wsum>1e-5) acc/=wsum;\n"
    "    return acc*u_gi_spec_gain;\n"
    "  }\n"
    "  ivec3 gdim=ivec3(u_probe_grid_dim+vec3(0.5));\n"
    "  vec3 wp_b = wp + nn*(0.35*min(min(u_probe_grid_cell.x,u_probe_grid_cell.y),u_probe_grid_cell.z));\n"
    "  vec3 g=(wp_b - u_probe_grid_origin)/u_probe_grid_cell;\n"
    "  g=clamp(g, vec3(0.0), vec3(gdim)-vec3(1.0001));\n"
    "  ivec3 g0=ivec3(floor(g)); vec3 fr=g-vec3(g0);\n"
    "  vec3 acc=vec3(0.0); float wsum=0.0;\n"
    "  float rw=clamp(1.0-rough, 0.05, 1.0);\n"        /* roughness -> lobe narrowing. */
    "  for(int c=0;c<8;++c){\n"
    "    ivec3 o=ivec3(c&1,(c>>1)&1,(c>>2)&1);\n"
    "    vec3 wv=mix(vec3(1.0)-fr, fr, vec3(o)); float w=wv.x*wv.y*wv.z; if(w<=0.0) continue;\n"
    "    ivec3 gc=min(g0+o, gdim-ivec3(1)); int probe=(gc.z*gdim.y + gc.y)*gdim.x + gc.x;\n"
    /* Visibility-weight like the diffuse so probes behind the roof/walls can't\n"
     * leak their reflection onto surfaces they don't see. */
    "    vec3 ppos=u_probe_grid_origin + vec3(gc)*u_probe_grid_cell;\n"
    "    vec3 pd=wp_b-ppos; float pl=length(pd);\n"
    "    if(pl>1e-4) w*=max(probe_vis(probe, pd/pl, pl), 0.02);\n"
    "    if(w<=0.0) continue;\n"
    /* Sum the probe's 3 SG lobes evaluated along R (multi-lobe: fire + windows). */
    "    vec3 lobesum=vec3(0.0);\n"
    "    for(int L=0;L<u_gi_spec_lobes;++L){\n"
    "      vec4 a=texelFetch(u_probe_sg, (probe*3+L)*2+0);\n"   /* axis.xyz, kappa. */
    "      vec4 b=texelFetch(u_probe_sg, (probe*3+L)*2+1);\n"   /* rgb, pad. */
    "      float keff=a.w*rw;\n"                                /* rougher -> lower sharpness. */
    "      vec3 ax=normalize(a.xyz);\n"                         /* temporal blend shortens it. */
    "      lobesum += b.rgb*exp(keff*(dot(R, ax)-1.0));\n"
    "    }\n"
    "    acc+=w*lobesum; wsum+=w;\n"
    "  }\n"
    "  if(wsum>1e-5) acc/=wsum;\n"
    "  return acc*u_gi_spec_gain;\n"
    "}\n"
    "void main() {\n"
    /* OVERDRAW debug (mode 11): emit a small constant and bail BEFORE any shading.
     * Drawn with additive blend + GL_ALWAYS depth (see render_forward), so the
     * framebuffer accumulates one increment per fragment that WOULD shade -- a
     * heatmap of overdraw (== redundant probe/lightmap sampling on occluded geo:
     * every such fragment runs the full 8-corner probe trilinear). */
    "  if(u_debug_mode==11){ frag=vec4(0.07,0.025,0.006,1.0); return; }\n"
    /* Material textures tile at u_uv_scale; the lightmap (v_uv1) is NOT scaled. */
    "  vec2 muv = v_uv0 * u_uv_scale;\n"
    "  vec3 albedo = u_tint;\n"
    "  if(u_has_albedo==1) albedo *= texture(u_albedo_map, muv).rgb;\n"
    /* Albedo contrast about mid-grey (1 = none; >1 makes brick faces vs mortar\n"
     * pop). Applied in linear space before lighting. */
    "  albedo = clamp((albedo - 0.5) * u_contrast + 0.5, 0.0, 1.0);\n"
    "  float metal = u_metalness;\n"
    "  if(u_has_metallic==1) metal *= texture(u_metallic_map, muv).r;\n"
    "  metal = clamp(metal, 0.0, 1.0);\n"
    /* ORM-packed: one fetch of the roughness map gives ao (.r) AND roughness (.g),\n"
     * replacing the separate ao + roughness textures. */
    "  float rsample; float ao = 1.0;\n"
    "  if(u_orm_packed==1){ vec2 orm = texture(u_roughness_map, muv).rg;\n"
    "    ao = mix(1.0, orm.r, u_ao_strength); rsample = orm.g; }\n"
    "  else {\n"
    "    rsample = (u_has_roughness==1) ? texture(u_roughness_map, muv).r : 1.0;\n"
    "    if(u_has_ao==1) ao = mix(1.0, texture(u_ao_map, muv).r, u_ao_strength);\n"
    "  }\n"
    "  float rough = clamp(mix(u_roughness_min, u_roughness_max, rsample), 0.045, 1.0);\n"
    "  vec3 Ng = normalize(v_normal);\n"   /* geometric (unmapped) normal. */
    "  vec3 N = Ng;\n"
    "  if(u_has_normal==1){\n"
    "    vec3 tn = texture(u_normal_map, muv).xyz*2.0-1.0;\n"
    "    tn.xy *= u_normal_scale;\n"
    "    mat3 TBN = mat3(normalize(v_tangent), normalize(v_bitangent), N);\n"
    "    N = normalize(TBN * tn);\n"
    "  }\n"
    /* Muted normal for the low-frequency INDIRECT terms (baked lightmap + probe\n"
     * GI): partway from geometric to mapped so the detail relief doesn't over-\n"
     * modulate the soft bounce. Direct sun + specular still use the full N. */
    "  vec3 Nsh = normalize(mix(Ng, N, u_sh_normal_bias));\n"
    "  vec3 V = normalize(u_eye_pos - v_world_pos);\n"
    "  vec3 F0 = mix(vec3(0.08*u_specular_strength), albedo, metal);\n"
    /* This fragment's forward+ cluster -- computed ONCE (one log) and shared by\n"
     * both the clustered light loop and the probe gather. */
    "  int cdimx=int(u_cluster_dims.x), cdimy=int(u_cluster_dims.y), cdimz=int(u_cluster_dims.z);\n"
    "  int ctx=clamp(int(gl_FragCoord.x/u_screen.x*u_cluster_dims.x),0,cdimx-1);\n"
    "  int cty=clamp(int(gl_FragCoord.y/u_screen.y*u_cluster_dims.y),0,cdimy-1);\n"
    "  float cvz=max(-v_view_z,u_cluster_near);\n"
    "  int csl=clamp(int(log(cvz/u_cluster_near)*u_inv_log_farnear*u_cluster_dims.z),0,cdimz-1);\n"
    "  int frag_cluster=(csl*cdimy+cty)*cdimx+ctx;\n"
    /* Fragment-constant PCF kernel rotation (dithers both the CSM and cube taps),\n"
     * built ONCE instead of per shadowed light. Interleaved gradient noise angle. */
    "  float kang = fract(52.9829189*fract(dot(gl_FragCoord.xy, vec2(0.06711056,0.00583715))))*6.2831853;\n"
    "  float kc=cos(kang), ks=sin(kang);\n"
    "  mat2 krot = mat2(kc,-ks,ks,kc);\n"
    /* Perf probe: 9 = material fetches only (no lighting), isolates fill/bandwidth. */
    "  if(u_debug_mode==9){ frag=vec4(albedo*ao,1.0); return; }\n"
    /* Directional sun. */
    "  vec3 direct = pbr_light(N, V, normalize(u_sun_dir), albedo, rough, metal, F0) * u_sun_color * pbr_csm_shadow(v_world_pos, krot);\n"
    "  float dbg_cubesh = 1.0;\n"   /* raw cube-shadow of the nearest shadowed clustered light. */
    "  if(u_clustered==1){\n"
    /* Forward+: shade only this cluster's lights. */
    "    int cluster = frag_cluster;\n"
    "    int roff = texelFetch(u_cluster_offset, cluster).r;\n"
    "    int rcnt = texelFetch(u_cluster_count, cluster).r;\n"
    "    for(int i=0; i<rcnt; ++i){\n"
    "      int li = texelFetch(u_light_index, roff + i).r;\n"
    "      int b = li*4;\n"
    "      vec4 t0=texelFetch(u_light_data,b), t1=texelFetch(u_light_data,b+1),\n"
    "           t2=texelFetch(u_light_data,b+2), t3=texelFetch(u_light_data,b+3);\n"
    /* Unshadowed contribution FIRST (cheap: range/cone/N.L cull). Only sample the\n"
     * shadow map when the light actually reaches this fragment -- skips ~8 cube\n"
     * taps for every out-of-range shadowed light in the cluster. */
    "      vec3 lc = pbr_accumulate(int(t0.x), t0.yzw, t1.xyz, t2.xyz, t1.w, t2.w, t3.x,\n"
    "                               N,V,albedo,rough,metal,F0);\n"
    "      if(dot(lc,lc) > 1e-8){\n"
    "        float sh = 1.0;\n"
    "        if(t3.y>=0.0){ float cs=pbr_cube_shadow(t0.yzw, t3.y, krot); sh *= cs; dbg_cubesh=min(dbg_cubesh,cs); }\n"
    "        if(li==u_spot_light) sh *= pbr_spot_shadow(v_world_pos, t0.yzw);\n"
    "        direct += sh * lc;\n"
    "      }\n"
    "    }\n"
    "  } else {\n"
    /* Direct uniform-array path (non-clustered). */
    "    for(int i=0; i<u_light_count && i<PBR_MAX_LIGHTS; ++i)\n"
    "      direct += pbr_accumulate(u_light_type[i], u_light_pos[i], u_light_dir[i],\n"
    "                               u_light_color[i], u_light_range[i], u_light_cos_inner[i],\n"
    "                               u_light_cos_outer[i], N,V,albedo,rough,metal,F0);\n"
    "  }\n"
    "  if(u_debug_mode==12){ frag=vec4(direct,1.0); return; }\n"  /* sun + clustered punctual (debug). */
    "  if(u_debug_mode==8){ frag=vec4(vec3(dbg_cubesh),1.0); return; }\n"
    "  if(u_debug_mode==1){ frag=vec4(texture(u_sh0,SHUV).rgb,1.0); return; }\n"
    "  if(u_debug_mode==2){ frag=vec4(max(pbr_sh_irradiance(N),vec3(0.0)),1.0); return; }\n"
    "  if(u_debug_mode==3){ frag=vec4(v_uv1,0.0,1.0); return; }\n"
    "  if(u_debug_mode==4){ frag=vec4(0.5+0.5*N,1.0); return; }\n"
    /* 5 = raw CSM shadow factor (white=lit, black=occluded). 6 = finest cascade\n"
     * whose box contains the fragment (red=0, green=1, blue=2+). */
    "  if(u_debug_mode==5){ float sh=pbr_csm_shadow(v_world_pos, krot); frag=vec4(vec3(sh),1.0); return; }\n"
    "  if(u_debug_mode==6){ int ci=-1; for(int i=0;i<u_csm_count;++i){ vec4 lc=u_csm_vp[i]*vec4(v_world_pos,1.0); vec3 nd=lc.xyz/lc.w; if(all(lessThanEqual(abs(nd),vec3(1.0)))){ci=i;break;} }\n"
    "    vec3 cc=ci<0?vec3(0.1):(ci==0?vec3(1,0,0):(ci==1?vec3(0,1,0):vec3(0,0,1))); frag=vec4(cc,1.0); return; }\n"
    "  if(u_debug_mode==5){ frag=vec4(0.5+0.5*normalize(v_tangent),1.0); return; }\n"
    "  vec3 ambient;\n"
    /* Dynamic objects (u_sh_object=0) are not in the bake -- their uv1 is
     * meaningless, so fall back to the flat ambient instead of the lightmap. */
    /* u_ambient is a flat sky-colour fill applied everywhere; the baked lightmap\n"
     * SH then adds on top for static (baked) geometry. Dynamic geometry, whose\n"
     * uv1 is meaningless, gets only the flat fill. */
    "  ambient = u_ambient*albedo*ao;\n"
    /* Environment BRDF split (Karis): kS = specular env response, kD = diffuse
     * remainder * (1-metal). Shared by the lightmap IBL and the probe IBL so the
     * static AND dynamic indirect both drive the full PBR (diffuse + specular). */
    "  float NoV = max(dot(N,V), 1e-4);\n"
    "  vec3 kS = env_brdf_approx(F0, rough, NoV);\n"
    "  vec3 kD = (vec3(1.0)-kS) * (1.0-metal);\n"
    "  vec3 Rspec = reflect(-V, N);\n"
    /* Baked lightmap as a FULL split-sum IBL term: fetch the luxel's SH9 once,\n"
     * take the cosine-convolved SH9 irradiance for diffuse and the raw SH9
     * radiance along R for the (blurry, static) specular reflection. */
    "  if(u_sh_enabled==1 && u_sh_object>0.5 && u_sh_layer>=0){\n"
    "    vec3 L[9]; sh9_fetch(L);\n"
    /* Diffuse irradiance along the muted indirect normal (Nsh); specular (Lr)\n"
     * keeps the full mapped N. */
    "    vec3 E = max(sh9_irradiance(L,Nsh), vec3(0.0));\n"
    "    vec3 Lr = max(sh9_radiance(L,Rspec), vec3(0.0));\n"
    "    ambient += (kD*albedo*E/PI + kS*Lr) * u_sh_scale * ao;\n"
    "  }\n"
    /* Dynamic-light indirect from the SDF-probe GI: incident irradiance E(N) ->\n"
     * diffuse albedo/PI * E, on top of the baked static ambient. */
    /* Probe indirect: dynamic always; static weighted by whether this object is\n"
     * baked (has the lightmap) or dynamic (needs the boosted static ambience). */
    "  vec3 gi_dyn, gi_stat; float gi_sky; gi_probe_indirect2(v_world_pos, Nsh, frag_cluster, gi_dyn, gi_stat, gi_sky);\n"
    "  float sgw = (u_sh_object>0.5) ? u_gi_static_baked_w : u_gi_static_dyn_w;\n"
    /* AO from sky openness, applied MULTIPLICATIVELY to the indirect (enclosed\n"
     * creases lose bounce). u_gi_ao_mult blends 1..openness. */
    "  float ao_o = mix(1.0, gi_sky, u_gi_ao_mult) * ao;\n"
    /* Full split-sum IBL from the probes: the SH gives the diffuse irradiance and\n"
     * the SG lobe the prefiltered specular reflection; the environment BRDF splits\n"
     * energy between them (kS specular, kD = (1-kS)(1-metal) diffuse) so the probes\n"
     * drive the WHOLE PBR response, not just diffuse. (kS/kD/Rspec above.) */
    "  vec3 irr = gi_dyn + sgw*gi_stat;\n"
    "  vec3 prefiltered = gi_probe_specular(v_world_pos, N, Rspec, rough);\n"
    "  vec3 diff_ibl = kD * albedo * irr / PI;\n"
    "  vec3 spec_ibl = kS * prefiltered;\n"
    "  if(u_debug_mode==7){ frag=vec4(irr,1.0); return; }\n"
    "  if(u_debug_mode==10){ frag=vec4(spec_ibl*ao_o,1.0); return; }\n"
    "  ambient += (diff_ibl + spec_ibl) * ao_o;\n"
    /* Sky-openness ambient (constant sky colour where open overhead). */
    "  ambient += kD * albedo * gi_sky * (0.5+0.5*N.y) * u_gi_sky_color * ao;\n"
    "  vec3 color = direct + ambient;\n"
    /* Emissive self-shading: the surface shows its own emission (the actual\n"
     * emissive LIGHTING of the scene is baked into the lightmap). Driven by the\n"
     * emissive colour, modulated by the emissive map when present. */
    "  vec3 emissive = u_emissive_color * u_emissive_strength;\n"
    "  if(u_has_emissive==1) emissive *= texture(u_emissive_map,muv).rgb;\n"
    "  color += emissive;\n"
    "  color = color/(color+vec3(1.0));\n"
    "  color = pow(color, vec3(1.0/2.2));\n"
    "  frag = vec4(color,1.0);\n"
    "}\n";

const char *pbr_shader_vertex_source(void) { return PBR_VS; }
const char *pbr_shader_fragment_source(void) { return PBR_FS; }

shader_program_status_t pbr_shader_create(shader_program_t *out,
                                          const gl_loader_t *loader, char *log,
                                          size_t log_cap)
{
    return shader_program_create(out, loader, PBR_VS, PBR_FS, log, log_cap);
}
