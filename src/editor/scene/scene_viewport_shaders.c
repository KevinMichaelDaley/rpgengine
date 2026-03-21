/**
 * @file scene_viewport_shaders.c
 * @brief Additional viewport shaders and primitive mesh registration.
 *
 * Separated from scene_viewport_render.c (at 4/4 non-static limit).
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_render_init_extra_shaders
 *   viewport_render_destroy_extra_shaders
 *   viewport_render_init_primitives
 */

#include "ferrum/editor/scene/scene_viewport_render.h"

#include <stdio.h>
#include <string.h>

/* ---- Matcap shader sources ---- */

/**
 * Matcap vertex shader: identical transform to entity shader.
 * Passes world position and normal to fragment for half-lambert lighting.
 */
static const char *const MATCAP_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec3 in_normal;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec3 v_world_pos;\n"
    "out vec3 v_normal;\n"
    "void main() {\n"
    "    vec4 world = u_model * vec4(in_position, 1.0);\n"
    "    v_world_pos = world.xyz;\n"
    "    v_normal = mat3(u_model) * in_normal;\n"
    "    gl_Position = u_projection * u_view * world;\n"
    "}\n";

/**
 * Matcap fragment shader: half-lambert lighting with clay-like appearance.
 *
 * Half-lambert wraps the diffuse lighting around the object for a softer,
 * more sculpted look. A beige off-white base color, subtle warm fill from
 * below, and a fresnel rim term give a polished clay/matcap feel that
 * clearly reveals surface curvature and edge silhouettes.
 */
static const char *const MATCAP_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "uniform vec3 u_color;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform vec3 u_select_color;\n"
    "uniform float u_select_tint;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    /* Beige off-white base tint for clay-like appearance. */
    "    vec3 base = u_color * vec3(0.92, 0.88, 0.82);\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 l = normalize(u_light_dir);\n"
    /* Half-lambert: remap dot [-1,1] to [0,1] then square for soft wrap. */
    "    float half_lambert = dot(n, l) * 0.5 + 0.5;\n"
    "    half_lambert = half_lambert * half_lambert;\n"
    /* Subtle warm fill from below for clay-like depth. */
    "    float fill = dot(n, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5;\n"
    "    fill = fill * 0.08;\n"
    /* Fresnel rim: highlights silhouette edges so unlit edges are visible. */
    "    vec3 v = normalize(u_eye_pos - v_world_pos);\n"
    "    float fresnel = 1.0 - max(dot(n, v), 0.0);\n"
    "    fresnel = fresnel * fresnel * 0.35;\n"
    /* Combine: moderate ambient + half-lambert diffuse + fill + rim. */
    "    vec3 ambient = 0.2 * base;\n"
    "    vec3 diffuse = half_lambert * base * 0.7;\n"
    "    vec3 warm_fill = fill * vec3(0.95, 0.85, 0.75);\n"
    "    vec3 rim = fresnel * vec3(0.85, 0.85, 0.9);\n"
    "    vec3 result = ambient + diffuse + warm_fill + rim;\n"
    "    result = mix(result, u_select_color, u_select_tint);\n"
    "    frag_color = vec4(result, 1.0);\n"
    "}\n";

/* ---- Flat (unlit) shader sources ---- */

/** Flat vertex shader: simple MVP transform, no lighting outputs. */
static const char *const FLAT_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec3 in_normal;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "void main() {\n"
    "    gl_Position = u_projection * u_view * u_model * vec4(in_position, 1.0);\n"
    "}\n";

/** Flat fragment shader: outputs solid color with no lighting. */
static const char *const FLAT_FRAG_SRC =
    "#version 330 core\n"
    "uniform vec3 u_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = vec4(u_color, 1.0);\n"
    "}\n";

/* ---- Public API ---- */

bool viewport_render_init_extra_shaders(viewport_render_state_t *state) {
    if (!state) return false;

    char log[512];

    /* Compile the matcap shader (half-lambert clay). */
    shader_program_status_t ss = shader_program_create(
        &state->matcap_shader, &state->loader, MATCAP_VERT_SRC, MATCAP_FRAG_SRC,
        log, sizeof(log));
    if (ss != SHADER_PROGRAM_OK) {
        fprintf(stderr, "viewport_shaders: matcap shader error: %s\n", log);
        return false;
    }
    shader_uniform_cache_init(&state->matcap_uniforms, &state->matcap_shader);

    /* Compile the flat shader (unlit solid color). */
    ss = shader_program_create(
        &state->flat_shader, &state->loader, FLAT_VERT_SRC, FLAT_FRAG_SRC,
        log, sizeof(log));
    if (ss != SHADER_PROGRAM_OK) {
        fprintf(stderr, "viewport_shaders: flat shader error: %s\n", log);
        shader_program_destroy(&state->matcap_shader);
        return false;
    }
    shader_uniform_cache_init(&state->flat_uniforms, &state->flat_shader);

    return true;
}

void viewport_render_destroy_extra_shaders(viewport_render_state_t *state) {
    if (!state) return;
    shader_program_destroy(&state->flat_shader);
    shader_program_destroy(&state->matcap_shader);
}

/* ---- Primitive mesh registration ---- */

/**
 * @brief Helper: create a primitive via a generator and register it.
 *
 * Creates the mesh into a temporary, then moves it into the registry
 * slot via memcpy (the registry owns the GPU resources afterward).
 */
static bool register_primitive_(mesh_registry_t *reg,
                                 const gl_loader_t *loader,
                                 int (*generator)(const gl_loader_t *, static_mesh_t *),
                                 mesh_handle_t *out) {
    static_mesh_t tmp;
    if (generator(loader, &tmp) != 0) {
        fprintf(stderr, "viewport_shaders: primitive creation failed\n");
        return false;
    }
    /* Allocate a registry slot and move the mesh into it. */
    uint32_t idx = reg->freelist[reg->freelist_count - 1];
    reg->freelist_count--;
    reg->count++;
    reg->types[idx] = MESH_TYPE_STATIC;
    reg->meshes[idx].stat = tmp;
    out->index = idx;
    out->generation = reg->generations[idx];
    return true;
}

/** Wrapper: create box with fixed dimensions. */
static int create_box_(const gl_loader_t *l, static_mesh_t *out) {
    return static_mesh_create_box(l, 1.0f, 1.0f, 1.0f, out);
}
/** Wrapper: create sphere with fixed dimensions. */
static int create_sphere_(const gl_loader_t *l, static_mesh_t *out) {
    return static_mesh_create_sphere(l, 0.5f, 16, 12, out);
}
/** Wrapper: create capsule with fixed dimensions. */
static int create_capsule_(const gl_loader_t *l, static_mesh_t *out) {
    return static_mesh_create_capsule(l, 0.3f, 0.5f, 16, 4, out);
}
/** Wrapper: create plane with fixed dimensions. */
static int create_plane_(const gl_loader_t *l, static_mesh_t *out) {
    return static_mesh_create_plane(l, 10.0f, 10.0f, out);
}

bool viewport_render_init_primitives(viewport_render_state_t *state) {
    if (!state) return false;

    if (!register_primitive_(&state->meshes, &state->loader,
                              create_box_, &state->mesh_box)) return false;
    if (!register_primitive_(&state->meshes, &state->loader,
                              create_sphere_, &state->mesh_sphere)) return false;
    if (!register_primitive_(&state->meshes, &state->loader,
                              create_capsule_, &state->mesh_capsule)) return false;
    if (!register_primitive_(&state->meshes, &state->loader,
                              create_plane_, &state->mesh_plane)) return false;

    return true;
}
