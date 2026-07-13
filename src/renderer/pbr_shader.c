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
    "void main() {\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_world_pos = wp.xyz;\n"
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
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "in vec3 v_tangent;\n"
    "in vec3 v_bitangent;\n"
    "in vec2 v_uv0;\n"
    "in vec2 v_uv1;\n"
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
    "uniform int u_has_emissive;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_specular_strength;\n"
    "uniform float u_metalness;\n"
    "uniform float u_roughness_min;\n"
    "uniform float u_roughness_max;\n"
    "uniform float u_emissive_strength;\n"
    "uniform float u_normal_scale;\n"
    "uniform float u_ao_strength;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform vec3 u_sun_dir;\n"
    "uniform vec3 u_sun_color;\n"
    "uniform vec3 u_ambient;\n"
    "const float PI = 3.14159265359;\n"
    "float D_GGX(float NoH, float a){ float a2=a*a; float d=(NoH*NoH*(a2-1.0)+1.0); return a2/max(PI*d*d,1e-7); }\n"
    "float G_SchlickGGX(float NoX, float k){ return NoX/(NoX*(1.0-k)+k); }\n"
    "float G_Smith(float NoV, float NoL, float r){ float k=(r+1.0); k=k*k/8.0; return G_SchlickGGX(NoV,k)*G_SchlickGGX(NoL,k); }\n"
    "vec3 F_Schlick(float VoH, vec3 F0){ return F0 + (1.0-F0)*pow(clamp(1.0-VoH,0.0,1.0),5.0); }\n"
    "void main() {\n"
    "  vec3 albedo = u_tint;\n"
    "  if(u_has_albedo==1) albedo *= texture(u_albedo_map, v_uv0).rgb;\n"
    "  float metal = u_metalness;\n"
    "  if(u_has_metallic==1) metal *= texture(u_metallic_map, v_uv0).r;\n"
    "  metal = clamp(metal, 0.0, 1.0);\n"
    "  float rsample = (u_has_roughness==1) ? texture(u_roughness_map, v_uv0).r : 1.0;\n"
    "  float rough = clamp(mix(u_roughness_min, u_roughness_max, rsample), 0.045, 1.0);\n"
    "  float ao = 1.0;\n"
    "  if(u_has_ao==1) ao = mix(1.0, texture(u_ao_map, v_uv0).r, u_ao_strength);\n"
    "  vec3 N = normalize(v_normal);\n"
    "  if(u_has_normal==1){\n"
    "    vec3 tn = texture(u_normal_map, v_uv0).xyz*2.0-1.0;\n"
    "    tn.xy *= u_normal_scale;\n"
    "    mat3 TBN = mat3(normalize(v_tangent), normalize(v_bitangent), N);\n"
    "    N = normalize(TBN * tn);\n"
    "  }\n"
    "  vec3 V = normalize(u_eye_pos - v_world_pos);\n"
    "  vec3 L = normalize(u_sun_dir);\n"
    "  vec3 H = normalize(V+L);\n"
    "  float NoV = max(dot(N,V),1e-4);\n"
    "  float NoL = max(dot(N,L),0.0);\n"
    "  float NoH = max(dot(N,H),0.0);\n"
    "  float VoH = max(dot(V,H),0.0);\n"
    "  vec3 F0 = mix(vec3(0.08*u_specular_strength), albedo, metal);\n"
    "  float D = D_GGX(NoH, rough*rough);\n"
    "  float G = G_Smith(NoV, NoL, rough);\n"
    "  vec3  F = F_Schlick(VoH, F0);\n"
    "  vec3 spec = (D*G)*F / max(4.0*NoV*NoL, 1e-4);\n"
    "  vec3 kd = (vec3(1.0)-F)*(1.0-metal);\n"
    "  vec3 diffuse = kd*albedo/PI;\n"
    "  vec3 direct = (diffuse+spec)*u_sun_color*NoL;\n"
    "  vec3 ambient = u_ambient*albedo*ao;\n"
    "  vec3 color = direct + ambient;\n"
    "  if(u_has_emissive==1) color += texture(u_emissive_map,v_uv0).rgb*u_emissive_strength;\n"
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
