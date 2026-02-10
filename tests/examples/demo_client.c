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

#include "ferrum/net/client/runtime_rx.h"
#include "ferrum/net/client/runtime_tx.h"
#include "ferrum/net/ghost_table.h"
#include "ferrum/net/prediction.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/interp/pose_interpolator.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/udp_socket.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"

/* ── Constants ──────────────────────────────────────────────────── */

#define CLIENT_MAX_BODIES   1024u
#define CLIENT_WIN_W        960
#define CLIENT_WIN_H        540
#define CLIENT_MOVE_SPEED   5.0f
#define CLIENT_MOUSE_SENS   0.002f
#define CLIENT_RENDER_DELAY 0.10  /* seconds behind to smooth interpolation */
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
    uint8_t  shape_type;  /**< 0=box. */
    float    half_x;      /**< Half-extent X in meters. */
    float    half_y;      /**< Half-extent Y in meters. */
    float    half_z;      /**< Half-extent Z in meters. */
    fr_pose_interpolator_t interp;
} entity_view_t;

/* ── GL context ─────────────────────────────────────────────────── */

typedef struct gl_ctx {
    SDL_Window      *window;
    SDL_GLContext     gl;
    gl_loader_t       loader;
    shader_program_t  program;
    shader_uniform_cache_t uniforms;
    vbo_t             vbo;
    vao_t             vao;
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

    glEnable(GL_DEPTH_TEST);
    SDL_GL_SetSwapInterval(1);

    /* Capture mouse for FPS look. */
    SDL_SetRelativeMouseMode(SDL_TRUE);

    return 0;
}

static void gl_shutdown(gl_ctx_t *ctx) {
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
                "Usage: %s <server_ip> <port> [duration_s] [--headless]\n",
                argv[0]);
        return 1;
    }

    const char *server_ip_str = argv[1];
    uint16_t port = (uint16_t)atoi(argv[2]);
    double duration = (argc >= 4 && strcmp(argv[3], "--headless") != 0)
                          ? atof(argv[3]) : 0.0;
    int headless = 0;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        }
    }

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

        /* Send keepalive/JOIN every 100ms. */
        if (now_ms >= next_keepalive_ms) {
            net_rudp_peer_send_unreliable(
                &peer, &sock, &server_addr, now_ms,
                NET_REPL_SCHEMA_JOIN, join_payload, sizeof(join_payload));
            next_keepalive_ms = now_ms + 100u;
        }

        /* Drain all pending packets. */
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
            if (net_rudp_peer_receive(&peer, rx_buf, rx_size, 0u,
                                      &reliable, &schema_id,
                                      payload, sizeof(payload),
                                      &payload_size) != NET_RUDP_OK) {
                continue;
            }

            const double recv_time = now_s();

            /* ── Stage 3: Process reliable events ──────────────── */
            if (schema_id == NET_REPL_SCHEMA_BODY_SPAWN) {
                net_repl_body_spawn_t sp;
                if (net_repl_body_spawn_decode(&sp, payload,
                                               payload_size) != NET_REPL_OK) {
                    continue;
                }

                /* Already known? */
                net_ghost_entity_t existing;
                if (net_ghost_table_lookup(&ghosts, sp.body_id,
                                           &existing) == NET_GHOST_OK) {
                    continue;
                }

                if (entity_count >= CLIENT_MAX_BODIES) {
                    continue;
                }

                uint32_t idx = entity_count++;
                entity_view_t *e = &entities[idx];
                e->body_id    = sp.body_id;
                e->is_static  = (sp.flags & 1u) ? 1 : 0;
                e->shape_type = sp.shape_type;
                e->half_x     = (float)sp.half_x_mm / 1000.0f;
                e->half_y     = (float)sp.half_y_mm / 1000.0f;
                e->half_z     = (float)sp.half_z_mm / 1000.0f;
                fr_pose_interpolator_reset(&e->interp);

                /* Dequantize initial pose. */
                vec3_t pos = {
                    (float)sp.pos_mm.x_mm / 1000.0f,
                    (float)sp.pos_mm.y_mm / 1000.0f,
                    (float)sp.pos_mm.z_mm / 1000.0f,
                };
                quat_t rot = {sp.rot_x, sp.rot_y, sp.rot_z, sp.rot_w};
                fr_pose_interpolator_push(&e->interp, recv_time, pos, rot);

                net_ghost_entity_t ghost = {.index = idx, .generation = 1};
                net_ghost_table_create(&ghosts, sp.body_id, ghost);
            }

            /* ── Stage 4: Apply unreliable state updates ───────── */
            if (schema_id == NET_REPL_SCHEMA_BODY_STATE) {
                net_repl_body_state_t bs;
                if (net_repl_body_state_decode(&bs, payload,
                                               payload_size) != NET_REPL_OK) {
                    continue;
                }

                net_ghost_entity_t ghost;
                if (net_ghost_table_lookup(&ghosts, bs.body_id,
                                           &ghost) != NET_GHOST_OK) {
                    continue; /* state before spawn — drop */
                }

                entity_view_t *e = &entities[ghost.index];

                vec3_t pos = {
                    (float)bs.pos_mm.x_mm / 1000.0f,
                    (float)bs.pos_mm.y_mm / 1000.0f,
                    (float)bs.pos_mm.z_mm / 1000.0f,
                };
                quat_t rot = {bs.rot_x, bs.rot_y, bs.rot_z, bs.rot_w};
                fr_pose_interpolator_push(&e->interp, recv_time, pos, rot);

                /* Reconcile prediction if this is a body we're tracking
                 * for prediction (future: per-body prediction). */
            }

            if (schema_id == NET_REPL_SCHEMA_WELCOME) {
                net_repl_welcome_t w;
                net_repl_welcome_decode(&w, payload, payload_size);
                printf("[client] WELCOME received (tick_hz=%u)\n", w.tick_hz);
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
            printf("[client] entities=%u cam=(%.1f, %.1f, %.1f)\n",
                   entity_count, cam_pos.x, cam_pos.y, cam_pos.z);
            next_diag_ms = now_ms + 2000u;
        }

        /* ── Stage 7-10: Render ────────────────────────────────── */
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
                                 0.1f, 500.0f, &proj);

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

                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }

                glBindVertexArray(0);
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

    free(send_slots);
    net_udp_socket_close(&sock);

    printf("[client] done. %u entities received.\n", entity_count);
    return 0;
}
