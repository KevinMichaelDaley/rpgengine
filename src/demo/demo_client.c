/**
 * @file demo_client.c
 * @brief Graphical SDL2+OpenGL demo client with FPS camera, WASD movement,
 *        mouse look, box spawning, and fire beam rendering.
 *
 * Usage: ./build/demo_client <server_ip> <port>
 *
 * Fork of tests/p008_renderer_client.c adapted for FPS-style gameplay:
 *   - FPS camera (demo_camera_t) instead of fixed isometric view
 *   - WASD movement + mouse look via SDL relative mouse mode
 *   - INPUT_MOVE sending instead of INPUT_ROT
 *   - Sphere mesh rendering (icosphere geometry)
 *   - Ground plane rendering
 *   - Fire beam line rendering
 *   - E key to spawn boxes
 */
#define _POSIX_C_SOURCE 200809L

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include <errno.h>
#include <math.h>
#include <stddef.h>
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

#include "ferrum/net/quantization.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/udp_socket.h"

#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/interp/pose_interpolator.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/spawn_batch.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/replication/welcome.h"

#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/collider.h"
#include "ferrum/job/system.h"
#include "ferrum/net/topic_channel.h"

#include "ferrum/demo/camera.h"
#include "ferrum/demo/geometry.h"
#include "ferrum/demo/input_move.h"
#include "ferrum/demo/input_spawn.h"

#include "ferrum/renderer/debug_correction_lines.h"
#include "ferrum/renderer/debug_lines.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"

/* ------------------------------------------------------------------ */
/*  Structures                                                        */
/* ------------------------------------------------------------------ */

/** Maximum bodies the client physics world can hold. */
#define CLIENT_MAX_BODIES 512u

/** Client-side view of a replicated entity. */
struct entity_view {
    uint32_t entity_id;          /**< Server body_id. */
    uint32_t phys_body;          /**< Index in client physics world (UINT32_MAX = none). */
    uint16_t owner_client_id;
    uint16_t last_server_tick;   /**< Most recent server_tick applied (for ordering). */
    fr_pose_interpolator_t pose; /**< Fallback interpolator (legacy path). */
    uint8_t  shape_type;         /**< 0=box, 1=sphere, 2=capsule */
    uint8_t  flags;              /**< Body flags (kinematic, etc.) */
    uint32_t color_seed;         /**< Color seed from server. */
    float    half_ext[3];        /**< Shape half-extents in meters. */
    float    scale;              /**< Uniform scale for rendering (fallback) */
};

/** OpenGL resource context for the demo client. */
struct gl_demo_context {
    SDL_Window   *window;
    SDL_GLContext  gl;
    gl_loader_t   loader;
    shader_program_t      program;
    shader_uniform_cache_t uniforms;

    /* Cube mesh */
    vbo_t cube_vbo;
    vao_t cube_vao;

    /* Sphere mesh */
    vbo_t    sphere_vbo;
    vao_t    sphere_vao;
    uint32_t sphere_vert_count;

    /* Ground plane */
    vbo_t ground_vbo;
    vao_t ground_vao;

    /* Lines (debug correction + beam) */
    vbo_t lines_vbo;
    vao_t lines_vao;
};

/* ------------------------------------------------------------------ */
/*  Utility helpers (from p008)                                       */
/* ------------------------------------------------------------------ */

static const char *gl_error_str_(GLenum err) {
    switch (err) {
        case GL_NO_ERROR:                      return "GL_NO_ERROR";
        case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
        default:                               return "GL_UNKNOWN_ERROR";
    }
}

static void gl_check_(const char *where) {
    static int budget = 64;
    if (budget <= 0) return;
    for (;;) {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) return;
        if (budget > 0) {
            fprintf(stderr, "GL error at %s: 0x%04x (%s)\n",
                    where ? where : "?", (unsigned)err, gl_error_str_(err));
            budget--;
        }
        if (budget <= 0) {
            fprintf(stderr, "GL error budget exhausted; suppressing further GL errors\n");
            return;
        }
    }
}

static uint64_t now_ns_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t now_ms_(void) {
    return now_ns_() / 1000000ull;
}

static double now_s_(void) {
    return (double)now_ns_() / 1000000000.0;
}

static void usage_(const char *argv0) {
    fprintf(stderr, "Usage: %s <server_ipv4> <port>\n", argv0);
}

static int parse_ipv4_dotted_(const char *s, uint8_t out[4]) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!s) return 0;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    if (a > 255u || b > 255u || c > 255u || d > 255u) return 0;
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return 1;
}

static uint32_t xorshift32_(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void color_from_owner_(uint16_t owner, float out_rgb[3]) {
    const uint32_t h = (uint32_t)owner * 2654435761u;
    out_rgb[0] = (float)((h >> 0u) & 0xFFu) / 255.0f;
    out_rgb[1] = (float)((h >> 8u) & 0xFFu) / 255.0f;
    out_rgb[2] = (float)((h >> 16u) & 0xFFu) / 255.0f;
}

static mat4_t mat4_from_quat_(quat_t q) {
    mat4_t out;
    if (quat_to_mat4(q, &out) != 0) {
        return mat4_identity();
    }
    return out;
}

static void *sdl_get_proc_address_(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

/* ------------------------------------------------------------------ */
/*  GL init / shutdown                                                */
/* ------------------------------------------------------------------ */

static int gl_demo_init_(struct gl_demo_context *ctx, int w, int h) {
    if (!ctx) return -1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Try multiple GL context versions from modern to legacy. */
    typedef struct gl_attempt {
        int major, minor, profile_mask, flags;
        const char *label;
    } gl_attempt_t;

    const gl_attempt_t attempts[] = {
        {3, 3, SDL_GL_CONTEXT_PROFILE_CORE, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG, "GL 3.3 core (fwd)"},
        {3, 3, SDL_GL_CONTEXT_PROFILE_CORE, 0, "GL 3.3 core"},
        {3, 3, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY, 0, "GL 3.3 compat"},
        {3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY, 0, "GL 3.0 compat"},
        {2, 1, 0, 0, "GL 2.1"},
    };

    int created = 0;
    for (size_t i = 0u; i < sizeof(attempts) / sizeof(attempts[0]); ++i) {
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attempts[i].major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempts[i].minor);
        if (attempts[i].profile_mask != 0)
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, attempts[i].profile_mask);
        if (attempts[i].flags != 0)
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, attempts[i].flags);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        ctx->window = SDL_CreateWindow("demo_client",
                                       SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                       w, h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (!ctx->window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return -1;
        }

        ctx->gl = SDL_GL_CreateContext(ctx->window);
        if (!ctx->gl) {
            fprintf(stderr, "SDL_GL_CreateContext failed (%s): %s\n", attempts[i].label, SDL_GetError());
            SDL_DestroyWindow(ctx->window);
            ctx->window = NULL;
            continue;
        }
        if (SDL_GL_MakeCurrent(ctx->window, ctx->gl) != 0) {
            fprintf(stderr, "SDL_GL_MakeCurrent failed (%s): %s\n", attempts[i].label, SDL_GetError());
            SDL_GL_DeleteContext(ctx->gl);
            SDL_DestroyWindow(ctx->window);
            ctx->gl = NULL;
            ctx->window = NULL;
            continue;
        }
        created = 1;
        break;
    }

    if (!created) {
        fprintf(stderr, "Failed to create any OpenGL context\n");
        SDL_Quit();
        return -1;
    }

    glewExperimental = GL_TRUE;
    GLenum glew_status = glewInit();
    if (glew_status != GLEW_OK) {
        fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glew_status));
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    (void)glGetError();

    {
        const char *v = (const char *)glGetString(GL_VENDOR);
        const char *r = (const char *)glGetString(GL_RENDERER);
        const char *g = (const char *)glGetString(GL_VERSION);
        const char *s = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
        fprintf(stderr, "GL: vendor=%s renderer=%s version=%s glsl=%s\n",
                v ? v : "(null)", r ? r : "(null)", g ? g : "(null)", s ? s : "(null)");
        gl_check_("post-glew");
    }

    ctx->loader.get_proc_address = sdl_get_proc_address_;
    ctx->loader.user_data = NULL;

    /* Select shaders based on GLSL version. */
    const char *glsl_ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int glsl_major = 0, glsl_minor = 0;
    if (glsl_ver) (void)sscanf(glsl_ver, "%d.%d", &glsl_major, &glsl_minor);

    const char *vs_src = NULL;
    const char *fs_src = NULL;
    if (glsl_major >= 3) {
        vs_src =
            "#version 330 core\n"
            "in vec3 in_pos;\n"
            "uniform mat4 u_mvp;\n"
            "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";
        fs_src =
            "#version 330 core\n"
            "uniform vec3 u_color;\n"
            "out vec4 out_color;\n"
            "void main() { out_color = vec4(u_color, 1.0); }\n";
    } else if (glsl_major == 1 && glsl_minor >= 30) {
        vs_src =
            "#version 130\n"
            "in vec3 in_pos;\n"
            "uniform mat4 u_mvp;\n"
            "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";
        fs_src =
            "#version 130\n"
            "uniform vec3 u_color;\n"
            "out vec4 out_color;\n"
            "void main() { out_color = vec4(u_color, 1.0); }\n";
    } else {
        vs_src =
            "#version 120\n"
            "attribute vec3 in_pos;\n"
            "uniform mat4 u_mvp;\n"
            "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";
        fs_src =
            "#version 120\n"
            "uniform vec3 u_color;\n"
            "void main() { gl_FragColor = vec4(u_color, 1.0); }\n";
    }

    char log_buffer[1024] = {0};
    shader_program_status_t sp = shader_program_create(&ctx->program, &ctx->loader,
                                                        vs_src, fs_src,
                                                        log_buffer, sizeof(log_buffer));
    if (sp != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader_program_create failed (%d): %s\n", (int)sp, log_buffer);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    if (shader_uniform_cache_init(&ctx->uniforms, &ctx->program) != SHADER_UNIFORM_OK) {
        fprintf(stderr, "shader_uniform_cache_init failed\n");
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    const GLint pos_loc = glGetAttribLocation((GLuint)shader_program_handle(&ctx->program), "in_pos");
    if (pos_loc < 0) {
        fprintf(stderr, "glGetAttribLocation failed for in_pos\n");
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    vao_attribute_t attrs[1];
    attrs[0] = (vao_attribute_t){(uint32_t)pos_loc, 3, GL_FLOAT, 0u, 0u, 0u};

    /* ---- Cube mesh ---- */
    static const float cube_vertices[] = {
        /* +X */ 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,0.5f, 0.5f,0.5f,0.5f,
                 0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.5f,0.5f,-0.5f,
        /* -X */ -0.5f,-0.5f,0.5f, -0.5f,-0.5f,-0.5f, -0.5f,0.5f,-0.5f,
                 -0.5f,-0.5f,0.5f, -0.5f,0.5f,-0.5f, -0.5f,0.5f,0.5f,
        /* +Y */ -0.5f,0.5f,-0.5f, 0.5f,0.5f,-0.5f, 0.5f,0.5f,0.5f,
                 -0.5f,0.5f,-0.5f, 0.5f,0.5f,0.5f, -0.5f,0.5f,0.5f,
        /* -Y */ -0.5f,-0.5f,0.5f, 0.5f,-0.5f,0.5f, 0.5f,-0.5f,-0.5f,
                 -0.5f,-0.5f,0.5f, 0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
        /* +Z */ -0.5f,-0.5f,0.5f, -0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f,
                 -0.5f,-0.5f,0.5f, 0.5f,0.5f,0.5f, 0.5f,-0.5f,0.5f,
        /* -Z */ 0.5f,-0.5f,-0.5f, 0.5f,0.5f,-0.5f, -0.5f,0.5f,-0.5f,
                 0.5f,-0.5f,-0.5f, -0.5f,0.5f,-0.5f, -0.5f,-0.5f,-0.5f,
    };

    if (vbo_create(&ctx->cube_vbo, &ctx->loader) != VBO_OK ||
        vao_create(&ctx->cube_vao, &ctx->loader) != VAO_OK) {
        fprintf(stderr, "cube VBO/VAO create failed\n");
        goto fail_gl;
    }
    if (vbo_upload(&ctx->cube_vbo, GL_ARRAY_BUFFER, cube_vertices,
                   sizeof(cube_vertices), GL_STATIC_DRAW) != VBO_OK) {
        fprintf(stderr, "cube VBO upload failed\n");
        goto fail_gl;
    }
    if (vao_bind_attributes(&ctx->cube_vao, &ctx->cube_vbo, attrs, 1u,
                            3u * sizeof(float)) != VAO_OK) {
        fprintf(stderr, "cube VAO bind failed\n");
        goto fail_gl;
    }

    /* ---- Sphere mesh (icosphere) ---- */
    if (vbo_create(&ctx->sphere_vbo, &ctx->loader) != VBO_OK ||
        vao_create(&ctx->sphere_vao, &ctx->loader) != VAO_OK) {
        fprintf(stderr, "sphere VBO/VAO create failed\n");
        goto fail_gl;
    }
    {
        uint32_t sphere_float_count = 0;
        demo_generate_icosphere(NULL, &sphere_float_count);
        float *sphere_verts = (float *)malloc(sphere_float_count * sizeof(float));
        if (!sphere_verts) {
            fprintf(stderr, "sphere vertex alloc failed\n");
            goto fail_gl;
        }
        demo_generate_icosphere(sphere_verts, &sphere_float_count);
        ctx->sphere_vert_count = sphere_float_count / 3u;

        if (vbo_upload(&ctx->sphere_vbo, GL_ARRAY_BUFFER, sphere_verts,
                       sphere_float_count * sizeof(float), GL_STATIC_DRAW) != VBO_OK) {
            free(sphere_verts);
            fprintf(stderr, "sphere VBO upload failed\n");
            goto fail_gl;
        }
        free(sphere_verts);
    }
    if (vao_bind_attributes(&ctx->sphere_vao, &ctx->sphere_vbo, attrs, 1u,
                            3u * sizeof(float)) != VAO_OK) {
        fprintf(stderr, "sphere VAO bind failed\n");
        goto fail_gl;
    }

    /* ---- Ground plane ---- */
    if (vbo_create(&ctx->ground_vbo, &ctx->loader) != VBO_OK ||
        vao_create(&ctx->ground_vao, &ctx->loader) != VAO_OK) {
        fprintf(stderr, "ground VBO/VAO create failed\n");
        goto fail_gl;
    }
    {
        float ground_verts[18];
        uint32_t ground_count = demo_generate_ground_plane(ground_verts, 200.0f);
        if (vbo_upload(&ctx->ground_vbo, GL_ARRAY_BUFFER, ground_verts,
                       ground_count * sizeof(float), GL_STATIC_DRAW) != VBO_OK) {
            fprintf(stderr, "ground VBO upload failed\n");
            goto fail_gl;
        }
    }
    if (vao_bind_attributes(&ctx->ground_vao, &ctx->ground_vbo, attrs, 1u,
                            3u * sizeof(float)) != VAO_OK) {
        fprintf(stderr, "ground VAO bind failed\n");
        goto fail_gl;
    }

    /* ---- Lines VBO/VAO (dynamic, for debug + beam) ---- */
    if (vbo_create(&ctx->lines_vbo, &ctx->loader) != VBO_OK ||
        vao_create(&ctx->lines_vao, &ctx->loader) != VAO_OK) {
        fprintf(stderr, "lines VBO/VAO create failed\n");
        goto fail_gl;
    }
    {
        static const float lines_dummy[] = {0.0f, 0.0f, 0.0f};
        if (vbo_upload(&ctx->lines_vbo, GL_ARRAY_BUFFER, lines_dummy,
                       sizeof(lines_dummy), GL_DYNAMIC_DRAW) != VBO_OK) {
            fprintf(stderr, "lines VBO upload failed\n");
            goto fail_gl;
        }
    }
    if (vao_bind_attributes(&ctx->lines_vao, &ctx->lines_vbo, attrs, 1u,
                            3u * sizeof(float)) != VAO_OK) {
        fprintf(stderr, "lines VAO bind failed\n");
        goto fail_gl;
    }

    glEnable(GL_DEPTH_TEST);
    gl_check_("glEnable(GL_DEPTH_TEST)");
    SDL_GL_SetSwapInterval(1);
    gl_check_("gl_demo_init_ done");
    return 0;

fail_gl:
    shader_program_destroy(&ctx->program);
    SDL_GL_DeleteContext(ctx->gl);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
    return -1;
}

static void gl_demo_shutdown_(struct gl_demo_context *ctx) {
    if (!ctx) return;
    vao_destroy(&ctx->lines_vao);
    vbo_destroy(&ctx->lines_vbo);
    vao_destroy(&ctx->ground_vao);
    vbo_destroy(&ctx->ground_vbo);
    vao_destroy(&ctx->sphere_vao);
    vbo_destroy(&ctx->sphere_vbo);
    vao_destroy(&ctx->cube_vao);
    vbo_destroy(&ctx->cube_vbo);
    shader_program_destroy(&ctx->program);
    if (ctx->gl)     SDL_GL_DeleteContext(ctx->gl);
    if (ctx->window) SDL_DestroyWindow(ctx->window);
    SDL_Quit();
    *ctx = (struct gl_demo_context){0};
}

/* ------------------------------------------------------------------ */
/*  Entity management (from p008)                                     */
/* ------------------------------------------------------------------ */

static int entity_find_(const struct entity_view *entities, size_t count, uint32_t id) {
    for (size_t i = 0u; i < count; ++i) {
        if (entities[i].entity_id == id) return (int)i;
    }
    return -1;
}

static int ensure_entity_cap_(struct entity_view **entities, size_t *cap, size_t needed) {
    if (!entities || !cap) return 0;
    if (needed <= *cap) return 1;
    size_t new_cap = (*cap == 0u) ? 16u : *cap;
    while (new_cap < needed) new_cap *= 2u;
    struct entity_view *p = (struct entity_view *)realloc(*entities, new_cap * sizeof(**entities));
    if (!p) return 0;
    *entities = p;
    *cap = new_cap;
    return 1;
}

static int add_entity_(struct entity_view **entities, size_t *count, size_t *cap,
                       uint32_t entity_id, uint16_t owner_client_id,
                       double recv_time_s, vec3_t pos, quat_t rot) {
    if (!entities || !count || !cap) return 0;
    if (!ensure_entity_cap_(entities, cap, *count + 1u)) return 0;

    struct entity_view *e = &(*entities)[*count];
    *e = (struct entity_view){0};
    e->entity_id = entity_id;
    e->phys_body = UINT32_MAX;
    e->owner_client_id = owner_client_id;
    e->shape_type = 0; /* box for now */
    e->scale = 1.0f;
    fr_pose_interpolator_reset(&e->pose);
    (void)fr_pose_interpolator_push(&e->pose, recv_time_s, pos, rot);
    *count += 1u;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Network message handlers (from p008)                              */
/* ------------------------------------------------------------------ */

static int handle_spawn_(struct entity_view **entities, size_t *count, size_t *cap,
                         const net_repl_spawn_t *sp, double recv_time_s,
                         uint16_t *io_self_owner, uint32_t *io_self_entity) {
    if (!entities || !count || !cap || !sp) return 0;
    if (entity_find_(*entities, *count, sp->entity_id) >= 0) return 1;

    net_qvec3_mm_t qpos;
    qpos.x_mm = sp->pos_mm.x_mm;
    qpos.y_mm = sp->pos_mm.y_mm;
    qpos.z_mm = sp->pos_mm.z_mm;
    qpos._magic = 0x4D4D3351u;

    vec3_t pos = {0};
    (void)net_dequantize_vec3_mm(qpos, &pos);
    const quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    if (!add_entity_(entities, count, cap, sp->entity_id, sp->owner_client_id, recv_time_s, pos, rot))
        return 0;

    if (io_self_owner && *io_self_owner == UINT16_MAX) {
        *io_self_owner = sp->owner_client_id;
        if (io_self_entity) *io_self_entity = sp->entity_id;
    }
    if (io_self_owner && io_self_entity && sp->owner_client_id == *io_self_owner)
        *io_self_entity = sp->entity_id;

    return 1;
}

static int handle_spawn_batch_(struct entity_view **entities, size_t *count, size_t *cap,
                               const net_repl_spawn_batch_entry_t *entries, uint16_t entry_count,
                               double recv_time_s,
                               uint16_t *io_self_owner, uint32_t *io_self_entity) {
    if (!entities || !count || !cap || !entries) return 0;

    for (uint16_t i = 0u; i < entry_count; ++i) {
        const net_repl_spawn_batch_entry_t *e = &entries[i];
        if (entity_find_(*entities, *count, e->entity_id) >= 0) continue;

        net_qvec3_mm_t qpos;
        qpos.x_mm = e->pos_mm.x_mm;
        qpos.y_mm = e->pos_mm.y_mm;
        qpos.z_mm = e->pos_mm.z_mm;
        qpos._magic = 0x4D4D3351u;

        vec3_t pos = {0};
        (void)net_dequantize_vec3_mm(qpos, &pos);
        const quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

        if (!add_entity_(entities, count, cap, e->entity_id, e->owner_client_id, recv_time_s, pos, rot))
            return 0;

        if (io_self_owner && *io_self_owner == UINT16_MAX) {
            *io_self_owner = e->owner_client_id;
            if (io_self_entity) *io_self_entity = e->entity_id;
        }
        if (io_self_owner && io_self_entity && e->owner_client_id == *io_self_owner)
            *io_self_entity = e->entity_id;
    }
    return 1;
}

static int handle_state_cube_(struct entity_view **entities, size_t *count, size_t *cap,
                              const net_repl_state_cube_t *st, double recv_time_s,
                              fr_debug_lines_t *correction_lines) {
    if (!entities || !count || !cap || !st) return 0;

    net_qvec3_mm_t qpos;
    qpos.x_mm = st->pos_mm.x_mm;
    qpos.y_mm = st->pos_mm.y_mm;
    qpos.z_mm = st->pos_mm.z_mm;
    qpos._magic = 0x4D4D3351u;

    vec3_t pos = {0};
    if (net_dequantize_vec3_mm(qpos, &pos) != NET_QUANT_OK) return 1;

    net_qquat_snorm16_t qrot;
    qrot.x = st->rot_snorm16.x;
    qrot.y = st->rot_snorm16.y;
    qrot.z = st->rot_snorm16.z;
    qrot.w = st->rot_snorm16.w;
    qrot._magic = 0x4E513136u;

    quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    (void)net_dequantize_quat_snorm16(qrot, &rot);

    int idx = entity_find_(*entities, *count, st->entity_id);
    if (idx < 0) {
        if (!add_entity_(entities, count, cap, st->entity_id, 0u, recv_time_s, pos, rot))
            return 0;
        return 1;
    }

    /* Generate debug correction lines when error exceeds threshold. */
    if (correction_lines) {
        vec3_t est_pos = {0};
        quat_t est_rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
        if (fr_pose_interpolator_sample(&(*entities)[idx].pose, recv_time_s, 1e-6f, &est_pos, &est_rot)) {
            const vec3_t dp = vec3_sub(pos, est_pos);
            const float pos_err = vec3_magnitude(dp);

            quat_t qa = quat_normalize_safe(est_rot, 1e-6f);
            quat_t qb = quat_normalize_safe(rot, 1e-6f);
            float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
            if (dot < 0.0f) dot = -dot;
            if (dot > 1.0f) dot = 1.0f;
            const float rot_err_rad = 2.0f * acosf(dot);

            const float pos_threshold = 0.005f;
            const float rot_threshold = 0.02f;
            if (pos_err > pos_threshold || rot_err_rad > rot_threshold) {
                vec3_t verts[16];
                const size_t n = fr_debug_correction_lines_cube(est_pos, est_rot, pos, rot, 0.125f, verts, 16u);
                for (size_t vi = 0u; vi + 1u < n; vi += 2u) {
                    (void)fr_debug_lines_add(correction_lines, verts[vi], verts[vi + 1u], recv_time_s, 0.35);
                }
            }
        }
    }

    (void)fr_pose_interpolator_push(&(*entities)[idx].pose, recv_time_s, pos, rot);
    return 1;
}

/** Return true if sequence 'a' is newer than 'b' (uint16 wrapping). */
static int seq_newer_(uint16_t a, uint16_t b) {
    return (int16_t)(a - b) > 0;
}

/** Context passed to the client-side spawn callback so it can map
 *  the newly created physics body index back to the entity system. */
struct client_spawn_ctx {
    struct entity_view **entities;
    size_t              *count;
};

/** Spawn callback invoked by phys_cmd_drain on the tick fiber.
 *  user_tag carries the entity_id so we can write back phys_body. */
static void client_spawn_cb_(uint32_t body_index, uint64_t user_tag,
                              void *user) {
    struct client_spawn_ctx *ctx = (struct client_spawn_ctx *)user;
    if (!ctx || !ctx->entities || !ctx->count) return;
    const uint32_t eid = (uint32_t)(user_tag & 0xFFFFFFFFu);
    int idx = entity_find_(*ctx->entities, *ctx->count, eid);
    if (idx >= 0) {
        (*ctx->entities)[idx].phys_body = body_index;
    }
}

static int handle_body_spawn_(struct entity_view **entities, size_t *count,
                              size_t *cap, const net_repl_body_spawn_t *sp,
                              double recv_time_s,
                              fr_topic_channel_t *cmd_channel) {
    if (!entities || !count || !cap || !sp) return 0;

    /* Use body_id as entity_id. */
    const uint32_t eid = (uint32_t)sp->body_id;

    /* Dequantize position and half-extents. */
    const float px = (float)sp->pos_mm.x_mm * 0.001f;
    const float py = (float)sp->pos_mm.y_mm * 0.001f;
    const float pz = (float)sp->pos_mm.z_mm * 0.001f;
    const float hx = (float)sp->half_x_mm * 0.001f;
    const float hy = (float)sp->half_y_mm * 0.001f;
    const float hz = (float)sp->half_z_mm * 0.001f;

    /* If we already know this body, just update metadata. */
    int idx = entity_find_(*entities, *count, eid);
    if (idx >= 0) {
        struct entity_view *e = &(*entities)[idx];
        e->shape_type  = sp->shape_type;
        e->flags       = sp->flags;
        e->color_seed  = sp->color_seed;
        e->half_ext[0] = hx;
        e->half_ext[1] = hy;
        e->half_ext[2] = hz;
        return 1;
    }

    vec3_t pos = {px, py, pz};
    quat_t rot = {sp->rot_x, sp->rot_y, sp->rot_z, sp->rot_w};

    /* Create entity in entity system first (phys_body = UINT32_MAX). */
    if (!add_entity_(entities, count, cap, eid, 0u, recv_time_s, pos, rot))
        return 0;

    struct entity_view *e = &(*entities)[*count - 1u];
    e->shape_type  = sp->shape_type;
    e->flags       = sp->flags;
    e->color_seed  = sp->color_seed;
    e->half_ext[0] = hx;
    e->half_ext[1] = hy;
    e->half_ext[2] = hz;

    /* Push a SPAWN_BODY command to the physics command queue.  The tick
     * fiber will drain it, create the body, and invoke client_spawn_cb_
     * to write back the phys_body index on the entity. */
    if (cmd_channel) {
        phys_cmd_spawn_body_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.position    = (phys_vec3_t){px, py, pz};
        cmd.orientation = (phys_quat_t){sp->rot_x, sp->rot_y,
                                        sp->rot_z, sp->rot_w};
        cmd.linear_vel  = (phys_vec3_t){0, 0, 0};
        cmd.flags       = sp->flags;

        /* Compute mass from volume * default density (500 kg/m³). */
        if (!(sp->flags & PHYS_BODY_FLAG_KINEMATIC) &&
            !(sp->flags & PHYS_BODY_FLAG_STATIC)) {
            float vol = (2.0f * hx) * (2.0f * hy) * (2.0f * hz);
            float mass = vol * 500.0f;
            if (mass < 0.1f) mass = 0.1f;
            cmd.mass = mass;
        }

        /* Shape. */
        if (sp->shape_type == 0) {
            cmd.shape = PHYS_CMD_SHAPE_BOX;
            cmd.shape_data.box_half = (phys_vec3_t){hx, hy, hz};
        } else if (sp->shape_type == 1) {
            cmd.shape = PHYS_CMD_SHAPE_SPHERE;
            cmd.shape_data.sphere_r = hx;
        }

        /* Encode entity_id in user_tag so the spawn callback can map it. */
        cmd.user_tag = (uint64_t)eid;

        phys_cmd_push(cmd_channel, PHYS_CMD_SPAWN_BODY,
                      &cmd, sizeof(cmd));
    }

    return 1;
}

static int handle_body_state_(struct entity_view **entities, size_t *count,
                              size_t *cap, const net_repl_body_state_t *st,
                              double recv_time_s,
                              fr_debug_lines_t *correction_lines,
                              fr_topic_channel_t *cmd_channel,
                              uint32_t rtt_ms) {
    if (!entities || !count || !cap || !st) return 0;

    const uint32_t eid = (uint32_t)st->body_id;

    /* Dequantize position, linear velocity, and angular velocity. */
    const float px = (float)st->pos_mm.x_mm * 0.001f;
    const float py = (float)st->pos_mm.y_mm * 0.001f;
    const float pz = (float)st->pos_mm.z_mm * 0.001f;
    const float vx = (float)st->vel_x_mm_s * 0.001f;
    const float vy = (float)st->vel_y_mm_s * 0.001f;
    const float vz = (float)st->vel_z_mm_s * 0.001f;
    const float ax = (float)st->ang_x_mrad_s * 0.001f;
    const float ay = (float)st->ang_y_mrad_s * 0.001f;
    const float az = (float)st->ang_z_mrad_s * 0.001f;

    vec3_t pos = {px, py, pz};
    quat_t rot = {st->rot_x, st->rot_y, st->rot_z, st->rot_w};

    int idx = entity_find_(*entities, *count, eid);
    if (idx < 0) {
        /* Body state arrived before spawn (unreliable ordering).
         * Create a placeholder entity. */
        if (!add_entity_(entities, count, cap, eid, 0u, recv_time_s, pos, rot))
            return 0;
        return 1;
    }

    struct entity_view *e = &(*entities)[idx];

    /* Drop stale out-of-order packets — only apply if this server_tick
     * is newer than the last one we accepted for this body. */
    if (e->last_server_tick != 0u &&
        !seq_newer_(st->server_tick, e->last_server_tick)) {
        return 1; /* silently discard */
    }
    e->last_server_tick = st->server_tick;

    /* Push authoritative correction through the command queue so it
     * is applied on the tick fiber -- no direct mutation from main.
     *
     * Extrapolate the server position forward by velocity to compensate
     * for network latency.  Uses measured RTT / 2 (one-way estimate)
     * with a 16ms minimum floor.  We do NOT add gravity here because
     * the client's integration stage applies gravity on every tick —
     * adding it to the extrapolation too would double-count it. */
    if (cmd_channel && e->phys_body != UINT32_MAX) {
        /* One-way latency ≈ RTT/2, floor at one tick (16 ms). */
        const float lat_s = (rtt_ms > 0u)
            ? (float)rtt_ms * 0.0005f   /* RTT/2 in seconds */
            : 0.016f;                    /* fallback: 1 tick */
        const float ex = px + vx * lat_s;
        const float ey = py + vy * lat_s;
        const float ez = pz + vz * lat_s;

        phys_cmd_set_state_t cmd;
        cmd.body_index  = e->phys_body;
        cmd.position    = (phys_vec3_t){ex, ey, ez};
        cmd.orientation = (phys_quat_t){st->rot_x, st->rot_y,
                                        st->rot_z, st->rot_w};
        cmd.linear_vel  = (phys_vec3_t){vx, vy, vz};
        cmd.angular_vel = (phys_vec3_t){ax, ay, az};
        phys_cmd_push(cmd_channel, PHYS_CMD_SET_STATE,
                      &cmd, sizeof(cmd));
    }

    /* Still push to the pose interpolator for debug correction lines. */
    if (correction_lines) {
        vec3_t est_pos = {0};
        quat_t est_rot = {0.0f, 0.0f, 0.0f, 1.0f};
        if (fr_pose_interpolator_sample(&e->pose, recv_time_s,
                                        1e-6f, &est_pos, &est_rot)) {
            const vec3_t dp = vec3_sub(pos, est_pos);
            const float pos_err = vec3_magnitude(dp);
            const float pos_threshold = 0.005f;
            if (pos_err > pos_threshold) {
                vec3_t verts[16];
                const size_t n = fr_debug_correction_lines_cube(
                    est_pos, est_rot, pos, rot, 0.125f, verts, 16u);
                for (size_t vi = 0u; vi + 1u < n; vi += 2u) {
                    (void)fr_debug_lines_add(correction_lines,
                                             verts[vi], verts[vi + 1u],
                                             recv_time_s, 0.35);
                }
            }
        }
    }

    (void)fr_pose_interpolator_push(&e->pose, recv_time_s, pos, rot);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Quantize yaw/pitch to snorm16 range                              */
/* ------------------------------------------------------------------ */

/** Map a float angle in [-PI, PI] to snorm16 [-32767, 32767]. */
static int16_t angle_to_snorm16_(float radians) {
    float norm = radians / FERRUM_PI;
    if (norm > 1.0f) norm = 1.0f;
    if (norm < -1.0f) norm = -1.0f;
    return (int16_t)(norm * 32767.0f);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 3) {
        usage_(argv[0]);
        return 2;
    }

    const char *server_ip_s = argv[1];
    long port_l = strtol(argv[2], NULL, 10);
    if (port_l <= 0 || port_l > 65535) {
        usage_(argv[0]);
        return 2;
    }

    uint8_t ip[4];
    if (!parse_ipv4_dotted_(server_ip_s, ip)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", server_ip_s);
        return 2;
    }

    /* ---- Network setup ---- */
    net_udp_addr_t server_addr;
    if (net_udp_addr_ipv4(&server_addr, ip[0], ip[1], ip[2], ip[3], (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build server address\n");
        return 1;
    }

    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }

    (void)net_udp_socket_set_recv_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_send_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_nonblocking(&sock, 1);

    if (net_udp_socket_connect(&sock, &server_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to connect\n");
        net_udp_socket_close(&sock);
        return 1;
    }

    net_rudp_peer_t peer;
    const size_t send_slots_count = (size_t)NET_RUDP_SEND_SLOTS_DEFAULT;
    const size_t send_slots_bytes = net_rudp_send_slot_storage_size(send_slots_count);
    net_rudp_send_slot_t *send_slots = (net_rudp_send_slot_t *)calloc(1u, send_slots_bytes);
    if (!send_slots) {
        fprintf(stderr, "Failed to allocate RUDP send slots\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, send_slots, send_slots_count);

    uint32_t rng = (uint32_t)(0xA5A5A5A5u ^ (uint32_t)getpid());

    /* Send JOIN. */
    net_repl_join_t join;
    join.client_nonce = xorshift32_(&rng);
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    if (net_repl_join_encode(&join, join_payload, sizeof(join_payload)) != NET_REPL_OK) {
        fprintf(stderr, "Failed to encode JOIN\n");
        free(send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }

    uint16_t join_seq = 0u;
    if (net_rudp_peer_send_reliable(&peer, &sock, &server_addr, now_ms_(),
                                    NET_REPL_SCHEMA_JOIN, join_payload,
                                    sizeof(join_payload), &join_seq) != NET_RUDP_OK) {
        fprintf(stderr, "Failed to send JOIN\n");
        free(send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }
    (void)join_seq;

    /* ---- GL init ---- */
    const int win_w = 1280;
    const int win_h = 720;
    struct gl_demo_context gl = {0};
    if (gl_demo_init_(&gl, win_w, win_h) != 0) {
        free(send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ---- Debug correction lines ---- */
    fr_debug_line_t correction_line_storage[512];
    fr_debug_lines_t correction_lines;
    fr_debug_lines_init(&correction_lines, correction_line_storage, 512u);
    vec3_t correction_vertices[1024];

    /* ---- Entity state ---- */
    struct entity_view *entities = NULL;
    size_t entity_count = 0u;
    size_t entity_cap = 0u;

    uint16_t self_owner_client_id = UINT16_MAX;
    uint32_t self_entity_id = 0u;

    /* ---- Client-side physics (prediction) ---- */
    job_system_t phys_job_sys;
    {
        job_system_create_status_t js = job_system_create(
            &phys_job_sys, 2u, 2048u, 1u << 18, 1024, 0);
        if (js != JOB_CREATE_OK) {
            fprintf(stderr, "Failed to create client job system\n");
            gl_demo_shutdown_(&gl);
            free(send_slots);
            net_udp_socket_close(&sock);
            return 1;
        }
        if (job_system_start(&phys_job_sys) != 0) {
            fprintf(stderr, "Failed to start client job system\n");
            job_system_shutdown(&phys_job_sys);
            gl_demo_shutdown_(&gl);
            free(send_slots);
            net_udp_socket_close(&sock);
            return 1;
        }
    }

    phys_world_t client_world;
    {
        phys_world_config_t wcfg = phys_world_config_default();
        wcfg.max_bodies = CLIENT_MAX_BODIES;
        wcfg.gravity = (phys_vec3_t){0.0f, -9.81f, 0.0f};
        wcfg.fixed_dt = 1.0f / 60.0f;
        if (phys_world_init(&client_world, &wcfg) != 0) {
            fprintf(stderr, "Failed to init client physics world\n");
            job_system_shutdown(&phys_job_sys);
            gl_demo_shutdown_(&gl);
            free(send_slots);
            net_udp_socket_close(&sock);
            return 1;
        }

        /* Client runs prediction mode: integrate + gravity only,
         * no narrowphase collision response.  Server corrections
         * override local state for colliding bodies. */
        client_world.prediction_mode = 1;

        /* Create ground plane matching server. */
        uint32_t ground = phys_world_create_body(&client_world);
        phys_body_t *gb = phys_world_get_body(&client_world, ground);
        gb->position = (phys_vec3_t){0.0f, -0.5f, 0.0f};
        gb->flags = PHYS_BODY_FLAG_STATIC;
        gb->inv_mass = 0.0f;
        phys_world_set_box_collider(&client_world, ground,
            (phys_vec3_t){200.0f, 0.5f, 200.0f},
            (phys_vec3_t){0.0f, 0.0f, 0.0f},
            (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});

        /* Copy ground to next buffer. */
        phys_body_t *gbn = phys_body_pool_get_next(&client_world.body_pool, ground);
        if (gbn) { *gbn = *gb; }
    }

    phys_job_context_t phys_jobs;
    phys_job_context_init(&phys_jobs, &phys_job_sys);

    /* Command channel for client physics corrections from the network.
     * BODY_SPAWN pushes to cmd_channel (drained before tick).
     * BODY_STATE pushes to correction_channel (drained after tick). */
    fr_topic_channel_config_t client_cmd_cfg;
    memset(&client_cmd_cfg, 0, sizeof(client_cmd_cfg));
    client_cmd_cfg.capacity = 8192u;
    fr_topic_channel_t *client_cmds = fr_topic_channel_create(&client_cmd_cfg);

    /* Correction channel needs more capacity — the server sends state
     * for every awake body every tick, and multiple ticks of corrections
     * can accumulate before the client's tick fiber drains them. */
    fr_topic_channel_config_t corr_cfg;
    memset(&corr_cfg, 0, sizeof(corr_cfg));
    corr_cfg.capacity = 65536u;
    fr_topic_channel_t *client_corrections = fr_topic_channel_create(&corr_cfg);

    /* Spawn callback context — the tick fiber uses this to map newly
     * created physics body indices back to entity_view entries.
     * The pointers are kept current because entities/entity_count are
     * stable across ticks (main thread only mutates between ticks). */
    struct client_spawn_ctx spawn_ctx;
    spawn_ctx.entities = &entities;
    spawn_ctx.count    = &entity_count;

    phys_tick_runner_t tick_runner;
    phys_tick_runner_init(&tick_runner, &client_world, &phys_jobs,
                          client_cmds, client_corrections,
                          client_spawn_cb_, &spawn_ctx);

    /* Fixed timestep tracking for client physics. */
    const uint64_t client_tick_ms = 16u; /* ~60 Hz */
    uint64_t client_next_tick = now_ms_();

    /* ---- FPS camera ---- */
    demo_camera_t cam;
    demo_camera_init(&cam);

    /* ---- Input state ---- */
    uint8_t wasd_flags = 0u;    /* bit0=W, bit1=A, bit2=S, bit3=D */
    int fire_held = 0;
    int e_held = 0;
    int e_just_released = 0;
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;
    uint16_t input_event_id = 0u;

    /* Capture mouse for FPS look. */
    SDL_SetRelativeMouseMode(SDL_TRUE);

    uint64_t last_frame_ns = now_ns_();
    uint64_t next_keepalive_ms = now_ms_();
    uint64_t next_diag_ms = now_ms_();
    uint8_t rx_packet[NET_RUDP_MAX_PACKET_SIZE];

    /* ---- Main loop ---- */
    int running = 1;
    while (running) {
        const uint64_t frame_ns = now_ns_();
        const float dt_s = (float)((double)(frame_ns - last_frame_ns) / 1000000000.0);
        last_frame_ns = frame_ns;
        const uint64_t now_ms = frame_ns / 1000000ull;
        const double now_s = (double)frame_ns / 1000000000.0;

        /* ---- (a) Poll SDL events ---- */
        mouse_dx = 0.0f;
        mouse_dy = 0.0f;
        e_just_released = 0;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
                break;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
                break;
            }
            if (ev.type == SDL_MOUSEMOTION) {
                mouse_dx += (float)ev.motion.xrel;
                mouse_dy += (float)ev.motion.yrel;
            }
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                switch (ev.key.keysym.sym) {
                    case SDLK_w: wasd_flags |= DEMO_MOVE_W; break;
                    case SDLK_a: wasd_flags |= DEMO_MOVE_A; break;
                    case SDLK_s: wasd_flags |= DEMO_MOVE_S; break;
                    case SDLK_d: wasd_flags |= DEMO_MOVE_D; break;
                    case SDLK_e: e_held = 1; break;
                    default: break;
                }
            }
            if (ev.type == SDL_KEYUP) {
                switch (ev.key.keysym.sym) {
                    case SDLK_w: wasd_flags &= (uint8_t)~DEMO_MOVE_W; break;
                    case SDLK_a: wasd_flags &= (uint8_t)~DEMO_MOVE_A; break;
                    case SDLK_s: wasd_flags &= (uint8_t)~DEMO_MOVE_S; break;
                    case SDLK_d: wasd_flags &= (uint8_t)~DEMO_MOVE_D; break;
                    case SDLK_e:
                        if (e_held) { e_just_released = 1; }
                        e_held = 0;
                        break;
                    default: break;
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                fire_held = 1;
            }
            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                fire_held = 0;
            }
        }

        if (!running) break;

        /* ---- (b) Update camera ---- */
        demo_camera_update(&cam, mouse_dx, mouse_dy, wasd_flags, dt_s);

        /* ---- (c) Build and send INPUT_MOVE ---- */
        {
            demo_input_move_t move_msg;
            memset(&move_msg, 0, sizeof(move_msg));
            move_msg.event_id = input_event_id++;
            move_msg.yaw_snorm16 = angle_to_snorm16_(cam.yaw);
            move_msg.pitch_snorm16 = angle_to_snorm16_(cam.pitch);
            move_msg.move_flags = wasd_flags;

            uint8_t action = 0u;
            if (fire_held) action |= DEMO_ACTION_FIRE;
            if (e_just_released) action |= DEMO_ACTION_SPAWN_BOX;
            move_msg.action_flags = action;

            uint8_t move_payload[DEMO_INPUT_MOVE_PAYLOAD_SIZE];
            if (demo_input_move_encode(&move_msg, move_payload, sizeof(move_payload)) == NET_REPL_OK) {
                (void)net_rudp_peer_send_unreliable(&peer, &sock, &server_addr, now_ms,
                                                    NET_REPL_SCHEMA_INPUT_MOVE,
                                                    move_payload, sizeof(move_payload));
            }
        }

        /* ---- (d) If E just released: send INPUT_SPAWN ---- */
        if (e_just_released) {
            demo_input_spawn_t spawn_msg;
            memset(&spawn_msg, 0, sizeof(spawn_msg));
            spawn_msg.event_id = input_event_id++;
            /* Random half-extents in mm (100..500 mm = 0.1..0.5 m). */
            spawn_msg.half_x_mm = (uint16_t)(300u + (xorshift32_(&rng) % 700u));
            spawn_msg.half_y_mm = (uint16_t)(300u + (xorshift32_(&rng) % 700u));
            spawn_msg.half_z_mm = (uint16_t)(300u + (xorshift32_(&rng) % 700u));
            spawn_msg.color_seed = xorshift32_(&rng);

            uint8_t spawn_payload[DEMO_INPUT_SPAWN_PAYLOAD_SIZE];
            if (demo_input_spawn_encode(&spawn_msg, spawn_payload, sizeof(spawn_payload)) == NET_REPL_OK) {
                uint16_t seq = 0u;
                (void)net_rudp_peer_send_reliable(&peer, &sock, &server_addr, now_ms,
                                                  NET_REPL_SCHEMA_INPUT_SPAWN,
                                                  spawn_payload, sizeof(spawn_payload), &seq);
            }
        }

        /* ---- Keepalive / resend ---- */
        if (now_ms >= next_keepalive_ms) {
            (void)net_rudp_peer_send_unreliable(&peer, &sock, &server_addr, now_ms,
                                                NET_REPL_SCHEMA_JOIN,
                                                join_payload, sizeof(join_payload));
            next_keepalive_ms = now_ms + 100u;
        }
        (void)net_rudp_peer_tick_resend(&peer, &sock, &server_addr, now_ms);

        /* ---- (e) Receive network messages ---- */
        for (;;) {
            size_t rx_size = 0u;
            const int rrc = net_udp_socket_recv(&sock, rx_packet, sizeof(rx_packet), &rx_size);
            if (rrc == NET_UDP_SOCKET_EMPTY) break;
            if (rrc != NET_UDP_SOCKET_OK) break;

            uint8_t reliable = 0u;
            uint16_t schema_id = 0u;
            uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
            size_t payload_size = 0u;
            if (net_rudp_peer_receive(&peer, rx_packet, rx_size, now_ms,
                                      &reliable, &schema_id,
                                      payload, sizeof(payload), &payload_size) != NET_RUDP_OK) {
                continue;
            }
            (void)reliable;
            const double recv_time_s = now_s_();

            if (schema_id == NET_REPL_SCHEMA_SPAWN) {
                net_repl_spawn_t sp;
                if (net_repl_spawn_decode(&sp, payload, payload_size) == NET_REPL_OK) {
                    (void)handle_spawn_(&entities, &entity_count, &entity_cap,
                                        &sp, recv_time_s, &self_owner_client_id, &self_entity_id);
                }
            } else if (schema_id == NET_REPL_SCHEMA_SPAWN_BATCH) {
                net_repl_spawn_batch_entry_t entries[64];
                uint16_t batch_count = 0u;
                uint16_t tick = 0u;
                if (net_repl_spawn_batch_decode(&tick, entries, 64u, &batch_count,
                                                payload, payload_size) == NET_REPL_OK) {
                    (void)tick;
                    (void)handle_spawn_batch_(&entities, &entity_count, &entity_cap,
                                              entries, batch_count, recv_time_s,
                                              &self_owner_client_id, &self_entity_id);
                }
            } else if (schema_id == NET_REPL_SCHEMA_STATE_CUBE) {
                net_repl_state_cube_t st;
                if (net_repl_state_cube_decode(&st, payload, payload_size) == NET_REPL_OK) {
                    (void)handle_state_cube_(&entities, &entity_count, &entity_cap,
                                             &st, recv_time_s, &correction_lines);
                }
            } else if (schema_id == NET_REPL_SCHEMA_BODY_SPAWN) {
                net_repl_body_spawn_t sp;
                if (net_repl_body_spawn_decode(&sp, payload, payload_size) == NET_REPL_OK) {
                    (void)handle_body_spawn_(&entities, &entity_count, &entity_cap,
                                             &sp, recv_time_s, client_cmds);
                }
            } else if (schema_id == NET_REPL_SCHEMA_BODY_STATE) {
                net_repl_body_state_t st;
                if (net_repl_body_state_decode(&st, payload, payload_size) == NET_REPL_OK) {
                    (void)handle_body_state_(&entities, &entity_count, &entity_cap,
                                             &st, recv_time_s, &correction_lines,
                                             client_corrections,
                                             peer.smoothed_rtt_ms);
                }
            } else if (schema_id == NET_REPL_SCHEMA_WELCOME) {
                net_repl_welcome_t w;
                (void)net_repl_welcome_decode(&w, payload, payload_size);
                fprintf(stderr, "WELCOME: expected_entities=%u tick_hz=%u\n",
                        (unsigned)w.expected_entities, (unsigned)w.tick_hz);
            }
        }

        /* ---- Diagnostics ---- */
        if (now_ms >= next_diag_ms) {
            fprintf(stderr, "diag: entities=%zu bodies=%u self_entity=%u cam=(%.1f,%.1f,%.1f)\n",
                    entity_count,
                    (unsigned)phys_world_body_count(&client_world),
                    (unsigned)self_entity_id,
                    (double)cam.position.x, (double)cam.position.y, (double)cam.position.z);
            next_diag_ms = now_ms + 1000u;
        }

        /* ---- Client physics tick (non-blocking) ---- */
        if (now_ms >= client_next_tick) {
            /* Cap catch-up to 3 ticks. */
            if (now_ms - client_next_tick > client_tick_ms * 3u) {
                client_next_tick = now_ms;
            }
            if (phys_tick_runner_done(&tick_runner)) {
                phys_tick_runner_consume(&tick_runner);
                phys_tick_runner_kick(&tick_runner);
            }
            client_next_tick += client_tick_ms;
        }

        /* ---- (f) Render ---- */
        glViewport(0, 0, win_w, win_h);
        glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl_check_("frame clear");

        if (shader_program_bind(&gl.program) == SHADER_PROGRAM_OK) {
            /* Perspective and view matrices. */
            mat4_t proj;
            const float aspect = (win_h > 0) ? ((float)win_w / (float)win_h) : 1.0f;
            if (mat4_perspective(60.0f * (FERRUM_PI / 180.0f), aspect, 0.1f, 500.0f, &proj) != 0)
                proj = mat4_identity();

            mat4_t view;
            demo_camera_view_matrix(&cam, &view);

            const mat4_t vp = mat4_mul(proj, view);

            /* Render slightly behind to smooth latency/loss. */
            const double render_time_s = now_s - 0.20;

            /* ---- Render entities ---- */
            for (size_t i = 0u; i < entity_count; ++i) {
                struct entity_view *e = &entities[i];
                vec3_t pos;
                quat_t rot;

                /* Prefer reading from the client physics world (last
                 * completed tick).  Fall back to the pose interpolator
                 * for entities that don't have a local body yet. */
                int have_pose = 0;
                if (e->phys_body != UINT32_MAX) {
                    const phys_body_t *b = phys_world_get_body(
                        &client_world, e->phys_body);
                    if (b) {
                        pos = (vec3_t){b->position.x, b->position.y,
                                       b->position.z};
                        rot = (quat_t){b->orientation.x, b->orientation.y,
                                       b->orientation.z, b->orientation.w};
                        have_pose = 1;
                    }
                }
                if (!have_pose) {
                    if (!fr_pose_interpolator_sample(&e->pose, render_time_s,
                                                     1e-6f, &pos, &rot))
                        continue;
                }

                /* Use half-extents for non-uniform scale if available,
                 * otherwise fall back to uniform scale. */
                float sx = (e->half_ext[0] > 0.0f) ? e->half_ext[0] : e->scale;
                float sy = (e->half_ext[1] > 0.0f) ? e->half_ext[1] : e->scale;
                float sz = (e->half_ext[2] > 0.0f) ? e->half_ext[2] : e->scale;

                const mat4_t t_mat = mat4_translation(pos.x, pos.y, pos.z);
                const mat4_t r_mat = mat4_from_quat_(rot);
                const mat4_t s_mat = mat4_scaling(sx, sy, sz);
                const mat4_t model = mat4_mul(t_mat, mat4_mul(r_mat, s_mat));
                const mat4_t mvp = mat4_mul(vp, model);

                if (shader_uniform_set_mat4(&gl.uniforms, &gl.program, "u_mvp", mvp.m, 0u) != SHADER_UNIFORM_OK) {
                    fprintf(stderr, "shader_uniform_set_mat4 failed for u_mvp\n");
                }

                /* Derive color from color_seed if nonzero, else entity_id. */
                float rgb[3];
                if (e->color_seed != 0u) {
                    color_from_owner_((uint16_t)(e->color_seed & 0xFFFFu), rgb);
                } else {
                    color_from_owner_((uint16_t)(e->entity_id & 0xFFFFu), rgb);
                }
                if (e->entity_id == self_entity_id) {
                    rgb[0] = 1.0f; rgb[1] = 1.0f; rgb[2] = 1.0f;
                }
                /* Ensure minimum brightness. */
                float brightness = rgb[0] * 0.299f + rgb[1] * 0.587f + rgb[2] * 0.114f;
                if (brightness < 0.25f) {
                    rgb[0] += 0.3f; rgb[1] += 0.3f; rgb[2] += 0.3f;
                }
                if (shader_uniform_set_vec3(&gl.uniforms, &gl.program, "u_color", rgb) != SHADER_UNIFORM_OK) {
                    fprintf(stderr, "shader_uniform_set_vec3 failed for u_color\n");
                }

                /* All entities rendered as boxes for now. */
                glBindVertexArray(vao_handle(&gl.cube_vao));
                glDrawArrays(GL_TRIANGLES, 0, 36);
                gl_check_("entity draw");
            }

            /* ---- Render ground plane ---- */
            {
                const mat4_t ground_mvp = vp; /* Ground is at identity transform. */
                if (shader_uniform_set_mat4(&gl.uniforms, &gl.program, "u_mvp",
                                            ground_mvp.m, 0u) != SHADER_UNIFORM_OK) {
                    fprintf(stderr, "shader_uniform_set_mat4 failed for ground u_mvp\n");
                }
                const float ground_color[3] = {0.25f, 0.35f, 0.25f};
                (void)shader_uniform_set_vec3(&gl.uniforms, &gl.program, "u_color", ground_color);

                glBindVertexArray(vao_handle(&gl.ground_vao));
                glDrawArrays(GL_TRIANGLES, 0, 6); /* 2 triangles = 6 vertices */
                gl_check_("ground draw");
            }

            /* ---- Render debug correction lines (red) ---- */
            {
                size_t line_vertex_count = 0u;
                if (fr_debug_lines_collect_vertices(&correction_lines, now_s,
                                                    correction_vertices,
                                                    sizeof(correction_vertices) / sizeof(correction_vertices[0]),
                                                    &line_vertex_count) &&
                    line_vertex_count > 0u) {

                    (void)vbo_upload(&gl.lines_vbo, GL_ARRAY_BUFFER,
                                     correction_vertices,
                                     line_vertex_count * sizeof(correction_vertices[0]),
                                     GL_DYNAMIC_DRAW);

                    glBindVertexArray(vao_handle(&gl.lines_vao));
                    if (shader_uniform_set_mat4(&gl.uniforms, &gl.program, "u_mvp",
                                                vp.m, 0u) != SHADER_UNIFORM_OK) {
                        fprintf(stderr, "shader_uniform_set_mat4 failed (correction lines)\n");
                    }
                    const float red[3] = {1.0f, 0.0f, 0.0f};
                    (void)shader_uniform_set_vec3(&gl.uniforms, &gl.program, "u_color", red);

                    glDisable(GL_DEPTH_TEST);
                    glDrawArrays(GL_LINES, 0, (GLsizei)line_vertex_count);
                    glEnable(GL_DEPTH_TEST);
                    gl_check_("correction lines draw");
                }
            }

            /* ---- Render fire beam line (yellow) ---- */
            if (fire_held) {
                vec3_t beam_start = cam.position;
                vec3_t fwd = demo_camera_forward(&cam);
                /* Beam extends 50 units from camera position. */
                vec3_t beam_end = vec3_add(beam_start, vec3_scale(fwd, 50.0f));

                vec3_t beam_verts[2];
                beam_verts[0] = beam_start;
                beam_verts[1] = beam_end;

                (void)vbo_upload(&gl.lines_vbo, GL_ARRAY_BUFFER,
                                 beam_verts, sizeof(beam_verts), GL_DYNAMIC_DRAW);

                glBindVertexArray(vao_handle(&gl.lines_vao));
                if (shader_uniform_set_mat4(&gl.uniforms, &gl.program, "u_mvp",
                                            vp.m, 0u) != SHADER_UNIFORM_OK) {
                    fprintf(stderr, "shader_uniform_set_mat4 failed (beam)\n");
                }
                const float yellow[3] = {1.0f, 1.0f, 0.0f};
                (void)shader_uniform_set_vec3(&gl.uniforms, &gl.program, "u_color", yellow);

                glDisable(GL_DEPTH_TEST);
                glDrawArrays(GL_LINES, 0, 2);
                glEnable(GL_DEPTH_TEST);
                gl_check_("beam draw");
            }

            glBindVertexArray(0u);
        }

        SDL_GL_SwapWindow(gl.window);
        gl_check_("SDL_GL_SwapWindow");
    }

    /* ---- Cleanup ---- */
    phys_tick_runner_wait(&tick_runner);
    phys_tick_runner_destroy(&tick_runner);
    if (client_cmds) { fr_topic_channel_destroy(client_cmds); }
    if (client_corrections) { fr_topic_channel_destroy(client_corrections); }
    phys_job_context_destroy(&phys_jobs);
    phys_world_destroy(&client_world);
    job_system_shutdown(&phys_job_sys);
    free(entities);
    gl_demo_shutdown_(&gl);
    free(send_slots);
    net_udp_socket_close(&sock);
    return 0;
}
