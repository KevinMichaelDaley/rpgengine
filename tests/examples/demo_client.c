/**
 * @file demo_client.c
 * @brief Full-stack demo client: SDL2 + OpenGL renderer with FPS controls.
 *
 * Connects to the demo server, receives BODY_SPAWN and BODY_STATE messages,
 * renders all replicated bodies as cubes, and sends INPUT_MOVE messages
 * for the local player camera.  Prediction/reconciliation is applied to
 * the local player state; all other entities use pose interpolation.
 *
 * Usage:  ./demo_client <server_ip> <port> [duration_s] [--headless]
 * Example: ./demo_client 127.0.0.1 40080 30
 */

#define _POSIX_C_SOURCE 200809L

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <math.h>
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

#include "ferrum/net/ghost_table.h"
#include "ferrum/net/prediction.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/body_state_batch.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/interp/pose_interpolator.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/stream.h"
#include "ferrum/net/udp_socket.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"

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
#define CLIENT_RENDER_DELAY 0.066  /* seconds behind to smooth interpolation */
#define CLIENT_PREDICT_RING 128u

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

/* ── Entity storage ─────────────────────────────────────────────── */

/** Local entity created when BODY_SPAWN arrives. */
typedef struct entity_view {
    uint16_t body_id;     /**< Server physics body index. */
    uint8_t  is_static;   /**< Static body (ground plane). */
    uint8_t  shape_type;  /**< 0=box, 1=sphere, 2=capsule. */
    float    half_x;      /**< Half-extent X in meters. */
    float    half_y;      /**< Half-extent Y in meters. */
    float    half_z;      /**< Half-extent Z in meters. */
    fr_pose_interpolator_t interp;

    /* Correction debug lines: when a new server state pushes the
     * interpolator, we snapshot the old rendered pose.  The renderer
     * draws lines from old corners → new corners, fading over time. */
    vec3_t  corr_old_pos;   /**< Position before correction. */
    quat_t  corr_old_rot;   /**< Rotation before correction. */
    vec3_t  corr_raw_pos;   /**< Raw server correction position. */
    quat_t  corr_raw_rot;   /**< Raw server correction rotation. */
    vec3_t  corr_vel;       /**< Server velocity at correction time. */
    vec3_t  corr_ang_vel;   /**< Server angular velocity at correction time. */
    double  corr_time_s;    /**< Receive time the correction was recorded. */
    double  corr_server_time_s; /**< Server send time (monotonic seconds). */
    float   corr_alpha;     /**< Fade-out alpha (1→0 over ~0.5s). */
} entity_view_t;

#define CORR_FADE_DURATION 0.5  /* seconds to show correction lines */

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
    uint32_t          cap_vert_count; /* number of capsule vertices */
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

/** Segments around the cylinder axis. */
#define CAP_SLICES 12
/** Rings per hemisphere. */
#define CAP_HEMI_RINGS 4

/**
 * @brief Generate a unit capsule mesh (radius 0.5, total height 1.0)
 *        oriented along the Y axis.  Caller scales to actual size.
 *
 * @param out     Output buffer for triangle vertices (xyz).
 * @param max_verts  Maximum vertices that fit in out.
 * @return Number of vertices written, or 0 on overflow.
 */
static uint32_t gen_capsule_mesh(float *out, uint32_t max_verts)
{
    uint32_t v = 0;
    const float r = 0.5f;
    const float half_h = 0.0f; /* unit capsule: hemispheres meet at center */

#define EMIT(px,py,pz) do { \
    if (v >= max_verts) return 0; \
    out[v*3+0]=(px); out[v*3+1]=(py); out[v*3+2]=(pz); v++; \
} while(0)

    /* Cylinder body (2 triangles per slice). */
    for (int i = 0; i < CAP_SLICES; i++) {
        float a0 = (float)i / CAP_SLICES * 2.0f * FERRUM_PI;
        float a1 = (float)(i+1) / CAP_SLICES * 2.0f * FERRUM_PI;
        float c0 = cosf(a0)*r, s0 = sinf(a0)*r;
        float c1 = cosf(a1)*r, s1 = sinf(a1)*r;
        EMIT(c0, -half_h, s0); EMIT(c1, -half_h, s1); EMIT(c1,  half_h, s1);
        EMIT(c0, -half_h, s0); EMIT(c1,  half_h, s1); EMIT(c0,  half_h, s0);
    }

    /* Top hemisphere (+Y). */
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

    /* Bottom hemisphere (-Y). */
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
            /* Winding reversed for bottom hemisphere. */
            EMIT(x00,y0,z00); EMIT(x11,y1,z11); EMIT(x10,y0,z10);
            EMIT(x00,y0,z00); EMIT(x01,y1,z01); EMIT(x11,y1,z11);
        }
    }

#undef EMIT
    return v;
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

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "glewInit failed\n");
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    (void)glGetError(); /* clear spurious GLEW error */

    ctx->loader.get_proc_address = sdl_get_proc;
    ctx->loader.user_data = NULL;

    /* Shader program */
    const char *vs =
        "#version 330 core\n"
        "in vec3 in_pos;\n"
        "uniform mat4 u_mvp;\n"
        "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";
    const char *fs =
        "#version 330 core\n"
        "uniform vec3 u_color;\n"
        "out vec4 out_color;\n"
        "void main() { out_color = vec4(u_color, 1.0); }\n";

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

    /* Cube VBO + VAO */
    static const float cube_verts[] = {
        /* +X */  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
                  0.5f,-0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f,
        /* -X */ -0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
                 -0.5f,-0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,
        /* +Y */ -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
                 -0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
        /* -Y */ -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f,
                 -0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
        /* +Z */ -0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
                 -0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f,
        /* -Z */  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
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
        vao_bind_attributes(&ctx->cap_vao, &ctx->cap_vbo, &attr, 1u,
                            3u * sizeof(float));
    }

    glEnable(GL_DEPTH_TEST);
    SDL_GL_SetSwapInterval(1);

    /* Capture mouse for FPS look. */
    SDL_SetRelativeMouseMode(SDL_TRUE);

    return 0;
}

static void gl_shutdown(gl_ctx_t *ctx) {
    vao_destroy(&ctx->cap_vao);
    vbo_destroy(&ctx->cap_vbo);
    vao_destroy(&ctx->vao);
    vbo_destroy(&ctx->vbo);
    shader_program_destroy(&ctx->program);
    if (ctx->gl) {
        SDL_GL_DeleteContext(ctx->gl);
    }
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
    }
    SDL_Quit();
}

/* ── Correction debug line helpers ──────────────────────────────── */

/** Rotate a vector by a quaternion: q * v * q⁻¹ */
static vec3_t quat_rotate_vec3(quat_t q, vec3_t v) {
    /* t = 2 * cross(q.xyz, v) */
    float tx = 2.0f * (q.y * v.z - q.z * v.y);
    float ty = 2.0f * (q.z * v.x - q.x * v.z);
    float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return (vec3_t){
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

/** Compute the 8 corners of an axis-aligned box transformed by pos+rot. */
static void box_corners(vec3_t pos, quat_t rot,
                        float hx, float hy, float hz,
                        vec3_t out[8]) {
    const float sx[8] = {-1, 1, 1,-1,-1, 1, 1,-1};
    const float sy[8] = {-1,-1, 1, 1,-1,-1, 1, 1};
    const float sz[8] = {-1,-1,-1,-1, 1, 1, 1, 1};
    for (int i = 0; i < 8; ++i) {
        vec3_t local = {sx[i] * hx, sy[i] * hy, sz[i] * hz};
        vec3_t world = quat_rotate_vec3(rot, local);
        out[i] = (vec3_t){pos.x + world.x, pos.y + world.y, pos.z + world.z};
    }
}

/**
 * Draw correction debug lines.
 *
 * Yellow lines: old rendered pose → raw server pose (shows correction error).
 *
 * Max bodies to draw lines for per frame, to avoid GPU stalls.
 */
#define CORR_MAX_LINES_PER_FRAME 128u

static void draw_correction_lines(gl_ctx_t *glc,
                                  const entity_view_t *entities,
                                  uint32_t entity_count,
                                  double now_time,
                                  double render_time,
                                  mat4_t vp) {
    /* Yellow lines: pre-correction interpolated pose → raw server pose.
     * corr_old_pos/rot was sampled from the interpolator at render_time
     * *before* the push, so it reflects what was on screen last frame. */
    float line_verts[CORR_MAX_LINES_PER_FRAME * 8 * 2 * 3];
    uint32_t vert_count = 0;
    uint32_t pair_count = 0;

    for (uint32_t i = 0; i < entity_count; ++i) {
        if (pair_count >= CORR_MAX_LINES_PER_FRAME) { break; }
        const entity_view_t *e = &entities[i];
        if (e->is_static) { continue; }
        if (e->corr_alpha <= 0.0f) { continue; }

        vec3_t old_corners[8], raw_corners[8];
        box_corners(e->corr_old_pos, e->corr_old_rot,
                    e->half_x, e->half_y, e->half_z, old_corners);
        box_corners(e->corr_raw_pos, e->corr_raw_rot,
                    e->half_x, e->half_y, e->half_z, raw_corners);

        for (int c = 0; c < 8; ++c) {
            float dx = raw_corners[c].x - old_corners[c].x;
            float dy = raw_corners[c].y - old_corners[c].y;
            float dz = raw_corners[c].z - old_corners[c].z;
            if (dx*dx + dy*dy + dz*dz < 0.0001f) { continue; }

            uint32_t base = vert_count * 3;
            if (base + 6 > sizeof(line_verts)/sizeof(float)) { break; }
            line_verts[base + 0] = old_corners[c].x;
            line_verts[base + 1] = old_corners[c].y;
            line_verts[base + 2] = old_corners[c].z;
            line_verts[base + 3] = raw_corners[c].x;
            line_verts[base + 4] = raw_corners[c].y;
            line_verts[base + 5] = raw_corners[c].z;
            vert_count += 2;
        }
        pair_count++;
    }

    if (vert_count == 0) { return; }

    shader_uniform_set_mat4(&glc->uniforms, &glc->program,
                            "u_mvp", vp.m, 0u);

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vert_count * 3 * sizeof(float)),
                 line_verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    float color[3] = {1.0f, 1.0f, 0.0f};
    shader_uniform_set_vec3(&glc->uniforms, &glc->program,
                            "u_color", color);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, (GLsizei)vert_count);

    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    (void)now_time;
    (void)render_time;
}

/* ── Prediction sim step callback ───────────────────────────────── */

static void predict_sim_step(net_predict_state_t *state,
                             const net_predict_input_t *input,
                             void *user) {
    (void)user;
    const float dt = 1.0f / 60.0f; /* server tick rate */
    state->velocity[0] = input->move[0] * CLIENT_MOVE_SPEED;
    state->velocity[1] = 0.0f;
    state->velocity[2] = input->move[2] * CLIENT_MOVE_SPEED;
    state->position[0] += state->velocity[0] * dt;
    state->position[1] += state->velocity[1] * dt;
    state->position[2] += state->velocity[2] * dt;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <server_ip> <port> [duration_s] [--headless]"
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

#ifdef FR_NET_EMULATION
    float emu_delay = 0.0f, emu_jitter = 0.0f, emu_loss = 0.0f;
    float emu_reorder = 0.0f, emu_duplicate = 0.0f;
    net_emu_distribution_t emu_dist = NET_EMU_DIST_UNIFORM;
#endif

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
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
    /* Configure engine settings before any I/O. */
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

    /* Reliable stream reassembly for STREAM_FRAME messages from the server. */
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
    }

    /* ── 4. Ghost table + entity storage ───────────────────────── */
    net_ghost_entry_t ghost_entries[CLIENT_MAX_BODIES];
    net_ghost_table_t ghosts;
    net_ghost_table_init(&ghosts, ghost_entries, CLIENT_MAX_BODIES);

    entity_view_t entities[CLIENT_MAX_BODIES];
    memset(entities, 0, sizeof(entities));
    uint32_t entity_count = 0;

    /* ── 5. Prediction context ─────────────────────────────────── */
    net_predict_input_t predict_ring_buf[CLIENT_PREDICT_RING];
    net_predict_ctx_t predict;
    net_predict_config_t pred_cfg = {
        .snap_threshold = 2.0f,
        .blend_threshold = 0.01f,
        .blend_rate = 0.1f,
    };
    net_predict_init(&predict, predict_ring_buf, CLIENT_PREDICT_RING,
                     &pred_cfg, predict_sim_step, NULL);

    /* ── 6. FPS camera state ───────────────────────────────────── */
    float cam_yaw   = 0.0f;
    float cam_pitch = 0.0f;
    vec3_t cam_pos  = {0.0f, 5.0f, 20.0f};

    /* ── 7. Main loop ──────────────────────────────────────────── */
    const double start_time = now_s();
    uint64_t next_keepalive_ms = now_ms_val();
    uint64_t next_diag_ms = now_ms_val() + 1000u;
    uint32_t client_tick = 0;
    uint64_t last_frame_ns = now_ns();
    uint32_t dbg_batch_rx = 0;
    uint32_t dbg_state_applied = 0;
    uint32_t dbg_stream_frames = 0;
    uint8_t rx_buf[1500];

    printf("[client] entering main loop\n");

    while (g_running) {
        const uint64_t frame_ns = now_ns();
        const double dt_s = (double)(frame_ns - last_frame_ns) / 1e9;
        last_frame_ns = frame_ns;
        const uint64_t now_ms = frame_ns / 1000000ull;
        const double now_time = (double)frame_ns / 1e9;

        /* Duration limit. */
        if (duration > 0.0 && (now_time - start_time) >= duration) {
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
                    if (cam_pitch >  1.5f) { cam_pitch =  1.5f; }
                    if (cam_pitch < -1.5f) { cam_pitch = -1.5f; }
                }
            }
            const uint8_t *keys = SDL_GetKeyboardState(NULL);
            move_fwd   = keys[SDL_SCANCODE_W];
            move_back  = keys[SDL_SCANCODE_S];
            move_left  = keys[SDL_SCANCODE_A];
            move_right = keys[SDL_SCANCODE_D];
        }

        /* ── Stage 2: Network receive ──────────────────────────── */
        net_rudp_peer_tick_resend(&peer, &sock, &server_addr, now_ms);
        const double recv_time = now_s();

        /* Send keepalive/JOIN every 100ms. */
        if (now_ms >= next_keepalive_ms) {
            net_rudp_peer_send_unreliable(
                &peer, &sock, &server_addr, now_ms,
                NET_REPL_SCHEMA_JOIN, join_payload, sizeof(join_payload));
            next_keepalive_ms = now_ms + 100u;
        }

        /* Drain all pending RUDP packets. */
        for (;;) {
            size_t rx_size = 0;
            int rc = net_udp_socket_recv(&sock, rx_buf, sizeof(rx_buf),
                                         &rx_size);
            if (rc != NET_UDP_SOCKET_OK) {
                break;
            }

            uint8_t reliable = 0;
            uint16_t schema_id = 0;
            uint8_t payload[1472];
            size_t payload_size = 0;
            int rudp_rc = net_rudp_peer_receive(&peer, rx_buf, rx_size, now_ms,
                                      &reliable, &schema_id,
                                      payload, sizeof(payload),
                                      &payload_size);
            if (rudp_rc != NET_RUDP_OK) {
                continue;
            }

            /* Reliable stream frames: push into stream for reassembly. */
            if (schema_id == NET_REPL_SCHEMA_STREAM_FRAME) {
                dbg_stream_frames++;
                if (payload_size >= 4u) {
                    uint16_t sseq = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
                    bool ok = fr_rudp_stream_push_frame(rx_stream, payload, payload_size);
                    if (!ok) {
                        fprintf(stderr, "[client] push_frame REJECTED sseq=%u len=%zu\n",
                                (unsigned)sseq, payload_size);
                    }
                } else {
                    fprintf(stderr, "[client] STREAM_FRAME too short: %zu\n", payload_size);
                }
                continue;
            }

            /* Unreliable BODY_STATE_BATCH: decode and apply each entry. */
            if (schema_id == NET_REPL_SCHEMA_BODY_STATE_BATCH) {
                dbg_batch_rx++;
                uint16_t batch_count = 0u;
                const uint8_t *entries = NULL;
                if (net_repl_body_state_batch_decode(payload, payload_size,
                                                     &batch_count,
                                                     &entries) != NET_REPL_OK) {
                    continue;
                }

                for (uint16_t bi = 0u; bi < batch_count; ++bi) {
                    net_repl_body_state_t bs;
                    if (net_repl_body_state_decode(
                            &bs,
                            entries + (size_t)bi * NET_REPL_BODY_STATE_PAYLOAD_SIZE,
                            NET_REPL_BODY_STATE_PAYLOAD_SIZE) != NET_REPL_OK) {
                        continue;
                    }

                    net_ghost_entity_t ghost;
                    if (net_ghost_table_lookup(&ghosts, bs.body_id,
                                               &ghost) != NET_GHOST_OK) {
                        continue;
                    }

                    entity_view_t *e = &entities[ghost.index];
                    vec3_t pos = {
                        (float)bs.pos_mm.x_mm / 1000.0f,
                        (float)bs.pos_mm.y_mm / 1000.0f,
                        (float)bs.pos_mm.z_mm / 1000.0f,
                    };
                    quat_t rot = {bs.rot_x, bs.rot_y, bs.rot_z, bs.rot_w};

                    /* Snapshot the interpolated pose at current render time,
                     * *before* pushing the new snapshot.  This is what's
                     * on screen right now. */
                    vec3_t vel = {
                        (float)bs.vel_x_mm_s / 1000.0f,
                        (float)bs.vel_y_mm_s / 1000.0f,
                        (float)bs.vel_z_mm_s / 1000.0f,
                    };
                    vec3_t ang_vel = {
                        (float)bs.ang_x_mrad_s / 1000.0f,
                        (float)bs.ang_y_mrad_s / 1000.0f,
                        (float)bs.ang_z_mrad_s / 1000.0f,
                    };

                    /* Correction debug: sample before and after push at
                     * the same render-time so the line shows the actual
                     * correction jump, not the natural travel distance. */
                    double sample_t = recv_time - CLIENT_RENDER_DELAY;
                    vec3_t old_pos;
                    quat_t old_rot;
                    bool had_old = fr_pose_interpolator_sample(
                        &e->interp, sample_t, 1e-6f, &old_pos, &old_rot);

                    fr_pose_interpolator_push(&e->interp, recv_time, pos, rot,
                                              vel, ang_vel,
                                              (double)bs.send_time_ms / 1000.0);

                    if (had_old) {
                        vec3_t new_pos;
                        quat_t new_rot;
                        fr_pose_interpolator_sample(
                            &e->interp, sample_t, 1e-6f, &new_pos, &new_rot);
                        e->corr_old_pos = old_pos;
                        e->corr_old_rot = old_rot;
                        e->corr_raw_pos = new_pos;
                        e->corr_raw_rot = new_rot;
                        e->corr_time_s  = recv_time;
                        e->corr_alpha   = 1.0f;
                    }

                    e->corr_vel      = vel;
                    e->corr_ang_vel  = ang_vel;
                    e->corr_server_time_s = (double)bs.send_time_ms / 1000.0;
                    dbg_state_applied++;
                }
            }

            /* Legacy: individual BODY_STATE (fallback). */
            if (schema_id == NET_REPL_SCHEMA_BODY_STATE) {
                net_repl_body_state_t bs;
                if (net_repl_body_state_decode(&bs, payload,
                                               payload_size) != NET_REPL_OK) {
                    continue;
                }

                net_ghost_entity_t ghost;
                if (net_ghost_table_lookup(&ghosts, bs.body_id,
                                           &ghost) != NET_GHOST_OK) {
                    continue;
                }

                entity_view_t *e = &entities[ghost.index];
                vec3_t pos = {
                    (float)bs.pos_mm.x_mm / 1000.0f,
                    (float)bs.pos_mm.y_mm / 1000.0f,
                    (float)bs.pos_mm.z_mm / 1000.0f,
                };
                quat_t rot = {bs.rot_x, bs.rot_y, bs.rot_z, bs.rot_w};

                vec3_t vel = {
                    (float)bs.vel_x_mm_s / 1000.0f,
                    (float)bs.vel_y_mm_s / 1000.0f,
                    (float)bs.vel_z_mm_s / 1000.0f,
                };
                vec3_t ang_vel = {
                    (float)bs.ang_x_mrad_s / 1000.0f,
                    (float)bs.ang_y_mrad_s / 1000.0f,
                    (float)bs.ang_z_mrad_s / 1000.0f,
                };

                /* Correction debug: sample before and after push. */
                double sample_t = recv_time - CLIENT_RENDER_DELAY;
                vec3_t old_pos;
                quat_t old_rot;
                bool had_old = fr_pose_interpolator_sample(
                    &e->interp, sample_t, 1e-6f, &old_pos, &old_rot);

                fr_pose_interpolator_push(&e->interp, recv_time, pos, rot,
                                          vel, ang_vel,
                                          (double)bs.send_time_ms / 1000.0);

                if (had_old) {
                    vec3_t new_pos;
                    quat_t new_rot;
                    fr_pose_interpolator_sample(
                        &e->interp, sample_t, 1e-6f, &new_pos, &new_rot);
                    e->corr_old_pos = old_pos;
                    e->corr_old_rot = old_rot;
                    e->corr_raw_pos = new_pos;
                    e->corr_raw_rot = new_rot;
                    e->corr_time_s  = recv_time;
                    e->corr_alpha   = 1.0f;
                }

                e->corr_vel      = vel;
                e->corr_ang_vel  = ang_vel;
                e->corr_server_time_s = (double)bs.send_time_ms / 1000.0;
            }

            if (schema_id == NET_REPL_SCHEMA_WELCOME) {
                net_repl_welcome_t w;
                net_repl_welcome_decode(&w, payload, payload_size);
                printf("[client] WELCOME received (tick_hz=%u)\n", w.tick_hz);
            }
        }

        /* Pop reassembled reliable messages from the stream.
         * Each message is [schema_id:u16 LE][payload]. */
        {
            uint8_t stream_msg[1400];
            for (;;) {
                size_t msg_len = sizeof(stream_msg);
                if (!fr_rudp_stream_pop(rx_stream, 0u, stream_msg, &msg_len)) {
                    break;
                }
                if (msg_len < 2u) { continue; }

                uint16_t inner_schema = (uint16_t)stream_msg[0]
                                      | ((uint16_t)stream_msg[1] << 8u);
                const uint8_t *inner_payload = stream_msg + 2u;
                size_t inner_len = msg_len - 2u;

                if (inner_schema == NET_REPL_SCHEMA_BODY_SPAWN) {
                    net_repl_body_spawn_t sp;
                    if (net_repl_body_spawn_decode(&sp, inner_payload,
                                                   inner_len) != NET_REPL_OK) {
                        continue;
                    }

                    net_ghost_entity_t existing;
                    if (net_ghost_table_lookup(&ghosts, sp.body_id,
                                               &existing) == NET_GHOST_OK) {
                        continue;
                    }

                    if (entity_count >= CLIENT_MAX_BODIES) { continue; }

                    uint32_t idx = entity_count++;
                    entity_view_t *e = &entities[idx];
                    e->body_id    = sp.body_id;
                    e->is_static  = (sp.flags & 1u) ? 1 : 0;
                    e->shape_type = sp.shape_type;
                    e->half_x     = net_float16_to_float(sp.half_x_f16);
                    e->half_y     = net_float16_to_float(sp.half_y_f16);
                    e->half_z     = net_float16_to_float(sp.half_z_f16);
                    fr_pose_interpolator_reset(&e->interp);

                    vec3_t pos = {
                        (float)sp.pos_mm.x_mm / 1000.0f,
                        (float)sp.pos_mm.y_mm / 1000.0f,
                        (float)sp.pos_mm.z_mm / 1000.0f,
                    };
                    quat_t rot = {sp.rot_x, sp.rot_y, sp.rot_z, sp.rot_w};
                    fr_pose_interpolator_push(&e->interp, recv_time, pos, rot,
                                              (vec3_t){0,0,0}, (vec3_t){0,0,0}, 0.0);

                    net_ghost_entity_t ghost = {.index = idx, .generation = 1};
                    net_ghost_table_create(&ghosts, sp.body_id, ghost);

                    printf("[client] SPAWN body=%u half=(%.2f,%.2f,%.2f)\n",
                           sp.body_id, e->half_x, e->half_y, e->half_z);
                }

                if (inner_schema == NET_REPL_SCHEMA_WELCOME) {
                    net_repl_welcome_t w;
                    net_repl_welcome_decode(&w, inner_payload, inner_len);
                    printf("[client] WELCOME received (tick_hz=%u)\n",
                           w.tick_hz);
                }
            }
        }

        /* ── Stage 5: FPS camera movement ──────────────────────── */
        {
            /* Forward/right vectors from yaw (ignore pitch for movement). */
            float fwd_x = -sinf(cam_yaw);
            float fwd_z = -cosf(cam_yaw);
            float right_x = cosf(cam_yaw);
            float right_z = -sinf(cam_yaw);

            float mx = 0.0f, mz = 0.0f;
            if (move_fwd)   { mx += fwd_x;   mz += fwd_z; }
            if (move_back)  { mx -= fwd_x;   mz -= fwd_z; }
            if (move_left)  { mx -= right_x; mz -= right_z; }
            if (move_right) { mx += right_x; mz += right_z; }

            /* Normalize diagonal movement. */
            float len = sqrtf(mx * mx + mz * mz);
            if (len > 0.001f) {
                mx /= len;
                mz /= len;
            }

            cam_pos.x += mx * CLIENT_MOVE_SPEED * (float)dt_s;
            cam_pos.z += mz * CLIENT_MOVE_SPEED * (float)dt_s;

            /* Push input to prediction ring. */
            net_predict_input_t inp = {
                .tick = client_tick,
                .move = {mx, 0.0f, mz},
            };
            net_predict_input_ring_push(&predict.input_ring, &inp);
            predict.predicted.position[0] = cam_pos.x;
            predict.predicted.position[1] = cam_pos.y;
            predict.predicted.position[2] = cam_pos.z;
            predict.predicted_tick = client_tick;
            client_tick++;

            /* Send INPUT_MOVE to server.
             * Wire format: [schema_id:u16 LE][tick:u32 LE][mx:f32 LE][my:f32 LE][mz:f32 LE] */
            uint8_t input_pkt[18];
            input_pkt[0] = (uint8_t)(NET_REPL_SCHEMA_INPUT_MOVE & 0xFFu);
            input_pkt[1] = (uint8_t)((NET_REPL_SCHEMA_INPUT_MOVE >> 8u) & 0xFFu);
            uint32_t tick_le = client_tick;
            float move_arr[3] = {mx, 0.0f, mz};
            memcpy(input_pkt + 2, &tick_le, 4);
            memcpy(input_pkt + 6, move_arr, 12);
            net_rudp_peer_send_unreliable(
                &peer, &sock, &server_addr, now_ms,
                NET_REPL_SCHEMA_INPUT_MOVE,
                input_pkt + 2, 16u);
        }

        /* ── Stage 6: Diagnostics ──────────────────────────────── */
        if (now_ms >= next_diag_ms) {
            printf("[client] entities=%u cam=(%.1f, %.1f, %.1f) batches=%u applied=%u stream_frames=%u\n",
                   entity_count, cam_pos.x, cam_pos.y, cam_pos.z,
                   dbg_batch_rx, dbg_state_applied, dbg_stream_frames);
            if (entity_count > 1u) {
                entity_view_t *e1 = &entities[1];
                printf("[client] body1 interp: prev=(%.2f,%.2f,%.2f)@%.3f curr=(%.2f,%.2f,%.2f)@%.3f render_t=%.3f\n",
                       e1->interp.prev_pos.x, e1->interp.prev_pos.y, e1->interp.prev_pos.z, e1->interp.prev_time_s,
                       e1->interp.curr_pos.x, e1->interp.curr_pos.y, e1->interp.curr_pos.z, e1->interp.curr_time_s,
                       now_time - CLIENT_RENDER_DELAY);
            }
            next_diag_ms = now_ms + 2000u;
        }

        /* ── Stage 7-10: Render ────────────────────────────────── */

        /* Decay correction debug line alpha. */
        for (uint32_t i = 0; i < entity_count; ++i) {
            entity_view_t *e = &entities[i];
            if (e->corr_alpha > 0.0f) {
                double age = now_time - e->corr_time_s;
                e->corr_alpha = (age < CORR_FADE_DURATION)
                    ? 1.0f - (float)(age / CORR_FADE_DURATION) : 0.0f;
            }
        }

        if (!headless) {
            glViewport(0, 0, CLIENT_WIN_W, CLIENT_WIN_H);
            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (shader_program_bind(&gl.program) == SHADER_PROGRAM_OK) {
                glBindVertexArray(vao_handle(&gl.vao));

                /* Projection. */
                mat4_t proj;
                float aspect = (float)CLIENT_WIN_W / (float)CLIENT_WIN_H;
                mat4_perspective(70.0f * (FERRUM_PI / 180.0f), aspect,
                                 0.1f, 5000.0f, &proj);

                /* FPS camera view matrix. */
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

                /* Render time slightly behind for interpolation. */
                double render_time = now_time - CLIENT_RENDER_DELAY;

                for (uint32_t i = 0; i < entity_count; ++i) {
                    entity_view_t *e = &entities[i];
                    vec3_t pos;
                    quat_t rot;
                    if (!fr_pose_interpolator_sample(&e->interp,
                                                     render_time, 1e-6f,
                                                     &pos, &rot)) {
                        continue;
                    }

                    mat4_t t = mat4_translation(pos.x, pos.y, pos.z);
                    mat4_t r = mat4_from_quat(rot);
                    mat4_t s = mat4_scaling(e->half_x * 2.0f,
                                            e->half_y * 2.0f,
                                            e->half_z * 2.0f);
                    mat4_t model = mat4_mul(t, mat4_mul(r, s));
                    mat4_t mvp = mat4_mul(vp, model);

                    shader_uniform_set_mat4(&gl.uniforms, &gl.program,
                                            "u_mvp", mvp.m, 0u);

                    float rgb[3];
                    if (e->is_static) {
                        rgb[0] = 0.3f; rgb[1] = 0.6f; rgb[2] = 0.3f;
                    } else {
                        color_from_body(e->body_id, rgb);
                    }
                    shader_uniform_set_vec3(&gl.uniforms, &gl.program,
                                            "u_color", rgb);

                    if (e->shape_type == 2) {
                        /* Capsule. */
                        glBindVertexArray(vao_handle(&gl.cap_vao));
                        glDrawArrays(GL_TRIANGLES, 0,
                                     (GLsizei)gl.cap_vert_count);
                        glBindVertexArray(vao_handle(&gl.vao));
                    } else {
                        glDrawArrays(GL_TRIANGLES, 0, 36);
                    }
                }

                glBindVertexArray(0);

                /* Draw correction debug lines (old corner → new corner). */
                draw_correction_lines(&gl, entities, entity_count,
                                      now_time, render_time, vp);
            }

            SDL_GL_SwapWindow(gl.window);
        } else {
            /* Headless: sleep to avoid busy-spin. */
            struct timespec ts = {0, 1000000}; /* 1ms */
            nanosleep(&ts, NULL);
        }
    }

done:
    printf("[client] shutting down...\n");

    if (!headless) {
        gl_shutdown(&gl);
    }

    fr_rudp_stream_destroy(rx_stream);
    free(send_slots);
    net_udp_socket_close(&sock);

    printf("[client] done. %u entities received.\n", entity_count);
    return 0;
}
