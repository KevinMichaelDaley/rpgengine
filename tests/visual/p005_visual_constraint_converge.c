/**
 * @file p005_visual_constraint_converge.c
 * @brief Visual test: constraint convergence with varying iteration counts.
 *
 * Loads humanoid.fskel and runs the animation constraint solver with
 * 1, 2, 4, and 8 iterations.  Renders all four skeletons side by side
 * so visual convergence improvement can be verified.
 *
 * PASS if frame_count >= TOTAL_FRAMES and no GL errors.
 */

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/ik_solver.h"
#include "ferrum/animation/copy_track.h"
#include "ferrum/animation/limit_constraints.h"
#include "ferrum/animation/surface_vol.h"
#include "ferrum/animation/transform_map.h"
#include "ferrum/animation/fskel_loader.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/phys_anim_entity.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/job/system.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/video_capture.h"

/* ── Constants ────────────────────────────────────────────────────── */

#define WINDOW_W     800
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 2
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f
#define DT (1.0f / TARGET_FPS)

#define NUM_VARIANTS 4
static const int ITERATION_COUNTS[NUM_VARIANTS] = { 1, 2, 4, 8 };
static const float SPACING = 6.0f;

/* ── Shader sources ───────────────────────────────────────────────── */

static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = vec4(v_color, 1.0);\n"
    "}\n";

/* ── GL context ───────────────────────────────────────────────────── */

static SDL_Window   *g_window;
static SDL_GLContext  g_gl_ctx;
static gl_loader_t   g_loader;

static void *sdl_get_proc_(const char *name, void *ud) {
    (void)ud;
    return SDL_GL_GetProcAddress(name);
}

static int init_gl_context_(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    g_window = SDL_CreateWindow("Constraint Convergence",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                WINDOW_W, WINDOW_H,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_window) return -1;
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) { SDL_DestroyWindow(g_window); SDL_Quit(); return -1; }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(g_gl_ctx); SDL_DestroyWindow(g_window);
        SDL_Quit(); return -1;
    }
    (void)glGetError();
    g_loader.get_proc_address = sdl_get_proc_;
    g_loader.user_data = NULL;
    return 0;
}

static void cleanup_gl_context_(void) {
    SDL_GL_DeleteContext(g_gl_ctx);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
}

/* ── PPM snapshot ─────────────────────────────────────────────────── */

static int save_ppm_(const char *path, int w, int h) {
    uint8_t *pixels = (uint8_t *)malloc((size_t)w * h * 3);
    if (!pixels) return -1;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    FILE *f = fopen(path, "wb");
    if (!f) { free(pixels); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)
        fwrite(pixels + y * w * 3, 3, (size_t)w, f);
    fclose(f);
    free(pixels);
    return 0;
}

/* ── Dynamic line buffer ──────────────────────────────────────────── */

#define MAX_LINE_VERTS 16384

typedef struct { float pos[3]; float col[3]; } line_vertex_t;
static line_vertex_t *g_line_verts;
static int g_line_vert_count;
static GLuint g_line_vao, g_line_vbo;

static void init_line_buffer_(void) {
    g_line_verts = (line_vertex_t *)calloc(MAX_LINE_VERTS,
                                           sizeof(line_vertex_t));
    glGenVertexArrays(1, &g_line_vao);
    glGenBuffers(1, &g_line_vbo);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(MAX_LINE_VERTS * sizeof(line_vertex_t)),
                 NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(line_vertex_t), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(line_vertex_t),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    g_line_vert_count = 0;
}

static void begin_lines_(void) { g_line_vert_count = 0; }

static void add_line_(vec3_t a, vec3_t b, float r, float g, float bl) {
    if (g_line_vert_count + 2 > MAX_LINE_VERTS) return;
    g_line_verts[g_line_vert_count].pos[0] = a.x;
    g_line_verts[g_line_vert_count].pos[1] = a.y;
    g_line_verts[g_line_vert_count].pos[2] = a.z;
    g_line_verts[g_line_vert_count].col[0] = r;
    g_line_verts[g_line_vert_count].col[1] = g;
    g_line_verts[g_line_vert_count].col[2] = bl;
    g_line_vert_count++;
    g_line_verts[g_line_vert_count].pos[0] = b.x;
    g_line_verts[g_line_vert_count].pos[1] = b.y;
    g_line_verts[g_line_vert_count].pos[2] = b.z;
    g_line_verts[g_line_vert_count].col[0] = r;
    g_line_verts[g_line_vert_count].col[1] = g;
    g_line_verts[g_line_vert_count].col[2] = bl;
    g_line_vert_count++;
}

static void flush_lines_(const mat4_t *mvp, shader_program_t *sh,
                          int32_t u_mvp_loc) {
    shader_program_bind(sh);
    sh->glUniformMatrix4fv(u_mvp_loc, 1, 0, mvp->m);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_line_vert_count * sizeof(line_vertex_t)),
                    g_line_verts);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, g_line_vert_count);
    glBindVertexArray(0);
}

static void cleanup_line_buffer_(void) {
    glDeleteVertexArrays(1, &g_line_vao);
    glDeleteBuffers(1, &g_line_vbo);
    free(g_line_verts);
}

/* ── Draw skeleton as lines (with X offset) ──────────────────────── */

static void draw_skeleton_offset_(const skeleton_def_t *skel,
                                  const mat4_t *pose,
                                  float x_offset,
                                  float r, float g, float b) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t pi = skel->parent_indices[i];
        if (pi == UINT32_MAX || pi >= skel->joint_count) continue;
        vec3_t child  = {
            pose[i].m[12]  + x_offset,
            pose[i].m[13],
            pose[i].m[14]
        };
        vec3_t parent = {
            pose[pi].m[12] + x_offset,
            pose[pi].m[13],
            pose[pi].m[14]
        };
        add_line_(parent, child, r, g, b);
    }
}

/* ── Draw ground grid ────────────────────────────────────────────── */

static void draw_ground_grid_(float y, float extent, int divs) {
    float step = (2.0f * extent) / (float)divs;
    for (int i = 0; i <= divs; i++) {
        float t = -extent + step * (float)i;
        float brightness = (i == divs / 2) ? 0.2f : 0.08f;
        add_line_((vec3_t){t, y, -extent},
                  (vec3_t){t, y, extent},
                  brightness, brightness, brightness);
        add_line_((vec3_t){-extent, y, t},
                  (vec3_t){extent, y, t},
                  brightness, brightness, brightness);
    }
}

/* ── Draw label (position indicator ring) ────────────────────────── */

static void draw_label_(float x_offset, float y, float r, float g, float b) {
    /* Small diamond at the base to mark each variant. */
    float sz = 0.3f;
    add_line_((vec3_t){x_offset - sz, y, 0},
              (vec3_t){x_offset, y + sz, 0}, r, g, b);
    add_line_((vec3_t){x_offset, y + sz, 0},
              (vec3_t){x_offset + sz, y, 0}, r, g, b);
    add_line_((vec3_t){x_offset + sz, y, 0},
              (vec3_t){x_offset - sz, y, 0}, r, g, b);
}

/* ── Substep animation context (per variant) ─────────────────────── */

#define STRIDE_LENGTH 3.0f
#define STEP_HEIGHT   1.5f
#define WALK_SPEED    2.0f

static vec3_t walk_foot_pos_(float phase, float rest_x,
                              float rest_y, float rest_z) {
    vec3_t pos;
    pos.x = rest_x;
    if (phase < 0.5f) {
        float t = phase / 0.5f;
        pos.z = rest_z + STRIDE_LENGTH * (0.5f - t);
        pos.y = rest_y;
    } else {
        float t = (phase - 0.5f) / 0.5f;
        pos.z = rest_z + STRIDE_LENGTH * (t - 0.5f);
        pos.y = rest_y + STEP_HEIGHT * sinf(t * PI);
    }
    return pos;
}

static int find_bone_(const skeleton_def_t *skel, const char *name) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        if (strcmp(skel->joint_names[i], name) == 0) return (int)i;
    }
    return -1;
}

typedef struct converge_ctx {
    skeleton_def_t       *skel;
    phys_anim_entity_t   *anim_ent;
    constraint_solver_t  *solver;
    mat4_t               *local_pose;
    mat4_t               *target_pose;
    int                   idx_foot_l;
    int                   idx_foot_r;
    int                   idx_traj;
    int                   idx_root_master;
    vec3_t                rest_foot_l;
    vec3_t                rest_foot_r;
    float                 time_acc;
} converge_ctx_t;

/**
 * @brief Substep callback: advance walk cycle and push kinematic
 *        bone positions+orientations so joints are XPBD-solved.
 */
static void converge_substep(void *user, struct phys_world *world,
                              uint32_t substep, float substep_dt) {
    (void)substep_dt;
    converge_ctx_t *ctx = (converge_ctx_t *)user;
    skeleton_def_t *skel = ctx->skel;
    uint32_t n = skel->joint_count;

    if (substep == 0) {
        ctx->time_acc += (1.0f / (float)TARGET_FPS);
    }
    float t = ctx->time_acc;
    float cycle = t * WALK_SPEED;
    float phase_l = fmodf(cycle, 1.0f);
    float phase_r = fmodf(cycle + 0.5f, 1.0f);
    float forward_z = cycle * STRIDE_LENGTH;

    memcpy(ctx->local_pose, skel->rest_local, n * sizeof(mat4_t));

    if (ctx->idx_traj >= 0)
        ctx->local_pose[ctx->idx_traj].m[14] += forward_z;

    if (ctx->idx_foot_l >= 0) {
        vec3_t fl = walk_foot_pos_(phase_l, ctx->rest_foot_l.x,
                                    ctx->rest_foot_l.y,
                                    ctx->rest_foot_l.z + forward_z);
        ctx->local_pose[ctx->idx_foot_l].m[12] = fl.x;
        ctx->local_pose[ctx->idx_foot_l].m[13] = fl.y;
        ctx->local_pose[ctx->idx_foot_l].m[14] = fl.z;
    }
    if (ctx->idx_foot_r >= 0) {
        vec3_t fr = walk_foot_pos_(phase_r, ctx->rest_foot_r.x,
                                    ctx->rest_foot_r.y,
                                    ctx->rest_foot_r.z + forward_z);
        ctx->local_pose[ctx->idx_foot_r].m[12] = fr.x;
        ctx->local_pose[ctx->idx_foot_r].m[13] = fr.y;
        ctx->local_pose[ctx->idx_foot_r].m[14] = fr.z;
    }

    if (ctx->idx_root_master >= 0) {
        float bob = 0.3f * sinf(cycle * 4.0f * PI);
        ctx->local_pose[ctx->idx_root_master].m[13] += bob;
    }

    memset(ctx->target_pose, 0, n * sizeof(mat4_t));
    constraint_solver_evaluate(ctx->solver, skel, ctx->local_pose,
                               ctx->target_pose, n);
    phys_anim_entity_push_kinematic(ctx->anim_ent, world,
                                     ctx->target_pose, n);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Constraint Convergence ===\n");

    if (init_gl_context_() != 0) { return 1; }

    shader_program_t shader;
    char log_buf[512];
    int rc = shader_program_create(&shader, &g_loader,
                                   VERT_SRC, FRAG_SRC,
                                   log_buf, sizeof(log_buf));
    if (rc != 0) {
        fprintf(stderr, "Shader failed: %s\n", log_buf);
        cleanup_gl_context_();
        return 1;
    }
    int32_t u_mvp_loc = shader.glGetUniformLocation(shader.handle, "u_mvp");
    init_line_buffer_();

    /* Load skeleton. */
    skeleton_def_t skel;
    mat4_t *ibms = NULL;
    uint32_t ibm_count = 0;
    if (!fskel_load("asset_src/humanoid.fskel", &skel, &ibms, &ibm_count)) {
        fprintf(stderr, "Failed to load humanoid.fskel\n");
        cleanup_line_buffer_();
        shader_program_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }
    printf("  Loaded: %u joints\n", skel.joint_count);

    /* Find control bones (shared across all variants). */
    int idx_foot_l = find_bone_(&skel, "c_foot_ik.l");
    int idx_foot_r = find_bone_(&skel, "c_foot_ik.r");
    int idx_traj   = find_bone_(&skel, "c_traj");
    int idx_root_master = find_bone_(&skel, "c_root_master.x");

    vec3_t rest_foot_l = {0}, rest_foot_r = {0};
    if (idx_foot_l >= 0) {
        rest_foot_l = (vec3_t){
            skel.rest_world[idx_foot_l].m[12],
            skel.rest_world[idx_foot_l].m[13],
            skel.rest_world[idx_foot_l].m[14]
        };
    }
    if (idx_foot_r >= 0) {
        rest_foot_r = (vec3_t){
            skel.rest_world[idx_foot_r].m[12],
            skel.rest_world[idx_foot_r].m[13],
            skel.rest_world[idx_foot_r].m[14]
        };
    }

    /* Colors: 1-iter=red, 2=orange, 4=green, 8=blue. */
    static const float colors[NUM_VARIANTS][3] = {
        {0.9f, 0.2f, 0.2f},
        {0.9f, 0.6f, 0.1f},
        {0.2f, 0.8f, 0.3f},
        {0.2f, 0.4f, 0.9f},
    };

    /* ── Physics setup: one world per variant ──────────────────── */

    job_system_t phys_job_sys;
    if (job_system_create(&phys_job_sys, 2, 4096, 256u * 1024u, 4096, 0)
        != JOB_CREATE_OK) {
        fprintf(stderr, "job_system_create failed\n"); return 1;
    }
    if (job_system_start(&phys_job_sys) != 0) {
        fprintf(stderr, "job_system_start failed\n"); return 1;
    }

    phys_world_t worlds[NUM_VARIANTS];
    phys_anim_entity_t anim_ents[NUM_VARIANTS];
    constraint_solver_t solvers[NUM_VARIANTS];
    converge_ctx_t ctxs[NUM_VARIANTS];
    mat4_t *local_poses[NUM_VARIANTS];
    mat4_t *target_poses[NUM_VARIANTS];
    phys_job_context_t phys_jobs[NUM_VARIANTS];
    phys_tick_runner_t tick_runners[NUM_VARIANTS];
    fr_topic_channel_t *cmd_channels[NUM_VARIANTS];
    phys_game_state_t game_states[NUM_VARIANTS];

    for (int v = 0; v < NUM_VARIANTS; v++) {
        phys_world_config_t wcfg = phys_world_config_default();
        wcfg.max_bodies = 256;
        wcfg.max_colliders = 256;
        wcfg.max_joints = 256;
        wcfg.default_solver_iterations = (uint32_t)ITERATION_COUNTS[v];
        if (phys_world_init(&worlds[v], &wcfg) != 0) {
            fprintf(stderr, "world init failed for variant %d\n", v);
            return 1;
        }

        /* Ground plane. */
        uint32_t ground_id = phys_world_create_body(&worlds[v]);
        {
            phys_body_t *g = phys_world_get_body(&worlds[v], ground_id);
            g->flags |= PHYS_BODY_FLAG_STATIC;
            phys_world_set_halfspace_collider(&worlds[v], ground_id,
                (phys_vec3_t){0.0f, 1.0f, 0.0f}, 0.0f);
        }

        /* Animated entity. */
        if (!phys_anim_entity_create(&anim_ents[v], &worlds[v], &skel,
                                      skel.rest_world)) {
            fprintf(stderr, "anim entity create failed for variant %d\n", v);
            return 1;
        }

        /* Constraint solver. */
        constraint_solver_init(&solvers[v], skel.joint_count,
                               skel.max_constraints_per_joint);
        ik_solver_register(&solvers[v]);
        copy_track_register(&solvers[v]);
        limit_constraints_register(&solvers[v]);
        surface_vol_register(&solvers[v]);
        transform_map_register(&solvers[v]);

        /* Pose buffers. */
        local_poses[v]  = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
        target_poses[v] = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));

        /* Substep animation context. */
        memset(&ctxs[v], 0, sizeof(ctxs[v]));
        ctxs[v].skel            = &skel;
        ctxs[v].anim_ent        = &anim_ents[v];
        ctxs[v].solver          = &solvers[v];
        ctxs[v].local_pose      = local_poses[v];
        ctxs[v].target_pose     = target_poses[v];
        ctxs[v].idx_foot_l      = idx_foot_l;
        ctxs[v].idx_foot_r      = idx_foot_r;
        ctxs[v].idx_traj        = idx_traj;
        ctxs[v].idx_root_master = idx_root_master;
        ctxs[v].rest_foot_l     = rest_foot_l;
        ctxs[v].rest_foot_r     = rest_foot_r;

        /* Wire substep callback on the world. */
        worlds[v].anim_substep_cb   = converge_substep;
        worlds[v].anim_substep_user = &ctxs[v];

        /* Job context + tick runner. */
        phys_job_context_init(&phys_jobs[v], &phys_job_sys);

        fr_topic_channel_config_t chan_cfg = {
            .capacity = 64, .capacity_bytes = 64 * 1024,
            .max_message_size = 1024,
            .backpressure = FR_TOPIC_BACKPRESSURE_FAIL
        };
        cmd_channels[v] = fr_topic_channel_create(&chan_cfg);

        phys_tick_runner_init(&tick_runners[v], &worlds[v], &phys_jobs[v],
                              cmd_channels[v], NULL, NULL, NULL);

        phys_game_state_init(&game_states[v]);
        phys_player_state_t player0 = {
            .position = {0.0f, 0.0f, 0.0f},
            .interaction_radius = 20.0f,
        };
        phys_game_state_set_player(&game_states[v], 0, &player0);
        tick_runners[v].game_state = &game_states[v];

        phys_tick_runner_start(&tick_runners[v]);
        printf("  Variant %d (%d iters): started\n",
               v, ITERATION_COUNTS[v]);
    }

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_constraint_converge.mp4";
    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture)
        fprintf(stderr, "WARNING: video capture unavailable\n");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.06f, 0.06f, 0.10f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;

    /* ── Render loop ──────────────────────────────────────────────── */

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        begin_lines_();
        draw_ground_grid_(0.0f, 20.0f, 40);

        for (int v = 0; v < NUM_VARIANTS; v++) {
            /* Sync bone transforms from physics. */
            phys_anim_entity_sync_from_world(&anim_ents[v], &worlds[v], &skel);

            /* Draw variant at X offset. */
            float x_off = ((float)v - 1.5f) * SPACING;
            draw_skeleton_offset_(&skel, anim_ents[v].bone_world, x_off,
                                  colors[v][0], colors[v][1], colors[v][2]);
            draw_label_(x_off, -0.2f,
                        colors[v][0], colors[v][1], colors[v][2]);
        }

        /* Camera: wide view to see all 4 variants. */
        mat4_t view, proj;
        mat4_look_at((vec3_t){0.0f, 8.0f, 30.0f},
                     (vec3_t){0.f, 3.0f, 0.f},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(45.0f * PI / 180.0f,
                         (float)WINDOW_W / (float)WINDOW_H,
                         0.1f, 200.0f, &proj);
        mat4_t mvp = mat4_mul(proj, view);
        flush_lines_(&mvp, &shader, u_mvp_loc);

        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        if (f == 0 || f == TOTAL_FRAMES / 2 || f == TOTAL_FRAMES - 1) {
            char path[256];
            snprintf(path, sizeof(path),
                     "tests/output/p005_converge_frame_%03d.ppm", f);
            save_ppm_(path, WINDOW_W, WINDOW_H);
            printf("  Snapshot: %s\n", path);
        }

        if (capture) {
            fr_video_capture_submit_frame(capture);
            glFlush();
        }
        SDL_GL_SwapWindow(g_window);
        ++frame_count;

        uint32_t elapsed = SDL_GetTicks() - frame_start;
        uint32_t interval = 1000 / TARGET_FPS;
        if (elapsed < interval) SDL_Delay(interval - elapsed);
    }

    /* Cleanup. */
    for (int v = 0; v < NUM_VARIANTS; v++) {
        phys_tick_runner_stop(&tick_runners[v]);
    }
    if (capture) fr_video_capture_destroy(capture);
    for (int v = 0; v < NUM_VARIANTS; v++) {
        constraint_solver_destroy(&solvers[v]);
        free(local_poses[v]);
        free(target_poses[v]);
        phys_anim_entity_destroy(&anim_ents[v]);
        phys_tick_runner_destroy(&tick_runners[v]);
        phys_job_context_destroy(&phys_jobs[v]);
        if (cmd_channels[v]) fr_topic_channel_destroy(cmd_channels[v]);
        phys_world_destroy(&worlds[v]);
    }
    cleanup_line_buffer_();
    shader_program_destroy(&shader);
    job_system_shutdown(&phys_job_sys);
    skeleton_def_destroy(&skel);
    if (ibms) free(ibms);
    cleanup_gl_context_();

    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);

    int pass = (frame_count >= TOTAL_FRAMES) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
