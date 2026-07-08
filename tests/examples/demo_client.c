/**
 * @file demo_client.c
 * @brief Demo client with client-side prediction.
 *
 * Connects to the demo server, receives BODY_SPAWN (reliable) and
 * SNAPSHOT_CHUNK (unreliable) messages.  Maintains a local phys_world
 * in prediction mode (integration only, no collision) and reconciles
 * against server snapshots via snap/blend.
 *
 * Usage:  ./demo_client <server_ip> <port> [duration_s] [--headless]
 *                       [--record FILE]
 */

#define _POSIX_C_SOURCE 200809L

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ferrum/math/constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include "ferrum/mesh/obj_loader.h"

#include "ferrum/procgen/procgen_level_load.h"
#include "ferrum/procgen/procgen_srd_level_load.h"

#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/mesh_data.h"
#include "ferrum/net/replication/prediction_tick.h"

#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/snapshot_chunk.h"
#include "ferrum/net/replication/interp/snapshot_interp.h"
#include "ferrum/net/stream.h"
#include "ferrum/net/udp_socket.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/prediction.h"
#include "ferrum/physics/snapshot.h"
#include "ferrum/physics/world.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"
#include "ferrum/renderer/video_capture.h"

#ifdef FR_NET_EMULATION
#include "ferrum/engine_settings.h"
#include "ferrum/net/emulation/net_emulator.h"
#endif

/* ── Constants ──────────────────────────────────────────────────── */

#define CLIENT_MAX_BODIES   1024u
#define CLIENT_WIN_W        960
#define CLIENT_WIN_H        540
#define CLIENT_MOVE_SPEED   5.0f
#define CLIENT_MOUSE_SENS   0.002f

/** Maximum snapshot wire size the client can reassemble. */
#define CLIENT_SNAPSHOT_BUF_SIZE (12u + CLIENT_MAX_BODIES * 26u)

/* ── Globals for signal handling ────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── Time helpers ───────────────────────────────────────────────── */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t now_ms_val(void) {
    return now_ns() / 1000000ull;
}

static double now_s(void) {
    return (double)now_ns() / 1000000000.0;
}

/* ── IPv4 parser ────────────────────────────────────────────────── */

static int parse_ipv4(const char *s, uint8_t out[4]) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!s || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    if (a > 255u || b > 255u || c > 255u || d > 255u) {
        return 0;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return 1;
}

/* ── Color from body ID ─────────────────────────────────────────── */

static void color_from_body(uint16_t body_id, float out_rgb[3]) {
    const uint32_t h = (uint32_t)body_id * 2654435761u;
    out_rgb[0] = 0.3f + 0.7f * (float)((h >> 0u) & 0xFFu) / 255.0f;
    out_rgb[1] = 0.3f + 0.7f * (float)((h >> 8u) & 0xFFu) / 255.0f;
    out_rgb[2] = 0.3f + 0.7f * (float)((h >> 16u) & 0xFFu) / 255.0f;
}

/* ── Per-body render metadata ───────────────────────────────────── */

/**
 * @brief Render metadata for each body, indexed by body pool slot.
 *
 * The physics state (pos, rot, vel) lives in the phys_world body pool.
 * This struct holds only the rendering-specific data.
 */
typedef struct body_render_info {
    uint8_t  shape_type;    /**< 0=box, 1=sphere, 2=capsule, 3=mesh, 4=halfspace, 5=custom. */
    uint8_t  is_static;     /**< 1 if static/kinematic body. */
    uint8_t  is_constrained; /**< 1 if body has joints → use interpolation. */
    uint8_t  is_hidden;     /**< 1 if body is invisible (trigger volume). */
    float    half_x;        /**< Half-extent X in meters. */
    float    half_y;        /**< Half-extent Y in meters. */
    float    half_z;        /**< Half-extent Z in meters. */
    float    color[3];      /**< RGB color [0..1]. */
    /* Per-body custom mesh (shape_type == 5). */
    GLuint   custom_vbo;    /**< VBO for custom mesh data, 0 if none. */
    GLuint   custom_vao;    /**< VAO for custom mesh data, 0 if none. */
    uint32_t custom_vert_count; /**< Vertex count for custom mesh. */
} body_render_info_t;

/* ── GL context ─────────────────────────────────────────────────── */

typedef struct gl_ctx {
    SDL_Window      *window;
    SDL_GLContext     gl;
    gl_loader_t       loader;
    shader_program_t  program;
    shader_uniform_cache_t uniforms;
    vbo_t             vbo;       /* cube mesh */
    vao_t             vao;
    vbo_t             cap_vbo;   /* capsule mesh */
    vao_t             cap_vao;
    uint32_t          cap_vert_count;
    shader_program_t  cap_program;  /* capsule-specific shader */
    GLint             cap_u_mvp;
    GLint             cap_u_color;
    GLint             cap_u_radius;
    GLint             cap_u_half_h;
    vbo_t             sph_vbo;   /* sphere mesh */
    vao_t             sph_vao;
    uint32_t          sph_vert_count;
    vbo_t             arm_vbo;   /* armadillo mesh */
    vao_t             arm_vao;
    uint32_t          arm_vert_count;
    vbo_t             plane_vbo;  /* ground plane quad */
    vao_t             plane_vao;
} gl_ctx_t;

static void *sdl_get_proc(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}

static mat4_t mat4_from_quat(quat_t q) {
    mat4_t out;
    if (quat_to_mat4(q, &out) != 0) {
        return mat4_identity();
    }
    return out;
}

/* ── Capsule mesh generator ──────────────────────────────────────── */

#define CAP_SLICES 12
#define CAP_HEMI_RINGS 4

static uint32_t gen_capsule_mesh(float *out, uint32_t max_verts)
{
    /* Unit capsule: radius = 1.0, half_height = 1.0.
     * Cylinder body spans Y = [-1, 1], hemisphere caps at each end.
     * Total height = 4.0 (2*half_h + 2*r).
     * The vertex shader rescales using u_radius and u_half_height uniforms
     * to produce the correct capsule dimensions. */
    uint32_t v = 0;
    const float r = 1.0f;
    const float half_h = 1.0f;

#define EMIT(px,py,pz) do { \
    if (v >= max_verts) return 0; \
    out[v*3+0]=(px); out[v*3+1]=(py); out[v*3+2]=(pz); v++; \
} while(0)

    for (int i = 0; i < CAP_SLICES; i++) {
        float a0 = (float)i / CAP_SLICES * 2.0f * FERRUM_PI;
        float a1 = (float)(i+1) / CAP_SLICES * 2.0f * FERRUM_PI;
        float c0 = cosf(a0)*r, s0 = sinf(a0)*r;
        float c1 = cosf(a1)*r, s1 = sinf(a1)*r;
        EMIT(c0, -half_h, s0); EMIT(c1, -half_h, s1); EMIT(c1,  half_h, s1);
        EMIT(c0, -half_h, s0); EMIT(c1,  half_h, s1); EMIT(c0,  half_h, s0);
    }

    for (int ring = 0; ring < CAP_HEMI_RINGS; ring++) {
        float phi0 = (float)ring / CAP_HEMI_RINGS * (FERRUM_PI / 2.0f);
        float phi1 = (float)(ring+1) / CAP_HEMI_RINGS * (FERRUM_PI / 2.0f);
        float y0 = half_h + sinf(phi0)*r, r0 = cosf(phi0)*r;
        float y1 = half_h + sinf(phi1)*r, r1 = cosf(phi1)*r;
        for (int i = 0; i < CAP_SLICES; i++) {
            float a0 = (float)i / CAP_SLICES * 2.0f * FERRUM_PI;
            float a1 = (float)(i+1) / CAP_SLICES * 2.0f * FERRUM_PI;
            float x00=cosf(a0)*r0, z00=sinf(a0)*r0;
            float x10=cosf(a1)*r0, z10=sinf(a1)*r0;
            float x01=cosf(a0)*r1, z01=sinf(a0)*r1;
            float x11=cosf(a1)*r1, z11=sinf(a1)*r1;
            EMIT(x00,y0,z00); EMIT(x10,y0,z10); EMIT(x11,y1,z11);
            EMIT(x00,y0,z00); EMIT(x11,y1,z11); EMIT(x01,y1,z01);
        }
    }

    for (int ring = 0; ring < CAP_HEMI_RINGS; ring++) {
        float phi0 = (float)ring / CAP_HEMI_RINGS * (FERRUM_PI / 2.0f);
        float phi1 = (float)(ring+1) / CAP_HEMI_RINGS * (FERRUM_PI / 2.0f);
        float y0 = -half_h - sinf(phi0)*r, r0 = cosf(phi0)*r;
        float y1 = -half_h - sinf(phi1)*r, r1 = cosf(phi1)*r;
        for (int i = 0; i < CAP_SLICES; i++) {
            float a0 = (float)i / CAP_SLICES * 2.0f * FERRUM_PI;
            float a1 = (float)(i+1) / CAP_SLICES * 2.0f * FERRUM_PI;
            float x00=cosf(a0)*r0, z00=sinf(a0)*r0;
            float x10=cosf(a1)*r0, z10=sinf(a1)*r0;
            float x01=cosf(a0)*r1, z01=sinf(a0)*r1;
            float x11=cosf(a1)*r1, z11=sinf(a1)*r1;
            EMIT(x00,y0,z00); EMIT(x11,y1,z11); EMIT(x10,y0,z10);
            EMIT(x00,y0,z00); EMIT(x01,y1,z01); EMIT(x11,y1,z11);
        }
    }

#undef EMIT
    return v;
}

/* ── Sphere mesh generator (unit UV sphere) ─────────────────────── */

#define SPH_RINGS  12
#define SPH_SLICES 16
#define SPH_MAX_VERTS (SPH_RINGS * SPH_SLICES * 6)

static uint32_t gen_sphere_mesh(float *out, uint32_t max_verts)
{
    uint32_t v = 0;
    const float r = 0.5f;

#define EMIT_S(px,py,pz) do { \
    if (v >= max_verts) return 0; \
    out[v*3+0]=(px); out[v*3+1]=(py); out[v*3+2]=(pz); v++; \
} while(0)

    for (int ring = 0; ring < SPH_RINGS; ring++) {
        float phi0 = (float)ring / SPH_RINGS * FERRUM_PI;
        float phi1 = (float)(ring + 1) / SPH_RINGS * FERRUM_PI;
        float y0 = cosf(phi0) * r, r0 = sinf(phi0) * r;
        float y1 = cosf(phi1) * r, r1 = sinf(phi1) * r;
        for (int sl = 0; sl < SPH_SLICES; sl++) {
            float a0 = (float)sl / SPH_SLICES * 2.0f * FERRUM_PI;
            float a1 = (float)(sl + 1) / SPH_SLICES * 2.0f * FERRUM_PI;
            float x00 = cosf(a0) * r0, z00 = sinf(a0) * r0;
            float x10 = cosf(a1) * r0, z10 = sinf(a1) * r0;
            float x01 = cosf(a0) * r1, z01 = sinf(a0) * r1;
            float x11 = cosf(a1) * r1, z11 = sinf(a1) * r1;
            EMIT_S(x00, y0, z00); EMIT_S(x10, y0, z10); EMIT_S(x11, y1, z11);
            EMIT_S(x00, y0, z00); EMIT_S(x11, y1, z11); EMIT_S(x01, y1, z01);
        }
    }

#undef EMIT_S
    return v;
}

/* ── Build GL VAO from FVMA data ────────────────────────────────── */

/**
 * @brief Deserialize FVMA blob and upload to a per-body GL VBO/VAO.
 *
 * Extracts positions from the FVMA, expands indexed triangles into
 * a flat vertex array (6 floats per vertex: pos + normal), and uploads
 * to new GL buffers.
 *
 * @param fvma_data  Raw FVMA bytes.
 * @param fvma_size  Size in bytes.
 * @param out_vbo    Receives GL VBO name.
 * @param out_vao    Receives GL VAO name.
 * @param out_verts  Receives vertex count (for glDrawArrays).
 * @return true on success.
 */
static bool build_vao_from_fvma(const uint8_t *fvma_data, uint32_t fvma_size,
                                 GLuint *out_vbo, GLuint *out_vao,
                                 uint32_t *out_verts) {
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    if (!mesh_vao_deserialize(fvma_data, (size_t)fvma_size, &slot)) {
        fprintf(stderr, "[client] FVMA deserialize failed\n");
        return false;
    }
    if (slot.index_count == 0 || !slot.positions) {
        mesh_slot_destroy(&slot);
        return false;
    }

    /* Expand indexed triangles into flat array: 6 floats per vertex
     * (px, py, pz, nx, ny, nz). */
    uint32_t tri_verts = slot.index_count; /* one index per vertex */
    float *buf = (float *)malloc(tri_verts * 6 * sizeof(float));
    if (!buf) {
        mesh_slot_destroy(&slot);
        return false;
    }
    for (uint32_t i = 0; i < tri_verts; i++) {
        uint32_t vi = slot.indices[i];
        buf[i * 6 + 0] = slot.positions[vi * 3 + 0];
        buf[i * 6 + 1] = slot.positions[vi * 3 + 1];
        buf[i * 6 + 2] = slot.positions[vi * 3 + 2];
        if (slot.normals) {
            buf[i * 6 + 3] = slot.normals[vi * 3 + 0];
            buf[i * 6 + 4] = slot.normals[vi * 3 + 1];
            buf[i * 6 + 5] = slot.normals[vi * 3 + 2];
        } else {
            buf[i * 6 + 3] = 0.0f;
            buf[i * 6 + 4] = 1.0f;
            buf[i * 6 + 5] = 0.0f;
        }
    }
    mesh_slot_destroy(&slot);

    GLuint vbo, vao;
    glGenBuffers(1, &vbo);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(tri_verts * 6 * sizeof(float)),
                 buf, GL_STATIC_DRAW);
    /* position: location 0, 3 floats, stride 24, offset 0 */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    /* normal: location 1, 3 floats, stride 24, offset 12 */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    glBindVertexArray(0);

    free(buf);
    *out_vbo = vbo;
    *out_vao = vao;
    *out_verts = tri_verts;
    return true;
}

/* ── GL init / shutdown ─────────────────────────────────────────── */

static int gl_init(gl_ctx_t *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    ctx->window = SDL_CreateWindow("demo_client",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   CLIENT_WIN_W, CLIENT_WIN_H,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!ctx->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    ctx->gl = SDL_GL_CreateContext(ctx->window);
    if (!ctx->gl) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    SDL_GL_MakeCurrent(ctx->window, ctx->gl);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    ctx->loader.get_proc_address = sdl_get_proc;
    ctx->loader.user_data = NULL;

    const char *vs =
        "#version 330 core\n"
        "in vec3 in_pos;\n"
        "uniform mat4 u_mvp;\n"
        "out vec3 v_world_pos;\n"
        "void main() {\n"
        "  vec4 wp = vec4(in_pos, 1.0);\n"
        "  v_world_pos = wp.xyz;\n"
        "  gl_Position = u_mvp * wp;\n"
        "}\n";
    const char *fs =
        "#version 330 core\n"
        "uniform vec3 u_color;\n"
        "in vec3 v_world_pos;\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "  vec3 light_dir = normalize(vec3(0.5, 1.0, 0.3));\n"
        "  vec3 dx = dFdx(v_world_pos);\n"
        "  vec3 dy = dFdy(v_world_pos);\n"
        "  vec3 n = normalize(cross(dx, dy));\n"
        "  float ndotl = abs(dot(n, light_dir));\n"
        "  float ambient = 0.3;\n"
        "  float diff = ambient + ndotl * 0.7;\n"
        "  out_color = vec4(u_color * diff, 1.0);\n"
        "}\n";

    char log_buf[1024] = {0};
    if (shader_program_create(&ctx->program, &ctx->loader, vs, fs,
                              log_buf, sizeof(log_buf)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader error: %s\n", log_buf);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    shader_uniform_cache_init(&ctx->uniforms, &ctx->program);

    /* ── Capsule shader: rescales unit capsule mesh per-body ───────── */
    const char *cap_vs =
        "#version 330 core\n"
        "in vec3 in_pos;\n"
        "uniform mat4 u_mvp;\n"
        "uniform float u_radius;\n"
        "uniform float u_half_h;\n"
        "void main() {\n"
        "    /* Unit mesh: r=1, half_h=1.  Cylinder spans Y=[-1,1],\n"
        "     * hemisphere caps extend beyond.  Decompose vertex into\n"
        "     * cylinder vs hemisphere part and scale independently. */\n"
        "    float y = in_pos.y;\n"
        "    vec3 p;\n"
        "    p.x = in_pos.x * u_radius;\n"
        "    p.z = in_pos.z * u_radius;\n"
        "    if (y >= 1.0) {\n"
        "        /* Top hemisphere: offset from cylinder top. */\n"
        "        float dy = y - 1.0;\n"
        "        float xz_scale = length(vec2(in_pos.x, in_pos.z));\n"
        "        if (xz_scale > 0.001) {\n"
        "            p.x = in_pos.x / xz_scale * sqrt(max(0.0, u_radius*u_radius - (dy*u_radius)*(dy*u_radius)));\n"
        "            p.z = in_pos.z / xz_scale * sqrt(max(0.0, u_radius*u_radius - (dy*u_radius)*(dy*u_radius)));\n"
        "        }\n"
        "        p.y = u_half_h + dy * u_radius;\n"
        "    } else if (y <= -1.0) {\n"
        "        /* Bottom hemisphere. */\n"
        "        float dy = y + 1.0;\n"
        "        float xz_scale = length(vec2(in_pos.x, in_pos.z));\n"
        "        if (xz_scale > 0.001) {\n"
        "            p.x = in_pos.x / xz_scale * sqrt(max(0.0, u_radius*u_radius - (dy*u_radius)*(dy*u_radius)));\n"
        "            p.z = in_pos.z / xz_scale * sqrt(max(0.0, u_radius*u_radius - (dy*u_radius)*(dy*u_radius)));\n"
        "        }\n"
        "        p.y = -u_half_h + dy * u_radius;\n"
        "    } else {\n"
        "        /* Cylinder body: scale Y by half_height. */\n"
        "        p.y = y * u_half_h;\n"
        "    }\n"
        "    gl_Position = u_mvp * vec4(p, 1.0);\n"
        "}\n";

    char cap_log[1024] = {0};
    if (shader_program_create(&ctx->cap_program, &ctx->loader, cap_vs, fs,
                              cap_log, sizeof(cap_log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "capsule shader error: %s\n", cap_log);
    }
    ctx->cap_u_mvp    = glGetUniformLocation(ctx->cap_program.handle, "u_mvp");
    ctx->cap_u_color  = glGetUniformLocation(ctx->cap_program.handle, "u_color");
    ctx->cap_u_radius = glGetUniformLocation(ctx->cap_program.handle, "u_radius");
    ctx->cap_u_half_h = glGetUniformLocation(ctx->cap_program.handle, "u_half_h");

    /* Cube VBO + VAO */
    static const float cube_verts[] = {
         0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
         0.5f,-0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f,
        -0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
        -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
        -0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f,
         0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
         0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
    };

    vbo_create(&ctx->vbo, &ctx->loader);
    vbo_upload(&ctx->vbo, GL_ARRAY_BUFFER, cube_verts, sizeof(cube_verts),
               GL_STATIC_DRAW);
    vao_create(&ctx->vao, &ctx->loader);

    GLint pos_loc = glGetAttribLocation(
        (GLuint)shader_program_handle(&ctx->program), "in_pos");
    vao_attribute_t attr = {(uint32_t)pos_loc, 3, GL_FLOAT, 0u, 0u, 0u};
    vao_bind_attributes(&ctx->vao, &ctx->vbo, &attr, 1u, 3u * sizeof(float));

    /* Capsule mesh VBO + VAO. */
    {
        #define CAP_MAX_VERTS 4096
        float cap_verts[CAP_MAX_VERTS * 3];
        ctx->cap_vert_count = gen_capsule_mesh(cap_verts, CAP_MAX_VERTS);
        #undef CAP_MAX_VERTS

        vbo_create(&ctx->cap_vbo, &ctx->loader);
        vbo_upload(&ctx->cap_vbo, GL_ARRAY_BUFFER, cap_verts,
                   ctx->cap_vert_count * 3 * sizeof(float), GL_STATIC_DRAW);
        vao_create(&ctx->cap_vao, &ctx->loader);
        GLint cap_pos_loc = glGetAttribLocation(
            ctx->cap_program.handle, "in_pos");
        vao_attribute_t cap_attr = {(uint32_t)cap_pos_loc, 3, GL_FLOAT,
                                    0u, 0u, 0u};
        vao_bind_attributes(&ctx->cap_vao, &ctx->cap_vbo, &cap_attr, 1u,
                            3u * sizeof(float));
    }

    /* Sphere mesh VBO + VAO. */
    {
        float sph_verts[SPH_MAX_VERTS * 3];
        ctx->sph_vert_count = gen_sphere_mesh(sph_verts, SPH_MAX_VERTS);

        vbo_create(&ctx->sph_vbo, &ctx->loader);
        vbo_upload(&ctx->sph_vbo, GL_ARRAY_BUFFER, sph_verts,
                   ctx->sph_vert_count * 3 * sizeof(float), GL_STATIC_DRAW);
        vao_create(&ctx->sph_vao, &ctx->loader);
        vao_bind_attributes(&ctx->sph_vao, &ctx->sph_vbo, &attr, 1u,
                            3u * sizeof(float));
    }

    /* Armadillo or Procgen mesh VBO + VAO — placeholder, loaded after arg parse. */
    {
        /* Will be filled in after argument parsing. */
    }

    /* Ground plane quad VBO + VAO. */
    {
        static const float plane_verts[] = {
            -0.5f, 0.0f, -0.5f,
             0.5f, 0.0f, -0.5f,
             0.5f, 0.0f,  0.5f,
            -0.5f, 0.0f, -0.5f,
             0.5f, 0.0f,  0.5f,
            -0.5f, 0.0f,  0.5f,
        };
        vbo_create(&ctx->plane_vbo, &ctx->loader);
        vbo_upload(&ctx->plane_vbo, GL_ARRAY_BUFFER, plane_verts,
                   sizeof(plane_verts), GL_STATIC_DRAW);
        vao_create(&ctx->plane_vao, &ctx->loader);
        vao_bind_attributes(&ctx->plane_vao, &ctx->plane_vbo, &attr, 1u,
                            3u * sizeof(float));
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    SDL_GL_SetSwapInterval(0); /* No vsync — render as fast as possible. */
    SDL_SetRelativeMouseMode(SDL_TRUE);

    return 0;
}

static void gl_shutdown(gl_ctx_t *ctx) {
    vao_destroy(&ctx->plane_vao);
    vbo_destroy(&ctx->plane_vbo);
    if (ctx->arm_vert_count > 0) {
        vao_destroy(&ctx->arm_vao);
        vbo_destroy(&ctx->arm_vbo);
    }
    vao_destroy(&ctx->cap_vao);
    vbo_destroy(&ctx->cap_vbo);
    vao_destroy(&ctx->vao);
    vbo_destroy(&ctx->vbo);
    shader_program_destroy(&ctx->program);
    shader_program_destroy(&ctx->cap_program);
    if (ctx->gl) {
        SDL_GL_DeleteContext(ctx->gl);
    }
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
    }
    SDL_Quit();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <server_ip> <port> [duration_s] [--headless]"
                " [--record FILE]"
#ifdef FR_NET_EMULATION
                " [--emu-delay MS] [--emu-jitter MS] [--emu-loss PCT]"
                " [--emu-reorder PCT] [--emu-duplicate PCT]"
                " [--emu-dist uniform|normal|lognormal]"
#endif
                "\n", argv[0]);
        return 1;
    }

    const char *server_ip_str = argv[1];
    uint16_t port = (uint16_t)atoi(argv[2]);
    double duration = (argc >= 4 && strcmp(argv[3], "--headless") != 0)
                          ? atof(argv[3]) : 0.0;
    int headless = 0;
    const char *record_path = NULL;
    const char *procgen_path = NULL;
    int         use_srd = 0;
    double      srd_budget = 30.0;
    procgen_srd_level_t srd_level;  /* persistent — no free until end */
    int srd_loaded = 0;

#ifdef FR_NET_EMULATION
    float emu_delay = 0.0f, emu_jitter = 0.0f, emu_loss = 0.0f;
    float emu_reorder = 0.0f, emu_duplicate = 0.0f;
    net_emu_distribution_t emu_dist = NET_EMU_DIST_UNIFORM;
#endif

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            record_path = argv[++i];
        } else if (strcmp(argv[i], "--procgen") == 0 && i + 1 < argc) {
            procgen_path = argv[++i];
        } else if (strcmp(argv[i], "--srd") == 0 && i + 1 < argc) {
            procgen_path = argv[++i];
            use_srd = 1;
        } else if (strcmp(argv[i], "--srd-budget") == 0 && i + 1 < argc) {
            srd_budget = atof(argv[++i]);
        }
#ifdef FR_NET_EMULATION
        else if (strcmp(argv[i], "--emu-delay") == 0 && i + 1 < argc) {
            emu_delay = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-jitter") == 0 && i + 1 < argc) {
            emu_jitter = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-loss") == 0 && i + 1 < argc) {
            emu_loss = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-reorder") == 0 && i + 1 < argc) {
            emu_reorder = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-duplicate") == 0 && i + 1 < argc) {
            emu_duplicate = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-dist") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "normal") == 0)        emu_dist = NET_EMU_DIST_NORMAL;
            else if (strcmp(argv[i], "lognormal") == 0) emu_dist = NET_EMU_DIST_LOG_NORMAL;
            else                                        emu_dist = NET_EMU_DIST_UNIFORM;
        }
#endif
    }

#ifdef FR_NET_EMULATION
    fr_engine_settings_init();
    {
        fr_engine_settings_t *s = fr_engine_settings_mut();
        int has_emu = (emu_delay > 0.0f || emu_jitter > 0.0f ||
                       emu_loss > 0.0f || emu_reorder > 0.0f ||
                       emu_duplicate > 0.0f);
        s->net_emu_enabled = has_emu ? 1 : 0;
        s->net_emu.delay_ms      = emu_delay;
        s->net_emu.jitter_ms     = emu_jitter;
        s->net_emu.loss_pct      = emu_loss;
        s->net_emu.reorder_pct   = emu_reorder;
        s->net_emu.duplicate_pct = emu_duplicate;
        s->net_emu.distribution  = emu_dist;
        if (has_emu) {
            printf("[client] net emulation: delay=%.1fms jitter=%.1fms "
                   "loss=%.1f%% reorder=%.1f%% dup=%.1f%% dist=%d\n",
                   emu_delay, emu_jitter, emu_loss,
                   emu_reorder, emu_duplicate, (int)emu_dist);
        }
    }
    fr_engine_settings_freeze();
#endif

    signal(SIGINT, handle_sigint);

    uint8_t ip[4];
    if (!parse_ipv4(server_ip_str, ip)) {
        fprintf(stderr, "Invalid IPv4: %s\n", server_ip_str);
        return 1;
    }

    /* ── 1. UDP socket ─────────────────────────────────────────── */
    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "error: socket open failed\n");
        return 1;
    }
    net_udp_socket_set_recv_buffer_bytes(&sock, 4u * 1024u * 1024u);
    net_udp_socket_set_send_buffer_bytes(&sock, 4u * 1024u * 1024u);
    net_udp_socket_set_nonblocking(&sock, 1);

    net_udp_addr_t server_addr;
    net_udp_addr_ipv4(&server_addr, ip[0], ip[1], ip[2], ip[3], port);
    if (net_udp_socket_connect(&sock, &server_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "error: connect failed\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    printf("[client] connected to %s:%u\n", server_ip_str, port);
    net_udp_socket_set_nonblocking(&sock, 1);

    /* ── 2. RUDP peer + send JOIN ──────────────────────────────── */
    net_rudp_peer_t peer;
    const size_t send_slots_count = NET_RUDP_SEND_SLOTS_DEFAULT;
    const size_t send_slots_bytes = net_rudp_send_slot_storage_size(
        send_slots_count);
    net_rudp_send_slot_t *send_slots = calloc(1u, send_slots_bytes);
    if (!send_slots) {
        fprintf(stderr, "error: send slot alloc\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u,
                                    send_slots, send_slots_count);

    /* Reliable stream reassembly for STREAM_FRAME messages. */
    fr_rudp_stream_config_t stream_cfg = {0};
    stream_cfg.reliable_channels = 1u;
    stream_cfg.reliable_slot_count = 512u;
    stream_cfg.max_payload_size = 1400u;
    fr_rudp_stream_t *rx_stream = fr_rudp_stream_create(&stream_cfg);
    if (!rx_stream) {
        fprintf(stderr, "error: stream create failed\n");
        free(send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }

    net_repl_join_t join_msg;
    join_msg.client_nonce = (uint32_t)(now_ms_val() ^ (uint32_t)getpid());
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    net_repl_join_encode(&join_msg, join_payload, sizeof(join_payload));

    uint16_t join_seq = 0;
    net_rudp_peer_send_reliable(&peer, &sock, &server_addr, now_ms_val(),
                                NET_REPL_SCHEMA_JOIN, join_payload,
                                sizeof(join_payload), &join_seq);
    printf("[client] JOIN sent (nonce=%u)\n", join_msg.client_nonce);

    /* ── 3. GL context (unless headless) ───────────────────────── */
    gl_ctx_t gl;
    memset(&gl, 0, sizeof(gl));
    if (!headless) {
        if (gl_init(&gl) != 0) {
            free(send_slots);
            net_udp_socket_close(&sock);
            return 1;
        }

        /* ── Load procgen mesh if --procgen/--srd specified ────────── */
        if (procgen_path) {
            if (use_srd) {
                procgen_srd_level_init(&srd_level);
                if (procgen_srd_level_load(&srd_level, procgen_path, 0, srd_budget) == 0
                    && srd_level.mesh.vertex_count > 0) {
                    srd_loaded = 1;
                    uint32_t tris = srd_level.mesh.vertex_count / 9;
                    GLint ploc = glGetAttribLocation(gl.program.handle, "in_pos");
                    vao_attribute_t pattr = {(uint32_t)ploc, 3, GL_FLOAT, 0u, 0u, 0u};
                    gl.arm_vert_count = tris * 3;
                    float mn[3]={1e9f,1e9f,1e9f}, mx[3]={-1e9f,-1e9f,-1e9f};
                    for(uint32_t i=0;i<srd_level.mesh.vertex_count;i+=3){
                        float vv=srd_level.mesh.vertices[i];if(vv<mn[0])mn[0]=vv;if(vv>mx[0])mx[0]=vv;
                        vv=srd_level.mesh.vertices[i+1];if(vv<mn[1])mn[1]=vv;if(vv>mx[1])mx[1]=vv;
                        vv=srd_level.mesh.vertices[i+2];if(vv<mn[2])mn[2]=vv;if(vv>mx[2])mx[2]=vv;}
                    printf("[client] mesh bounds: (%.1f,%.1f,%.1f)→(%.1f,%.1f,%.1f)\n",
                           mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]);
                    vbo_create(&gl.arm_vbo, &gl.loader);
                    vbo_upload(&gl.arm_vbo, GL_ARRAY_BUFFER,
                               srd_level.mesh.vertices,
                               srd_level.mesh.vertex_count * sizeof(float),
                               GL_STATIC_DRAW);
                    vao_create(&gl.arm_vao, &gl.loader);
                    vao_bind_attributes(&gl.arm_vao, &gl.arm_vbo,
                                        &pattr, 1u, 3u * sizeof(float));
                    printf("[client] srd mesh: %u tris from %s\n", tris, procgen_path);
                } else {
                    procgen_srd_level_free(&srd_level);
                }
            } else {
                procgen_level_t lvl;
                procgen_level_init(&lvl);
            if (procgen_level_load(&lvl, procgen_path) == 0) {
                uint32_t tris = lvl.mesh.vertex_count / 9;
                if (tris > 0) {
                    GLint ploc = glGetAttribLocation(gl.program.handle, "in_pos");
                    vao_attribute_t pattr = {(uint32_t)ploc, 3, GL_FLOAT, 0u, 0u, 0u};
                    gl.arm_vert_count = tris * 3;
                    /* Debug: compute mesh bounding box */
                    float mn[3]={1e9f,1e9f,1e9f}, mx[3]={-1e9f,-1e9f,-1e9f};
                    for(uint32_t i=0;i<lvl.mesh.vertex_count;i+=3){
                        float vv=lvl.mesh.vertices[i];if(vv<mn[0])mn[0]=vv;if(vv>mx[0])mx[0]=vv;
                        vv=lvl.mesh.vertices[i+1];if(vv<mn[1])mn[1]=vv;if(vv>mx[1])mx[1]=vv;
                        vv=lvl.mesh.vertices[i+2];if(vv<mn[2])mn[2]=vv;if(vv>mx[2])mx[2]=vv;}
                    printf("[client] mesh bounds: (%.1f,%.1f,%.1f)→(%.1f,%.1f,%.1f)\n",
                           mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]);
                    vbo_create(&gl.arm_vbo, &gl.loader);
                    vbo_upload(&gl.arm_vbo, GL_ARRAY_BUFFER,
                               lvl.mesh.vertices,
                               lvl.mesh.vertex_count * sizeof(float),
                               GL_STATIC_DRAW);
                    vao_create(&gl.arm_vao, &gl.loader);
                    vao_bind_attributes(&gl.arm_vao, &gl.arm_vbo,
                                        &pattr, 1u, 3u * sizeof(float));
                    printf("[client] procgen mesh: %u tris from %s\n",
                           tris, procgen_path);
                }
                procgen_level_free(&lvl);
            } else {
                fprintf(stderr, "[client] procgen load failed: %s\n",
                        procgen_path);
            }
            }
        }
    }

    /* ── 3b. Video capture (if --record) ──────────────────────── */
    fr_video_capture_t *video_cap = NULL;
    if (record_path && !headless) {
        video_cap = fr_video_capture_create(&(fr_video_capture_desc_t){
            .width  = CLIENT_WIN_W,
            .height = CLIENT_WIN_H,
            .fps    = 30,
            .output_path = record_path,
        });
        if (video_cap) {
            printf("[client] recording to %s\n", record_path);
        } else {
            fprintf(stderr, "[client] warning: video capture init failed\n");
        }
    }

    /* ── 4. Local physics world (body storage + prediction) ──────── */
    phys_world_t world;
    {
        phys_world_config_t cfg = phys_world_config_default();
        cfg.max_bodies = CLIENT_MAX_BODIES;
        cfg.max_colliders = CLIENT_MAX_BODIES;
        cfg.frame_arena_size = 256u * 1024u;
        cfg.fixed_dt = 1.0f / 60.0f;
        cfg.gravity = (phys_vec3_t){0.0f, -9.81f, 0.0f};
        if (phys_world_init(&world, &cfg) != 0) {
            fprintf(stderr, "error: phys_world_init failed\n");
            return 1;
        }
        world.prediction_mode = 1;
    }

    /* Lightweight prediction integrator — fixed 60Hz, decoupled from
     * render frame rate.  Integrates position + orientation only
     * (no collision/constraints — those come from the server). */
    fr_prediction_tick_config_t ptick_cfg = {
        .fixed_dt   = 1.0f / 60.0f,
        .gravity    = {0.0f, -9.81f, 0.0f},
        .max_bodies = CLIENT_MAX_BODIES,
        .reconcile  = phys_prediction_config_default(),
    };
    fr_prediction_tick_t *pred_tick = fr_prediction_tick_create(&ptick_cfg);
    if (!pred_tick) {
        fprintf(stderr, "[client] failed to create prediction tick\n");
        return 1;
    }
    if (!fr_prediction_tick_start(pred_tick, &world.body_pool)) {
        fprintf(stderr, "[client] failed to start prediction thread\n");
        return 1;
    }

    /* Per-body render metadata. */
    body_render_info_t render_info[CLIENT_MAX_BODIES];
    memset(render_info, 0, sizeof(render_info));

    /* Mesh data reassembly table for custom meshes. */
    net_repl_mesh_reassembly_table_t mesh_reassembly;
    if (!net_repl_mesh_reassembly_init(&mesh_reassembly, 64)) {
        fprintf(stderr, "[client] mesh reassembly init failed\n");
        return 1;
    }

    /* Per-body last-seen server tick for BODY_STATE dedup. */
    uint16_t body_state_last_tick[CLIENT_MAX_BODIES];
    memset(body_state_last_tick, 0, sizeof(body_state_last_tick));

    /* Snapshot reassembly buffer. */
    uint8_t snap_reasm_buf[CLIENT_SNAPSHOT_BUF_SIZE];
    net_chunk_reassembly_t snap_reasm;
    net_chunk_reassembly_init(&snap_reasm, snap_reasm_buf,
                              sizeof(snap_reasm_buf));

    /* Pre-allocated decoded snapshot body array. */
    phys_snapshot_body_t snap_bodies[CLIENT_MAX_BODIES];
    phys_snapshot_t snap_decoded;
    snap_decoded.bodies = snap_bodies;

    /* Track highest server tick to reject stale snapshots. */
    uint64_t last_server_tick = 0;

    /* Snapshot interpolator for constrained bodies (joints/chains).
     * These bodies cannot be locally predicted because prediction_mode
     * skips the constraint solver.  Instead we lerp/slerp between the
     * two most recent server snapshot poses. */
    fr_snapshot_interp_config_t si_cfg = {
        .max_bodies    = CLIENT_MAX_BODIES,
        .quat_epsilon  = 1e-6f,
    };
    fr_snapshot_interp_t *snap_interp = fr_snapshot_interp_create(&si_cfg);
    if (!snap_interp) {
        fprintf(stderr, "[client] failed to create snapshot interpolator\n");
        return 1;
    }

    /* ── 5. FPS camera state ───────────────────────────────────── */
    float cam_yaw   = 0.0f;
    float cam_pitch = 0.0f;
    vec3_t cam_pos  = {0.0f, 5.0f, 20.0f};

    /* ── 6. Main loop ──────────────────────────────────────────── */
    const double start_time = now_s();
    uint64_t next_keepalive_ms = now_ms_val();
    uint64_t next_diag_ms = now_ms_val() + 2000u;
    uint64_t last_frame_ns = now_ns();
    uint32_t frame_count = 0;
    uint32_t diag_frame_start = 0;
    uint64_t diag_time_start_ms = now_ms_val();
    uint32_t snap_applied_count = 0;
    uint32_t dbg_pkts = 0;
    uint32_t dbg_stream_frames = 0;
    uint32_t dbg_snap_chunks = 0;
    uint8_t rx_buf[1500];

    printf("[client] entering main loop\n");

    while (g_running) {
        const uint64_t frame_ns = now_ns();
        const double dt_s = (double)(frame_ns - last_frame_ns) / 1e9;
        last_frame_ns = frame_ns;
        const uint64_t now_ms = frame_ns / 1000000ull;

        if (duration > 0.0 && (now_s() - start_time) >= duration) {
            break;
        }

        /* ── Stage 1: Poll SDL events ──────────────────────────── */
        int move_fwd = 0, move_back = 0, move_left = 0, move_right = 0;
        if (!headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    goto done;
                }
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) {
                    goto done;
                }
                if (ev.type == SDL_MOUSEMOTION) {
                    cam_yaw   -= (float)ev.motion.xrel * CLIENT_MOUSE_SENS;
                    cam_pitch -= (float)ev.motion.yrel * CLIENT_MOUSE_SENS;
                    if (cam_pitch >  1.4f) cam_pitch =  1.4f;
                    if (cam_pitch < -1.4f) cam_pitch = -1.4f;
                }
            }
            const uint8_t *keys = SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_W]) move_fwd  = 1;
            if (keys[SDL_SCANCODE_S]) move_back = 1;
            if (keys[SDL_SCANCODE_A]) move_left = 1;
            if (keys[SDL_SCANCODE_D]) move_right = 1;
        }

        uint64_t t_recv_start = now_ns();
        /* ── Stage 2: Network receive ──────────────────────────── */
        net_rudp_peer_tick_resend(&peer, &sock, &server_addr, now_ms);

        /* Send JOIN keepalive every 100ms. */
        if (now_ms >= next_keepalive_ms) {
            net_rudp_peer_send_unreliable(&peer, &sock, &server_addr,
                                          now_ms, NET_REPL_SCHEMA_JOIN,
                                          join_payload,
                                          sizeof(join_payload));
            next_keepalive_ms = now_ms + 100u;
        }

        /* Drain all pending UDP packets.  Stale unreliable data is
         * detected and skipped without expensive processing. */
        for (;;) {
            size_t rx_size = 0;
            int rc = net_udp_socket_recv(&sock, rx_buf, sizeof(rx_buf),
                                         &rx_size);
            if (rc != NET_UDP_SOCKET_OK) { break; }

            /* Raw snapshot chunks bypass RUDP — check schema prefix. */
            if (rx_size >= 14u) {
                uint16_t raw_schema = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8u));
                if (raw_schema == NET_REPL_SCHEMA_SNAPSHOT_CHUNK) {
                    dbg_snap_chunks++;
                    const uint8_t *chk = rx_buf + 2u;
                    size_t chk_len = rx_size - 2u;

                    net_chunk_header_t hdr;
                    hdr.chunk_index = (uint16_t)(chk[0] | (chk[1] << 8u));
                    hdr.chunk_total = (uint16_t)(chk[2] | (chk[3] << 8u));
                    memcpy(&hdr.offset, chk + 4, 4);
                    memcpy(&hdr.length, chk + 8, 4);

                    const uint8_t *chunk_data = chk + 12u;
                    uint32_t chunk_data_len = (uint32_t)(chk_len - 12u);
                    if (chunk_data_len < hdr.length) { continue; }

                    int crc = net_chunk_reassembly_push(&snap_reasm, &hdr,
                                                         chunk_data, hdr.length);
                    if (crc == NET_CHUNK_READY) {
                        if (phys_snapshot_decode(snap_reasm.buffer,
                                                 snap_reasm.total_size,
                                                 &snap_decoded) == 0) {
                            if (snap_decoded.tick > last_server_tick) {
                                last_server_tick = snap_decoded.tick;
                                /* Feed interpolator (constrained bodies). */
                                double rx_time = now_s();
                                fr_snapshot_interp_push(snap_interp,
                                                        &snap_decoded,
                                                        rx_time);
                                /* Write server-authoritative state into
                                 * bodies_net for prediction reconciliation.
                                 * The prediction thread will consume these
                                 * via atomic dirty flags. */
                                uint32_t sc = snap_decoded.body_count;
                                uint32_t wc = world.body_pool.capacity;
                                uint32_t n = (sc < wc) ? sc : wc;
                                for (uint32_t si = 0; si < n; si++) {
                                    if (!world.body_pool.active[si]) continue;
                                    const phys_snapshot_body_t *sb =
                                        &snap_decoded.bodies[si];
                                    phys_body_t net_body =
                                        world.body_pool.bodies_net[si];
                                    net_body.flags =
                                        (uint32_t)sb->flags;
                                    net_body.position =
                                        phys_dequantize_vec3(sb->position,
                                                             1.0f / 1000.0f);
                                    net_body.orientation =
                                        phys_dequantize_quat(sb->orientation);
                                    net_body.linear_vel =
                                        phys_dequantize_vec3(sb->linear_vel,
                                                             1.0f / 1000.0f);
                                    net_body.angular_vel =
                                        phys_dequantize_vec3(sb->angular_vel,
                                                             1.0f / 1000.0f);
                                    phys_body_pool_write_net(
                                        &world.body_pool, si, &net_body);
                                }
                                snap_applied_count++;
                            }
                        }
                        net_chunk_reassembly_reset(&snap_reasm);
                    }
                    continue;
                }

                /* Raw BODY_STATE priority updates for constrained bodies. */
                if (raw_schema == NET_REPL_SCHEMA_BODY_STATE &&
                    rx_size >= 2u + NET_REPL_BODY_STATE_PAYLOAD_SIZE) {
                    net_repl_body_state_t bst;
                    if (net_repl_body_state_decode(&bst, rx_buf + 2u,
                                                    rx_size - 2u)
                        == NET_REPL_OK) {
                        uint32_t bi = bst.body_id;
                        if (bi >= CLIENT_MAX_BODIES) { continue; }
                        if (!render_info[bi].is_constrained) { continue; }

                        /* Drop stale: tick wrapped-aware comparison. */
                        int16_t tick_diff = (int16_t)(bst.server_tick
                                            - body_state_last_tick[bi]);
                        if (tick_diff <= 0) { continue; }
                        body_state_last_tick[bi] = bst.server_tick;

                        vec3_t pos = {
                            (float)bst.pos_mm.x_mm / 1000.0f,
                            (float)bst.pos_mm.y_mm / 1000.0f,
                            (float)bst.pos_mm.z_mm / 1000.0f,
                        };
                        quat_t rot = {bst.rot_x, bst.rot_y,
                                      bst.rot_z, bst.rot_w};
                        vec3_t vel = {
                            (float)bst.vel_x_mm_s / 1000.0f,
                            (float)bst.vel_y_mm_s / 1000.0f,
                            (float)bst.vel_z_mm_s / 1000.0f,
                        };
                        vec3_t ang = {
                            (float)bst.ang_x_mrad_s / 1000.0f,
                            (float)bst.ang_y_mrad_s / 1000.0f,
                            (float)bst.ang_z_mrad_s / 1000.0f,
                        };
                        double rx_time = now_s();
                        fr_pose_interpolator_push(
                            &snap_interp->interps[bi],
                            rx_time, pos, rot, vel, ang, rx_time);
                    }
                    continue;
                }
            }

            /* RUDP packet — pass through peer for ACK/reliability. */
            uint8_t reliable = 0;
            uint16_t schema_id = 0;
            uint8_t payload[1472];
            size_t payload_size = 0;
            int rudp_rc = net_rudp_peer_receive(
                &peer, rx_buf, rx_size, now_ms,
                &reliable, &schema_id,
                payload, sizeof(payload), &payload_size);
            if (rudp_rc != NET_RUDP_OK) { continue; }
            dbg_pkts++;

            if (schema_id == NET_REPL_SCHEMA_STREAM_FRAME) {
                if (payload_size >= 4u) {
                    dbg_stream_frames++;
                    fr_rudp_stream_push_frame(rx_stream, payload,
                                              payload_size);
                }
            }
        }

        /* ── Stage 3: Process reliable stream (BODY_SPAWN) ─────── */
        {
            uint8_t rel_buf[2048];
            size_t rel_len = sizeof(rel_buf);
            while (fr_rudp_stream_pop(rx_stream, 0u, rel_buf, &rel_len)) {
                if (rel_len < 2u) {
                    rel_len = sizeof(rel_buf);
                    continue;
                }

                uint16_t rel_schema = (uint16_t)(rel_buf[0] | (rel_buf[1] << 8u));
                const uint8_t *rel_data = rel_buf + 2u;
                size_t rel_data_len = rel_len - 2u;

                if (rel_schema == NET_REPL_SCHEMA_BODY_SPAWN) {
                    net_repl_body_spawn_t sp;
                    if (net_repl_body_spawn_decode(&sp, rel_data,
                                                    rel_data_len) != NET_REPL_OK) {
                        rel_len = sizeof(rel_buf);
                        continue;
                    }

                    /* Create body in local physics world.  Body IDs arrive
                     * in order via the reliable stream, so create_body()
                     * allocates matching indices. */
                    uint32_t idx = phys_world_create_body(&world);
                    if (idx == UINT32_MAX) {
                        fprintf(stderr, "[client] body pool full\n");
                        rel_len = sizeof(rel_buf);
                        continue;
                    }

                    /* Set initial state from spawn message. */
                    phys_body_t *body = phys_world_get_body(&world, idx);
                    if (body) {
                        body->position.x = (float)sp.pos_mm.x_mm / 1000.0f;
                        body->position.y = (float)sp.pos_mm.y_mm / 1000.0f;
                        body->position.z = (float)sp.pos_mm.z_mm / 1000.0f;
                        body->orientation.x = sp.rot_x;
                        body->orientation.y = sp.rot_y;
                        body->orientation.z = sp.rot_z;
                        body->orientation.w = sp.rot_w;
                        /* Static bodies have infinite mass (inv_mass = 0). */
                        uint8_t is_static = (sp.flags & 0x01u) ? 1u : 0u;
                        if (is_static) {
                            body->inv_mass = 0.0f;
                        } else {
                            body->inv_mass = 1.0f;
                        }
                    }

                    /* Store render metadata. */
                    if (idx < CLIENT_MAX_BODIES) {
                        body_render_info_t *ri = &render_info[idx];
                        ri->shape_type    = sp.shape_type;
                        ri->is_static     = (sp.flags & 0x01u) ? 1u : 0u;
                        ri->is_constrained = (sp.flags & 0x04u) ? 1u : 0u;
                        ri->is_hidden     = (sp.flags & 0x08u) ? 1u : 0u;
                        ri->half_x = net_float16_to_float(sp.half_x_f16);
                        ri->half_y = net_float16_to_float(sp.half_y_f16);
                        ri->half_z = net_float16_to_float(sp.half_z_f16);
                        /* Decode RGB from color_seed (R8G8B8 packed). */
                        ri->color[0] = (float)((sp.color_seed >> 16u) & 0xFFu) / 255.0f;
                        ri->color[1] = (float)((sp.color_seed >>  8u) & 0xFFu) / 255.0f;
                        ri->color[2] = (float)((sp.color_seed >>  0u) & 0xFFu) / 255.0f;
                    }

                    printf("[client] spawn body %u shape=%u pos=(%.1f,%.1f,%.1f) half=(%.2f,%.2f,%.2f)\n",
                           idx, sp.shape_type,
                           body ? body->position.x : 0.0f,
                           body ? body->position.y : 0.0f,
                           body ? body->position.z : 0.0f,
                           render_info[idx].half_x,
                           render_info[idx].half_y,
                           render_info[idx].half_z);
                }

                /* ── Handle MESH_DATA chunks ─────────────────────── */
                if (rel_schema == NET_REPL_SCHEMA_MESH_DATA &&
                    rel_data_len >= NET_REPL_MESH_CHUNK_HEADER_SIZE) {
                    net_repl_mesh_chunk_t chunk;
                    if (net_repl_mesh_chunk_decode(&chunk, rel_data,
                                                   rel_data_len) == NET_REPL_OK) {
                        uint8_t *completed_data = NULL;
                        uint32_t completed_size = 0;
                        if (net_repl_mesh_reassembly_push(&mesh_reassembly,
                                                          &chunk,
                                                          &completed_data,
                                                          &completed_size)) {
                            /* Full mesh arrived — build VAO. */
                            uint16_t bi = chunk.body_id;
                            if (bi < CLIENT_MAX_BODIES && !headless) {
                                body_render_info_t *ri = &render_info[bi];
                                if (build_vao_from_fvma(completed_data,
                                                        completed_size,
                                                        &ri->custom_vbo,
                                                        &ri->custom_vao,
                                                        &ri->custom_vert_count)) {
                                    printf("[client] mesh VAO built for body %u (%u verts)\n",
                                           bi, ri->custom_vert_count);
                                }
                            }
                            free(completed_data);
                        }
                    }
                }

                rel_len = sizeof(rel_buf);
            }
        }

        /* ── Stage 4: FPS camera movement (6DOF noclip) ─────────── */
        {
            /* Forward vector follows pitch + yaw (true noclip). */
            float cos_p = cosf(cam_pitch);
            float sin_p = sinf(cam_pitch);
            float fwd_x = cos_p * (-sinf(cam_yaw));
            float fwd_y = sin_p;
            float fwd_z = cos_p * (-cosf(cam_yaw));
            /* Right vector stays horizontal (no roll). */
            float right_x =  cosf(cam_yaw);
            float right_z = -sinf(cam_yaw);
            float mx = 0.0f, my = 0.0f, mz = 0.0f;

            if (move_fwd)   { mx += fwd_x; my += fwd_y; mz += fwd_z; }
            if (move_back)  { mx -= fwd_x; my -= fwd_y; mz -= fwd_z; }
            if (move_left)  { mx -= right_x; mz -= right_z; }
            if (move_right) { mx += right_x; mz += right_z; }

            float len = sqrtf(mx * mx + my * my + mz * mz);
            if (len > 0.001f) {
                mx /= len;
                my /= len;
                mz /= len;
            }
            cam_pos.x += mx * CLIENT_MOVE_SPEED * (float)dt_s;
            cam_pos.y += my * CLIENT_MOVE_SPEED * (float)dt_s;
            cam_pos.z += mz * CLIENT_MOVE_SPEED * (float)dt_s;
        }

        /* ── Stage 5: Prediction runs on its own thread at 60Hz ── */
        uint64_t t_recv_end = now_ns();

        /* ── Stage 6: Diagnostics ──────────────────────────────── */
        if (now_ms >= next_diag_ms) {
            uint32_t body_count = phys_world_body_count(&world);
            uint32_t frames_in_interval = frame_count - diag_frame_start;
            uint64_t interval_ms = now_ms - diag_time_start_ms;
            float ms_per_frame = (frames_in_interval > 0)
                ? (float)interval_ms / (float)frames_in_interval : 0.0f;
            printf("[client] frame=%u ms/f=%.1f bodies=%u snaps=%u cam=(%.1f,%.1f,%.1f)\n",
                   frame_count, ms_per_frame, body_count, snap_applied_count,
                   cam_pos.x, cam_pos.y, cam_pos.z);
            diag_frame_start = frame_count;
            diag_time_start_ms = now_ms;
            next_diag_ms = now_ms + 2000u;
        }

        uint64_t t_render_start = now_ns();
        /* ── Stage 7: Render ───────────────────────────────────── */
        if (!headless) {
            glViewport(0, 0, CLIENT_WIN_W, CLIENT_WIN_H);
            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (shader_program_bind(&gl.program) == SHADER_PROGRAM_OK) {

                mat4_t proj;
                float aspect = (float)CLIENT_WIN_W / (float)CLIENT_WIN_H;
                mat4_perspective(70.0f * (FERRUM_PI / 180.0f), aspect,
                                 0.1f, 5000.0f, &proj);

                vec3_t look_dir = {
                    cosf(cam_pitch) * (-sinf(cam_yaw)),
                    sinf(cam_pitch),
                    cosf(cam_pitch) * (-cosf(cam_yaw)),
                };
                vec3_t target = {
                    cam_pos.x + look_dir.x,
                    cam_pos.y + look_dir.y,
                    cam_pos.z + look_dir.z,
                };
                mat4_t view;
                mat4_look_at(cam_pos, target,
                             (vec3_t){0.0f, 1.0f, 0.0f}, &view);
                mat4_t vp = mat4_mul(proj, view);

                /* ── Fix 2: Cache uniform locations once ──────── */
                GLint u_mvp_loc = glGetUniformLocation(
                    gl.program.handle, "u_mvp");
                GLint u_color_loc = glGetUniformLocation(
                    gl.program.handle, "u_color");

                /* ── Fix 4: Extract frustum planes from VP matrix.
                 * Each plane is (a,b,c,d) where ax+by+cz+d >= 0 is
                 * inside.  We normalize so that (a,b,c) is a unit
                 * vector, enabling direct sphere-vs-plane tests. */
                float frustum[6][4];
                {
                    const float *m = vp.m; /* column-major */
                    /* Left   */ frustum[0][0] = m[3]+m[0];  frustum[0][1] = m[7]+m[4];  frustum[0][2] = m[11]+m[8];  frustum[0][3] = m[15]+m[12];
                    /* Right  */ frustum[1][0] = m[3]-m[0];  frustum[1][1] = m[7]-m[4];  frustum[1][2] = m[11]-m[8];  frustum[1][3] = m[15]-m[12];
                    /* Bottom */ frustum[2][0] = m[3]+m[1];  frustum[2][1] = m[7]+m[5];  frustum[2][2] = m[11]+m[9];  frustum[2][3] = m[15]+m[13];
                    /* Top    */ frustum[3][0] = m[3]-m[1];  frustum[3][1] = m[7]-m[5];  frustum[3][2] = m[11]-m[9];  frustum[3][3] = m[15]-m[13];
                    /* Near   */ frustum[4][0] = m[3]+m[2];  frustum[4][1] = m[7]+m[6];  frustum[4][2] = m[11]+m[10]; frustum[4][3] = m[15]+m[14];
                    /* Far    */ frustum[5][0] = m[3]-m[2];  frustum[5][1] = m[7]-m[6];  frustum[5][2] = m[11]-m[10]; frustum[5][3] = m[15]-m[14];
                    for (int p = 0; p < 6; p++) {
                        float len = sqrtf(frustum[p][0]*frustum[p][0] +
                                          frustum[p][1]*frustum[p][1] +
                                          frustum[p][2]*frustum[p][2]);
                        if (len > 1e-8f) {
                            float inv = 1.0f / len;
                            frustum[p][0] *= inv;
                            frustum[p][1] *= inv;
                            frustum[p][2] *= inv;
                            frustum[p][3] *= inv;
                        }
                    }
                }

                /* ── Draw procgen static mesh at world origin ────── */
                if (gl.arm_vert_count > 0) {
                    mat4_t model = mat4_identity();
                    mat4_t mvp   = mat4_mul(vp, model);
                    glUniformMatrix4fv(u_mvp_loc, 1, GL_FALSE, mvp.m);
                    glUniform3f(u_color_loc, 0.85f, 0.80f, 0.72f);
                    glBindVertexArray(gl.arm_vao.handle);
                    glDisable(GL_CULL_FACE);
                    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)gl.arm_vert_count);
                    glEnable(GL_CULL_FACE);
                }

                /* ── Fix 3: Sort bodies by shape_type to minimise
                 *  VAO state changes.  Build index list, then sort
                 *  by shape_type with a simple insertion sort (stable,
                 *  ~5 shape types → nearly O(n)). ──────────────── */
                uint32_t draw_order[CLIENT_MAX_BODIES];
                uint32_t draw_count = 0;
                double render_time = now_s();
                uint32_t cap = world.body_pool.capacity;

                for (uint32_t i = 0; i < cap; i++) {
                    if (phys_body_pool_is_active(&world.body_pool, i) &&
                        !render_info[i].is_hidden) {
                        draw_order[draw_count++] = i;
                    }
                }

                /* Insertion sort by shape_type. */
                for (uint32_t si = 1; si < draw_count; si++) {
                    uint32_t key = draw_order[si];
                    uint8_t  key_st = render_info[key].shape_type;
                    uint32_t j = si;
                    while (j > 0 &&
                           render_info[draw_order[j - 1]].shape_type > key_st) {
                        draw_order[j] = draw_order[j - 1];
                        j--;
                    }
                    draw_order[j] = key;
                }

                /* ── Draw sorted bodies, binding each VAO only once
                 *  per shape group (fix 3 + fix 5). ────────────── */
                uint8_t cur_shape = UINT8_MAX; /* force initial bind */

                for (uint32_t di = 0; di < draw_count; di++) {
                    uint32_t i = draw_order[di];
                    const body_render_info_t *ri = &render_info[i];

                    vec3_t pos;
                    quat_t rot;

                    if (ri->is_constrained) {
                        if (!fr_snapshot_interp_sample(snap_interp, i,
                                                       render_time,
                                                       &pos, &rot)) {
                            const phys_body_t *body =
                                &world.body_pool.bodies_curr[i];
                            pos = (vec3_t){body->position.x,
                                           body->position.y,
                                           body->position.z};
                            rot = (quat_t){body->orientation.x,
                                           body->orientation.y,
                                           body->orientation.z,
                                           body->orientation.w};
                        }
                    } else {
                        const phys_body_t *body =
                            &world.body_pool.bodies_curr[i];
                        pos = (vec3_t){body->position.x,
                                       body->position.y,
                                       body->position.z};
                        rot = (quat_t){body->orientation.x,
                                       body->orientation.y,
                                       body->orientation.z,
                                       body->orientation.w};
                    }

                    /* ── Fix 4: Frustum cull (sphere test). ────── */
                    {
                        float hx = ri->half_x, hy = ri->half_y;
                        float hz = ri->half_z;
                        float radius = sqrtf(hx*hx + hy*hy + hz*hz);
                        int culled = 0;
                        for (int p = 0; p < 6; p++) {
                            float dist = frustum[p][0] * pos.x +
                                         frustum[p][1] * pos.y +
                                         frustum[p][2] * pos.z +
                                         frustum[p][3];
                            if (dist < -radius) { culled = 1; break; }
                        }
                        if (culled) { continue; }
                    }

                    mat4_t t = mat4_translation(pos.x, pos.y, pos.z);
                    mat4_t r = mat4_from_quat(rot);
                    mat4_t s;
                    if (ri->shape_type == 3) {
                        s = mat4_scaling(1.0f, 1.0f, 1.0f);
                    } else if (ri->shape_type == 4) {
                        s = mat4_scaling(ri->half_x * 2.0f,
                                         1.0f,
                                         ri->half_z * 2.0f);
                    } else if (ri->shape_type == 5) {
                        /* Custom mesh: geometry is already in world-units. */
                        s = mat4_scaling(1.0f, 1.0f, 1.0f);
                    } else {
                        s = mat4_scaling(ri->half_x * 2.0f,
                                         ri->half_y * 2.0f,
                                         ri->half_z * 2.0f);
                    }
                    mat4_t model = mat4_mul(t, mat4_mul(r, s));
                    mat4_t mvp = mat4_mul(vp, model);

                    /* Fix 2: Direct uniform upload (no cache lookup). */
                    glUniformMatrix4fv(u_mvp_loc, 1, GL_FALSE, mvp.m);

                    float rgb[3];
                    /* Use color from spawn message; fall back to hash if unset. */
                    if (ri->color[0] > 0.001f || ri->color[1] > 0.001f || ri->color[2] > 0.001f) {
                        rgb[0] = ri->color[0];
                        rgb[1] = ri->color[1];
                        rgb[2] = ri->color[2];
                    } else {
                        color_from_body((uint16_t)i, rgb);
                    }
                    glUniform3fv(u_color_loc, 1, rgb);

                    /* Fix 3+5: Bind VAO only on shape group change.
                     * Custom meshes (type 5) always rebind per-body. */
                    if (ri->shape_type == 5) {
                        if (ri->custom_vao != 0) {
                            glBindVertexArray(ri->custom_vao);
                        } else {
                            glBindVertexArray(gl.vao.handle);
                        }
                        cur_shape = 5;
                    } else if (ri->shape_type != cur_shape) {
                        cur_shape = ri->shape_type;
                        switch (cur_shape) {
                        case 4:
                            glBindVertexArray(gl.plane_vao.handle);
                            break;
                        case 3:
                            glBindVertexArray(gl.arm_vao.handle);
                            break;
                        case 2:
                            glBindVertexArray(gl.cap_vao.handle);
                            break;
                        case 1:
                            glBindVertexArray(gl.sph_vao.handle);
                            break;
                        default:
                            glBindVertexArray(gl.vao.handle);
                            break;
                        }
                    }

                    /* Issue draw call with correct vertex count. */
                    switch (ri->shape_type) {
                    case 5:
                        if (ri->custom_vert_count > 0) {
                            glDrawArrays(GL_TRIANGLES, 0,
                                         (GLsizei)ri->custom_vert_count);
                        }
                        break;
                    case 4:
                        glDrawArrays(GL_TRIANGLES, 0, 6);
                        break;
                    case 3:
                        if (gl.arm_vert_count > 0) {
                            glDrawArrays(GL_TRIANGLES, 0,
                                         (GLsizei)gl.arm_vert_count);
                        }
                        break;
                    case 2:
                        glDrawArrays(GL_TRIANGLES, 0,
                                     (GLsizei)gl.cap_vert_count);
                        break;
                    case 1:
                        glDrawArrays(GL_TRIANGLES, 0,
                                     (GLsizei)gl.sph_vert_count);
                        break;
                    default:
                        glDrawArrays(GL_TRIANGLES, 0, 36);
                        break;
                    }
                }

                glBindVertexArray(0);
            }

            if (video_cap) {
                fr_video_capture_submit_frame(video_cap);
            }

            SDL_GL_SwapWindow(gl.window);
            uint64_t t_render_end = now_ns();

            /* Accumulate per-stage times for diag (in µs). */
            static uint64_t acc_recv_us = 0, acc_render_us = 0;
            static uint32_t acc_frames = 0;
            acc_recv_us   += (t_recv_end - t_recv_start) / 1000u;
            acc_render_us += (t_render_end - t_render_start) / 1000u;
            acc_frames++;
            if (acc_frames >= 30) {
                /* printf("[client] avg recv=%.1fms render=%.1fms (%u frames)\n",
                       (float)acc_recv_us / (float)acc_frames / 1000.0f,
                       (float)acc_render_us / (float)acc_frames / 1000.0f,
                       acc_frames); */
                acc_recv_us = acc_render_us = 0;
                acc_frames = 0;
            }
        } else {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
        }
        frame_count++;
    }

done:
    printf("[client] shutting down...\n");

    if (video_cap) {
        uint64_t written = fr_video_capture_frames_written(video_cap);
        fr_video_capture_destroy(video_cap);
        printf("[client] recorded %" PRIu64 " frames to %s\n",
               written, record_path);
        video_cap = NULL;
    }

    if (!headless) {
        /* Clean up per-body custom VAOs. */
        for (uint32_t ci = 0; ci < CLIENT_MAX_BODIES; ci++) {
            if (render_info[ci].custom_vbo) {
                glDeleteBuffers(1, &render_info[ci].custom_vbo);
            }
            if (render_info[ci].custom_vao) {
                glDeleteVertexArrays(1, &render_info[ci].custom_vao);
            }
        }
        if (srd_loaded) {
            procgen_srd_level_free(&srd_level);
        }
        gl_shutdown(&gl);
    }

    net_repl_mesh_reassembly_destroy(&mesh_reassembly);

    printf("[client] done. %u snapshots applied.\n", snap_applied_count);

    fr_prediction_tick_stop(pred_tick);
    phys_world_destroy(&world);
    fr_prediction_tick_destroy(pred_tick);
    fr_snapshot_interp_destroy(snap_interp);
    fr_rudp_stream_destroy(rx_stream);
    free(send_slots);
    net_udp_socket_close(&sock);

    return 0;
}
