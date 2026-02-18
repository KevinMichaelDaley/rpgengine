#define _POSIX_C_SOURCE 200809L

#include <glad/glad.h>
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

#include "ferrum/net/replication/interp/pose_interpolator.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/spawn_batch.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/replication/welcome.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/debug_correction_lines.h"
#include "ferrum/renderer/debug_lines.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"

struct gl_client_context {
    SDL_Window *window;
    SDL_GLContext gl;
    gl_loader_t loader;
    shader_program_t program;
    shader_uniform_cache_t uniforms;
    vbo_t vbo;
    vao_t vao;
    vbo_t lines_vbo;
    vao_t lines_vao;
};

struct entity_view {
    uint32_t entity_id;
    uint16_t owner_client_id;
    fr_pose_interpolator_t pose;
};

static const char *gl_error_str_(GLenum err) {
    switch (err) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "GL_UNKNOWN_ERROR";
    }
}

static void gl_check_(const char *where) {
    static int budget = 64;
    if (budget <= 0) {
        return;
    }
    for (;;) {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) {
            return;
        }
        if (budget > 0) {
            fprintf(stderr, "GL error at %s: 0x%04x (%s)\n", where ? where : "?", (unsigned)err, gl_error_str_(err));
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

static void sleep_ms_(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

static void usage_(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <server_ipv4> <port> <duration_ms> [--seed <u32>] [--headless]\n",
            argv0);
}

static int parse_ipv4_dotted_(const char *s, uint8_t out[4]) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!s) {
        return 0;
    }
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
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

static uint32_t xorshift32_(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rand_float01_(uint32_t *state) {
    const uint32_t x = xorshift32_(state);
    return (float)((double)x / 4294967295.0);
}

static void color_from_owner_(uint16_t owner, float out_rgb[3]) {
    const uint32_t h = (uint32_t)owner * 2654435761u;
    out_rgb[0] = (float)((h >> 0u) & 0xFFu) / 255.0f;
    out_rgb[1] = (float)((h >> 8u) & 0xFFu) / 255.0f;
    out_rgb[2] = (float)((h >> 16u) & 0xFFu) / 255.0f;
}

static void *sdl_get_proc_address_(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

static int gl_client_init_(struct gl_client_context *ctx, const char *title, int w, int h) {
    if (!ctx) {
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    typedef struct gl_attempt {
        int major;
        int minor;
        int profile_mask;
        int flags;
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
    for (size_t i = 0u; i < (sizeof(attempts) / sizeof(attempts[0])); ++i) {
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, attempts[i].major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, attempts[i].minor);
        if (attempts[i].profile_mask != 0) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, attempts[i].profile_mask);
        }
        if (attempts[i].flags != 0) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, attempts[i].flags);
        }
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        ctx->window = SDL_CreateWindow(title,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       w,
                                       h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (ctx->window == NULL) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return -1;
        }

        ctx->gl = SDL_GL_CreateContext(ctx->window);
        if (ctx->gl == NULL) {
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

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

        const char *gl_vendor = (const char *)glGetString(GL_VENDOR);
        const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
        const char *gl_version = (const char *)glGetString(GL_VERSION);
        const char *glsl_version = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
        fprintf(stderr,
            "GL: vendor=%s renderer=%s version=%s glsl=%s\n",
            gl_vendor ? gl_vendor : "(null)",
            gl_renderer ? gl_renderer : "(null)",
            gl_version ? gl_version : "(null)",
            glsl_version ? glsl_version : "(null)");
        gl_check_("post-glad");

    ctx->loader.get_proc_address = sdl_get_proc_address_;
    ctx->loader.user_data = NULL;

    const char *glsl_ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int glsl_major = 0;
    int glsl_minor = 0;
    if (glsl_ver != NULL) {
        (void)sscanf(glsl_ver, "%d.%d", &glsl_major, &glsl_minor);
    }

    const char *vs_src = NULL;
    const char *fs_src = NULL;
    if (glsl_major >= 3) {
        vs_src =
            "#version 330 core\n"
            "in vec3 in_pos;\n"
            "uniform mat4 u_mvp;\n"
            "void main() {\n"
            "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n"
            "}\n";
        fs_src =
            "#version 330 core\n"
            "uniform vec3 u_color;\n"
            "out vec4 out_color;\n"
            "void main() {\n"
            "    out_color = vec4(u_color, 1.0);\n"
            "}\n";
    } else if (glsl_major == 1 && glsl_minor >= 30) {
        vs_src =
            "#version 130\n"
            "in vec3 in_pos;\n"
            "uniform mat4 u_mvp;\n"
            "void main() {\n"
            "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n"
            "}\n";
        fs_src =
            "#version 130\n"
            "uniform vec3 u_color;\n"
            "out vec4 out_color;\n"
            "void main() {\n"
            "    out_color = vec4(u_color, 1.0);\n"
            "}\n";
    } else {
        vs_src =
            "#version 120\n"
            "attribute vec3 in_pos;\n"
            "uniform mat4 u_mvp;\n"
            "void main() {\n"
            "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n"
            "}\n";
        fs_src =
            "#version 120\n"
            "uniform vec3 u_color;\n"
            "void main() {\n"
            "    gl_FragColor = vec4(u_color, 1.0);\n"
            "}\n";
    }

    char log_buffer[1024] = {0};
    shader_program_status_t sp = shader_program_create(&ctx->program, &ctx->loader, vs_src, fs_src, log_buffer,
                                                       sizeof(log_buffer));
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

    if (vbo_create(&ctx->vbo, &ctx->loader) != VBO_OK) {
        fprintf(stderr, "vbo_create failed\n");
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    if (vao_create(&ctx->vao, &ctx->loader) != VAO_OK) {
        fprintf(stderr, "vao_create failed\n");
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    static const float cube_vertices[] = {
        /* +X */
        0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f,
        /* -X */
        -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
        -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f,
        /* +Y */
        -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
        /* -Y */
        -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        /* +Z */
        -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
        /* -Z */
        0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
        0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
    };

    if (vbo_upload(&ctx->vbo, GL_ARRAY_BUFFER, cube_vertices, sizeof(cube_vertices), GL_STATIC_DRAW) != VBO_OK) {
        fprintf(stderr, "vbo_upload failed\n");
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    const GLint pos_loc = glGetAttribLocation((GLuint)shader_program_handle(&ctx->program), "in_pos");
    if (pos_loc < 0) {
        fprintf(stderr, "glGetAttribLocation failed for in_pos\n");
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    vao_attribute_t attrs[1];
    attrs[0] = (vao_attribute_t){(uint32_t)pos_loc, 3, GL_FLOAT, 0u, 0u, 0u};
    if (vao_bind_attributes(&ctx->vao, &ctx->vbo, attrs, 1u, 3u * sizeof(float)) != VAO_OK) {
        fprintf(stderr, "vao_bind_attributes failed\n");
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    if (vbo_create(&ctx->lines_vbo, &ctx->loader) != VBO_OK) {
        fprintf(stderr, "vbo_create failed (lines_vbo)\n");
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    if (vao_create(&ctx->lines_vao, &ctx->loader) != VAO_OK) {
        fprintf(stderr, "vao_create failed (lines_vao)\n");
        vbo_destroy(&ctx->lines_vbo);
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    static const float lines_dummy[] = {0.0f, 0.0f, 0.0f};
    if (vbo_upload(&ctx->lines_vbo, GL_ARRAY_BUFFER, lines_dummy, sizeof(lines_dummy), GL_DYNAMIC_DRAW) != VBO_OK) {
        fprintf(stderr, "vbo_upload failed (lines_vbo)\n");
        vao_destroy(&ctx->lines_vao);
        vbo_destroy(&ctx->lines_vbo);
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    if (vao_bind_attributes(&ctx->lines_vao, &ctx->lines_vbo, attrs, 1u, 3u * sizeof(float)) != VAO_OK) {
        fprintf(stderr, "vao_bind_attributes failed (lines_vao)\n");
        vao_destroy(&ctx->lines_vao);
        vbo_destroy(&ctx->lines_vbo);
        vao_destroy(&ctx->vao);
        vbo_destroy(&ctx->vbo);
        shader_program_destroy(&ctx->program);
        SDL_GL_DeleteContext(ctx->gl);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    gl_check_("glEnable(GL_DEPTH_TEST)");
    SDL_GL_SetSwapInterval(1);
    gl_check_("gl_client_init_ done");

    return 0;
}

static void gl_client_shutdown_(struct gl_client_context *ctx) {
    if (!ctx) {
        return;
    }
    vao_destroy(&ctx->lines_vao);
    vbo_destroy(&ctx->lines_vbo);
    vao_destroy(&ctx->vao);
    vbo_destroy(&ctx->vbo);
    shader_program_destroy(&ctx->program);
    if (ctx->gl != NULL) {
        SDL_GL_DeleteContext(ctx->gl);
    }
    if (ctx->window != NULL) {
        SDL_DestroyWindow(ctx->window);
    }
    SDL_Quit();
    *ctx = (struct gl_client_context){0};
}

static int entity_find_(const struct entity_view *entities, size_t count, uint32_t entity_id) {
    for (size_t i = 0u; i < count; ++i) {
        if (entities[i].entity_id == entity_id) {
            return (int)i;
        }
    }
    return -1;
}

static int ensure_entity_cap_(struct entity_view **entities, size_t *cap, size_t needed) {
    if (!entities || !cap) {
        return 0;
    }
    if (needed <= *cap) {
        return 1;
    }

    size_t new_cap = (*cap == 0u) ? 16u : *cap;
    while (new_cap < needed) {
        new_cap *= 2u;
    }

    struct entity_view *p = (struct entity_view *)realloc(*entities, new_cap * sizeof(**entities));
    if (!p) {
        return 0;
    }
    *entities = p;
    *cap = new_cap;
    return 1;
}

static int add_entity_(struct entity_view **entities,
                       size_t *count,
                       size_t *cap,
                       uint32_t entity_id,
                       uint16_t owner_client_id,
                       double recv_time_s,
                       vec3_t pos,
                       quat_t rot) {
    if (!entities || !count || !cap) {
        return 0;
    }
    if (!ensure_entity_cap_(entities, cap, *count + 1u)) {
        return 0;
    }

    struct entity_view *e = &(*entities)[*count];
    *e = (struct entity_view){0};
    e->entity_id = entity_id;
    e->owner_client_id = owner_client_id;
    fr_pose_interpolator_reset(&e->pose);
    (void)fr_pose_interpolator_push(&e->pose, recv_time_s, pos, rot,
                                    (vec3_t){0,0,0}, (vec3_t){0,0,0}, 0.0);

    *count += 1u;
    return 1;
}

static int handle_spawn_(struct entity_view **entities,
                         size_t *count,
                         size_t *cap,
                         const net_repl_spawn_t *sp,
                         double recv_time_s,
                         uint16_t *io_self_owner_client_id,
                         uint32_t *io_self_entity_id) {
    if (!entities || !count || !cap || !sp) {
        return 0;
    }

    if (entity_find_(*entities, *count, sp->entity_id) >= 0) {
        return 1;
    }

    net_qvec3_mm_t qpos;
    qpos.x_mm = sp->pos_mm.x_mm;
    qpos.y_mm = sp->pos_mm.y_mm;
    qpos.z_mm = sp->pos_mm.z_mm;
    qpos._magic = 0x4D4D3351u; /* 'Q3MM' */

    vec3_t pos = {0};
    (void)net_dequantize_vec3_mm(qpos, &pos);

    const quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    if (!add_entity_(entities, count, cap, sp->entity_id, sp->owner_client_id, recv_time_s, pos, rot)) {
        return 0;
    }

    if (io_self_owner_client_id && *io_self_owner_client_id == UINT16_MAX) {
        *io_self_owner_client_id = sp->owner_client_id;
        if (io_self_entity_id) {
            *io_self_entity_id = sp->entity_id;
        }
    }
    if (io_self_owner_client_id && io_self_entity_id && sp->owner_client_id == *io_self_owner_client_id) {
        *io_self_entity_id = sp->entity_id;
    }

    return 1;
}

static int handle_spawn_batch_(struct entity_view **entities,
                              size_t *count,
                              size_t *cap,
                              const net_repl_spawn_batch_entry_t *entries,
                              uint16_t entry_count,
                              double recv_time_s,
                              uint16_t *io_self_owner_client_id,
                              uint32_t *io_self_entity_id) {
    if (!entities || !count || !cap || !entries) {
        return 0;
    }

    for (uint16_t i = 0u; i < entry_count; ++i) {
        const net_repl_spawn_batch_entry_t *e = &entries[i];
        if (entity_find_(*entities, *count, e->entity_id) >= 0) {
            continue;
        }

        net_qvec3_mm_t qpos;
        qpos.x_mm = e->pos_mm.x_mm;
        qpos.y_mm = e->pos_mm.y_mm;
        qpos.z_mm = e->pos_mm.z_mm;
        qpos._magic = 0x4D4D3351u; /* 'Q3MM' */

        vec3_t pos = {0};
        (void)net_dequantize_vec3_mm(qpos, &pos);

        const quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
        if (!add_entity_(entities, count, cap, e->entity_id, e->owner_client_id, recv_time_s, pos, rot)) {
            return 0;
        }

        if (io_self_owner_client_id && *io_self_owner_client_id == UINT16_MAX) {
            *io_self_owner_client_id = e->owner_client_id;
            if (io_self_entity_id) {
                *io_self_entity_id = e->entity_id;
            }
        }
        if (io_self_owner_client_id && io_self_entity_id && e->owner_client_id == *io_self_owner_client_id) {
            *io_self_entity_id = e->entity_id;
        }
    }

    return 1;
}

static int handle_state_cube_(struct entity_view **entities,
                             size_t *count,
                             size_t *cap,
                             const net_repl_state_cube_t *st,
                             double recv_time_s,
                             fr_debug_lines_t *correction_lines) {
    if (!entities || !count || !cap || !st) {
        return 0;
    }

    net_qvec3_mm_t qpos;
    qpos.x_mm = st->pos_mm.x_mm;
    qpos.y_mm = st->pos_mm.y_mm;
    qpos.z_mm = st->pos_mm.z_mm;
    qpos._magic = 0x4D4D3351u; /* 'Q3MM' */

    vec3_t pos = {0};
    if (net_dequantize_vec3_mm(qpos, &pos) != NET_QUANT_OK) {
        return 1;
    }

    net_qquat_snorm16_t qrot;
    qrot.x = st->rot_snorm16.x;
    qrot.y = st->rot_snorm16.y;
    qrot.z = st->rot_snorm16.z;
    qrot.w = st->rot_snorm16.w;
    qrot._magic = 0x4E513136u; /* 'NQ16' */

    quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    (void)net_dequantize_quat_snorm16(qrot, &rot);

    int idx = entity_find_(*entities, *count, st->entity_id);
    if (idx < 0) {
        if (!add_entity_(entities, count, cap, st->entity_id, 0u, recv_time_s, pos, rot)) {
            return 0;
        }
        return 1;
    }

    if (correction_lines) {
        vec3_t est_pos = {0};
        quat_t est_rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
        if (fr_pose_interpolator_sample(&(*entities)[idx].pose, recv_time_s, 1e-6f, &est_pos, &est_rot)) {
            const vec3_t dp = vec3_sub(pos, est_pos);
            const float pos_err = vec3_magnitude(dp);

            quat_t qa = quat_normalize_safe(est_rot, 1e-6f);
            quat_t qb = quat_normalize_safe(rot, 1e-6f);
            float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
            if (dot < 0.0f) {
                dot = -dot;
            }
            if (dot > 1.0f) {
                dot = 1.0f;
            }
            const float rot_err_rad = 2.0f * acosf(dot);

            const float pos_threshold = 0.005f;
            const float rot_threshold = 0.02f;
            if (pos_err > pos_threshold || rot_err_rad > rot_threshold) {
                vec3_t verts[16];
                const size_t n = fr_debug_correction_lines_cube(est_pos, est_rot, pos, rot, 0.125f, verts, 16u);
                for (size_t i = 0u; i + 1u < n; i += 2u) {
                    (void)fr_debug_lines_add(correction_lines, verts[i + 0u], verts[i + 1u], recv_time_s, 0.35);
                }
            }
        }
    }

    (void)fr_pose_interpolator_push(&(*entities)[idx].pose, recv_time_s, pos, rot,
                                    (vec3_t){0,0,0}, (vec3_t){0,0,0}, 0.0);
    return 1;
}

static quat_t random_target_rot_(uint32_t *rng) {
    vec3_t axis = {
        rand_float01_(rng) * 2.0f - 1.0f,
        rand_float01_(rng) * 2.0f - 1.0f,
        rand_float01_(rng) * 2.0f - 1.0f,
    };
    axis = vec3_normalize_safe(axis, 1e-6f);
    const float angle = rand_float01_(rng) * 2.0f * FERRUM_PI;
    return quat_from_axis_angle(axis, angle, 1e-6f);
}

static mat4_t mat4_from_quat_(quat_t q) {
    mat4_t out;
    if (quat_to_mat4(q, &out) != 0) {
        return mat4_identity();
    }
    return out;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        usage_(argv[0]);
        return 2;
    }

    const char *server_ip_s = argv[1];
    long port_l = strtol(argv[2], NULL, 10);
    long duration_ms_l = strtol(argv[3], NULL, 10);
    if (port_l <= 0 || port_l > 65535 || duration_ms_l <= 0) {
        usage_(argv[0]);
        return 2;
    }

    uint32_t seed = 0u;
    int headless = 0;
    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
            continue;
        }
        if (strcmp(argv[i], "--seed") == 0) {
            if (i + 1 >= argc) {
                usage_(argv[0]);
                return 2;
            }
            seed = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            i++;
            continue;
        }
        usage_(argv[0]);
        return 2;
    }

    uint8_t ip[4];
    if (!parse_ipv4_dotted_(server_ip_s, ip)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", server_ip_s);
        return 2;
    }

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

    uint32_t rng = seed;
    if (rng == 0u) {
        rng = (uint32_t)(0xA5A5A5A5u ^ (uint32_t)getpid());
    }

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
    if (net_rudp_peer_send_reliable(&peer,
                                   &sock,
                                   &server_addr,
                                   now_ms_(),
                                   NET_REPL_SCHEMA_JOIN,
                                   join_payload,
                                   sizeof(join_payload),
                                   &join_seq) != NET_RUDP_OK) {
        fprintf(stderr, "Failed to send JOIN\n");
        free(send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }
    (void)join_seq;

    struct gl_client_context gl = {0};
    int win_w = 960;
    int win_h = 540;
    if (!headless) {
        if (gl_client_init_(&gl, "p008_renderer_client", win_w, win_h) != 0) {
            free(send_slots);
            net_udp_socket_close(&sock);
            return 1;
        }
    }

    fr_debug_line_t correction_line_storage[512];
    fr_debug_lines_t correction_lines;
    fr_debug_lines_t *correction_lines_ptr = NULL;
    vec3_t correction_vertices[1024];
    if (!headless) {
        fr_debug_lines_init(&correction_lines, correction_line_storage, 512u);
        correction_lines_ptr = &correction_lines;
    }

    struct entity_view *entities = NULL;
    size_t entity_count = 0u;
    size_t entity_cap = 0u;

    uint16_t self_owner_client_id = UINT16_MAX;
    uint32_t self_entity_id = 0u;

    quat_t self_rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    quat_t self_target = random_target_rot_(&rng);
    double next_target_time_s = now_s_() + 1.0;

    const uint64_t start_ms = now_ms_();
    const uint64_t end_ms = start_ms + (uint64_t)duration_ms_l;

    uint64_t next_diag_ms = start_ms;

    uint64_t next_keepalive_ms = start_ms;
    uint8_t rx_packet[NET_RUDP_MAX_PACKET_SIZE];

    uint64_t last_frame_ns = now_ns_();

    for (;;) {
        const uint64_t frame_ns = now_ns_();
        const double dt_s = (double)(frame_ns - last_frame_ns) / 1000000000.0;
        last_frame_ns = frame_ns;

        const uint64_t now_ms = frame_ns / 1000000ull;
        const double now_s = (double)frame_ns / 1000000000.0;

        if (now_ms >= end_ms) {
            break;
        }

        if (now_s >= next_target_time_s) {
            self_target = random_target_rot_(&rng);
            next_target_time_s = now_s + 1.0 + (double)(rand_float01_(&rng) * 1.0);
        }

        const float step = (float)(dt_s * 2.5);
        self_rot = quat_slerp(self_rot, self_target, step, 1e-6f);

        if (now_ms >= next_keepalive_ms) {
            (void)net_rudp_peer_send_unreliable(&peer, &sock, &server_addr, now_ms, NET_REPL_SCHEMA_JOIN,
                                                join_payload, sizeof(join_payload));
            next_keepalive_ms = now_ms + 100u;
        }

        (void)net_rudp_peer_tick_resend(&peer, &sock, &server_addr, now_ms);

        for (;;) {
            size_t rx_size = 0u;
            const int rrc = net_udp_socket_recv(&sock, rx_packet, sizeof(rx_packet), &rx_size);
            if (rrc == NET_UDP_SOCKET_EMPTY) {
                break;
            }
            if (rrc != NET_UDP_SOCKET_OK) {
                break;
            }

            uint8_t reliable = 0u;
            uint16_t schema_id = 0u;
            uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
            size_t payload_size = 0u;
            if (net_rudp_peer_receive(&peer,
                                      rx_packet,
                                      rx_size, 0u, &reliable,
                                      &schema_id,
                                      payload,
                                      sizeof(payload),
                                      &payload_size) != NET_RUDP_OK) {
                continue;
            }
            (void)reliable;

            const double recv_time_s = now_s_();

            if (schema_id == NET_REPL_SCHEMA_SPAWN) {
                net_repl_spawn_t sp;
                if (net_repl_spawn_decode(&sp, payload, payload_size) == NET_REPL_OK) {
                    (void)handle_spawn_(&entities, &entity_count, &entity_cap, &sp, recv_time_s, &self_owner_client_id,
                                        &self_entity_id);
                }
            } else if (schema_id == NET_REPL_SCHEMA_SPAWN_BATCH) {
                net_repl_spawn_batch_entry_t entries[64];
                uint16_t count = 0u;
                uint16_t tick = 0u;
                if (net_repl_spawn_batch_decode(&tick, entries, 64u, &count, payload, payload_size) == NET_REPL_OK) {
                    (void)tick;
                    (void)handle_spawn_batch_(&entities, &entity_count, &entity_cap, entries, count, recv_time_s,
                                              &self_owner_client_id, &self_entity_id);
                }
            } else if (schema_id == NET_REPL_SCHEMA_STATE_CUBE) {
                net_repl_state_cube_t st;
                if (net_repl_state_cube_decode(&st, payload, payload_size) == NET_REPL_OK) {
                    (void)handle_state_cube_(&entities, &entity_count, &entity_cap, &st, recv_time_s, correction_lines_ptr);
                }
            } else if (schema_id == NET_REPL_SCHEMA_WELCOME) {
                /* nothing needed for rendering */
                net_repl_welcome_t w;
                (void)net_repl_welcome_decode(&w, payload, payload_size);
            }
        }

        if (now_ms >= next_diag_ms) {
            fprintf(stderr, "diag: entities=%zu self_entity_id=%u\n", entity_count, (unsigned)self_entity_id);
            next_diag_ms = now_ms + 1000u;
        }

        if (!headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    duration_ms_l = 0;
                    goto done;
                }
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                    duration_ms_l = 0;
                    goto done;
                }
            }

            glViewport(0, 0, win_w, win_h);
            glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            gl_check_("frame clear");

            if (shader_program_bind(&gl.program) == SHADER_PROGRAM_OK) {
                glBindVertexArray(vao_handle(&gl.vao));

                mat4_t proj;
                const float aspect = (win_h > 0) ? ((float)win_w / (float)win_h) : 1.0f;
                if (mat4_perspective(60.0f * (FERRUM_PI / 180.0f), aspect, 0.1f, 100.0f, &proj) != 0) {
                    proj = mat4_identity();
                }

                mat4_t view;
                if (mat4_look_at((vec3_t){0.0f, 3.0f, 6.0f}, (vec3_t){0.0f, 0.0f, 0.0f}, (vec3_t){0.0f, 1.0f, 0.0f},
                                 &view) != 0) {
                    view = mat4_identity();
                }

                /* Render slightly behind to smooth latency/loss. */
                const double render_time_s = now_s - 0.20;

                for (size_t i = 0u; i < entity_count; ++i) {
                    struct entity_view *e = &entities[i];
                    vec3_t pos;
                    quat_t rot;
                    if (!fr_pose_interpolator_sample(&e->pose, render_time_s, 1e-6f, &pos, &rot)) {
                        continue;
                    }

                    if (e->entity_id == self_entity_id) {
                        rot = self_rot;
                    }

                    const mat4_t t = mat4_translation(pos.x, pos.y, pos.z);
                    const mat4_t r = mat4_from_quat_(rot);
                    const mat4_t s = mat4_scaling(0.25f, 0.25f, 0.25f);
                    const mat4_t model = mat4_mul(t, mat4_mul(r, s));

                    const mat4_t vp = mat4_mul(proj, view);
                    const mat4_t mvp = mat4_mul(vp, model);

                    if (shader_uniform_set_mat4(&gl.uniforms, &gl.program, "u_mvp", mvp.m, 0u) != SHADER_UNIFORM_OK) {
                        fprintf(stderr, "shader_uniform_set_mat4 failed for u_mvp\n");
                    }

                    float rgb[3];
                    color_from_owner_(e->owner_client_id, rgb);
                    if (e->entity_id == self_entity_id) {
                        rgb[0] = 1.0f;
                        rgb[1] = 1.0f;
                        rgb[2] = 1.0f;
                    }
                    if (shader_uniform_set_vec3(&gl.uniforms, &gl.program, "u_color", rgb) != SHADER_UNIFORM_OK) {
                        fprintf(stderr, "shader_uniform_set_vec3 failed for u_color\n");
                    }

                    glDrawArrays(GL_TRIANGLES, 0, 36);
                    gl_check_("glDrawArrays");
                }

                if (correction_lines_ptr) {
                    size_t line_vertex_count = 0u;
                    if (fr_debug_lines_collect_vertices(correction_lines_ptr,
                                                       now_s,
                                                       correction_vertices,
                                                       (sizeof(correction_vertices) / sizeof(correction_vertices[0])),
                                                       &line_vertex_count) &&
                        line_vertex_count > 0u) {

                        (void)vbo_upload(&gl.lines_vbo,
                                         GL_ARRAY_BUFFER,
                                         correction_vertices,
                                         line_vertex_count * sizeof(correction_vertices[0]),
                                         GL_DYNAMIC_DRAW);

                        glBindVertexArray(vao_handle(&gl.lines_vao));

                        const mat4_t vp = mat4_mul(proj, view);
                        if (shader_uniform_set_mat4(&gl.uniforms, &gl.program, "u_mvp", vp.m, 0u) !=
                            SHADER_UNIFORM_OK) {
                            fprintf(stderr, "shader_uniform_set_mat4 failed for u_mvp (lines)\n");
                        }
                        const float red[3] = {1.0f, 0.0f, 0.0f};
                        if (shader_uniform_set_vec3(&gl.uniforms, &gl.program, "u_color", red) != SHADER_UNIFORM_OK) {
                            fprintf(stderr, "shader_uniform_set_vec3 failed for u_color (lines)\n");
                        }

                        glDisable(GL_DEPTH_TEST);
                        glDrawArrays(GL_LINES, 0, (GLsizei)line_vertex_count);
                        glEnable(GL_DEPTH_TEST);
                        gl_check_("glDrawArrays(lines)");
                    }
                }

                glBindVertexArray(0u);
            }

            SDL_GL_SwapWindow(gl.window);
            gl_check_("SDL_GL_SwapWindow");
        } else {
            sleep_ms_(1u);
        }
    }

done:
    free(entities);
    if (!headless) {
        gl_client_shutdown_(&gl);
    }

    free(send_slots);
    net_udp_socket_close(&sock);
    return 0;
}
