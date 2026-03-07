/**
 * @file p005_visual_walk_cycle.c
 * @brief Visual test: humanoid skeleton with procedural walk cycle.
 *
 * Loads asset_src/humanoid.fskel (333 bones), renders as debug lines,
 * and animates IK control bones (c_foot_ik.l, c_foot_ik.r, c_traj)
 * to produce a simple procedural walk cycle over a ground plane.
 *
 * PASS if frame_count >= TOTAL_FRAMES and no GL errors.
 */

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/ik_solver.h"
#include "ferrum/animation/copy_track.h"
#include "ferrum/animation/limit_constraints.h"
#include "ferrum/animation/surface_vol.h"
#include "ferrum/animation/transform_map.h"
#include "ferrum/animation/fskel_loader.h"
#include "ferrum/animation/ragdoll.h"
#include "ferrum/animation/bone_to_body.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/video_capture.h"

/* ── Constants ────────────────────────────────────────────────────── */

#define WINDOW_W     640
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 4
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f

/* Walk cycle parameters. */
#define STRIDE_LENGTH 3.0f  /* Forward stride per step (in bone units). */
#define STEP_HEIGHT   1.5f  /* How high the foot lifts. */
#define WALK_SPEED    2.0f  /* Cycles per duration. */

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
    "    gl_PointSize = 4.0;\n"
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
    g_window = SDL_CreateWindow("Walk Cycle Visual Test",
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

#define MAX_LINE_VERTS 4096

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

/* ── Draw ground plane grid ──────────────────────────────────────── */

static void draw_ground_grid_(float y, float extent, int divs) {
    float step = (2.0f * extent) / (float)divs;
    for (int i = 0; i <= divs; i++) {
        float t = -extent + step * (float)i;
        float brightness = (i == divs / 2) ? 0.3f : 0.12f;
        add_line_((vec3_t){t, y, -extent},
                  (vec3_t){t, y, extent},
                  brightness, brightness * 0.8f, brightness * 0.6f);
        add_line_((vec3_t){-extent, y, t},
                  (vec3_t){extent, y, t},
                  brightness, brightness * 0.8f, brightness * 0.6f);
    }
}

/* ── Draw skeleton as lines ──────────────────────────────────────── */

static void draw_skeleton_(const skeleton_def_t *skel, const mat4_t *pose) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t pi = skel->parent_indices[i];
        if (pi == UINT32_MAX || pi >= skel->joint_count) continue;

        vec3_t child  = { pose[i].m[12],  pose[i].m[13],  pose[i].m[14] };
        vec3_t parent = { pose[pi].m[12], pose[pi].m[13], pose[pi].m[14] };

        /* Color by depth: blue at root, cyan mid, green at tips. */
        /* Use a simple heuristic: bone index / total. */
        float t = (float)i / (float)skel->joint_count;
        float r = 0.1f + 0.3f * t;
        float g = 0.4f + 0.5f * t;
        float b = 0.9f - 0.6f * t;

        add_line_(parent, child, r, g, b);
    }
}

/* ── Find bone by name ────────────────────────────────────────────── */

static int find_bone_(const skeleton_def_t *skel, const char *name) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        if (strcmp(skel->joint_names[i], name) == 0) return (int)i;
    }
    return -1;
}

/* ── Walk cycle foot position ─────────────────────────────────────── */

/**
 * @brief Compute foot IK target position for a walk cycle.
 *
 * @param phase  Walk cycle phase [0, 1).
 * @param rest_x Foot rest position X (left vs right).
 * @param rest_y Foot rest position Y (ground level).
 * @param rest_z Foot rest position Z.
 * @return World-space foot target position.
 */
static vec3_t walk_foot_pos_(float phase, float rest_x,
                              float rest_y, float rest_z) {
    /* Foot traces a figure-D shape:
       - forward stride on ground (phase 0.0–0.5)
       - lift and swing back (phase 0.5–1.0) */
    vec3_t pos;
    pos.x = rest_x;

    if (phase < 0.5f) {
        /* Contact / push phase: foot on ground, slides backward. */
        float t = phase / 0.5f; /* 0→1 during contact. */
        pos.z = rest_z + STRIDE_LENGTH * (0.5f - t);
        pos.y = rest_y;
    } else {
        /* Swing phase: foot lifts and swings forward. */
        float t = (phase - 0.5f) / 0.5f; /* 0→1 during swing. */
        pos.z = rest_z + STRIDE_LENGTH * (t - 0.5f);
        pos.y = rest_y + STEP_HEIGHT * sinf(t * PI);
    }
    return pos;
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Walk Cycle ===\n");

    if (init_gl_context_() != 0) { return 1; }

    /* Shader. */
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
    printf("  Loaded: %u joints, %u IBMs\n", skel.joint_count, ibm_count);

    /* Find control bones. */
    int idx_foot_l = find_bone_(&skel, "c_foot_ik.l");
    int idx_foot_r = find_bone_(&skel, "c_foot_ik.r");
    int idx_traj   = find_bone_(&skel, "c_traj");
    int idx_pos    = find_bone_(&skel, "c_pos");

    printf("  c_foot_ik.l = %d\n", idx_foot_l);
    printf("  c_foot_ik.r = %d\n", idx_foot_r);
    printf("  c_traj = %d\n", idx_traj);
    printf("  c_pos = %d\n", idx_pos);

    if (idx_foot_l < 0 || idx_foot_r < 0 || idx_traj < 0) {
        fprintf(stderr, "Missing required IK bones!\n");
        skeleton_def_destroy(&skel);
        if (ibms) free(ibms);
        cleanup_line_buffer_();
        shader_program_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }

    /* Rest positions of feet (for walk cycle reference). */
    vec3_t rest_foot_l = {
        skel.rest_world[idx_foot_l].m[12],
        skel.rest_world[idx_foot_l].m[13],
        skel.rest_world[idx_foot_l].m[14]
    };
    vec3_t rest_foot_r = {
        skel.rest_world[idx_foot_r].m[12],
        skel.rest_world[idx_foot_r].m[13],
        skel.rest_world[idx_foot_r].m[14]
    };

    printf("  Rest foot L: (%.2f, %.2f, %.2f)\n",
           (double)rest_foot_l.x, (double)rest_foot_l.y, (double)rest_foot_l.z);
    printf("  Rest foot R: (%.2f, %.2f, %.2f)\n",
           (double)rest_foot_r.x, (double)rest_foot_r.y, (double)rest_foot_r.z);

    /* Set up constraint solver with all evaluators (for animation targets). */
    constraint_solver_t solver;
    constraint_solver_init(&solver, skel.joint_count,
                           skel.max_constraints_per_joint);
    ik_solver_register(&solver);
    copy_track_register(&solver);
    limit_constraints_register(&solver);
    surface_vol_register(&solver);
    transform_map_register(&solver);

    /* Allocate pose buffers. */
    mat4_t *pose = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
    mat4_t *local_pose = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
    mat4_t *target_pose = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
    if (!pose || !local_pose || !target_pose) {
        fprintf(stderr, "Failed to allocate pose\n");
        free(pose); free(local_pose); free(target_pose);
        skeleton_def_destroy(&skel);
        if (ibms) free(ibms);
        cleanup_line_buffer_();
        shader_program_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }

    /* Create ragdoll from skeleton (uses colliders + joints from fskel). */
    ragdoll_t ragdoll;
    bool ragdoll_ok = ragdoll_create(&ragdoll, &skel, skel.rest_world);
    if (!ragdoll_ok) {
        fprintf(stderr, "WARNING: ragdoll creation failed, falling back to "
                        "constraint solver only\n");
    } else {
        /* Motor strength = 1.0 (fully animation-driven). */
        ragdoll_set_motor_strength(&ragdoll, 1.0f);
        printf("  Ragdoll: %u bodies, %u joints\n",
               ragdoll.bone_count, ragdoll.joint_count);
    }

    /* Find the root_master bone (child of c_traj, drives the body). */
    int idx_root_master = find_bone_(&skel, "c_root_master.x");
    printf("  c_root_master.x = %d\n", idx_root_master);

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_walk_cycle.mp4";

    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture)
        fprintf(stderr, "WARNING: video capture unavailable\n");

    /* GL state. */
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.06f, 0.06f, 0.10f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;
    int recording = (capture != NULL);
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;

    /* ── Render loop ──────────────────────────────────────────────── */

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float t = (float)f / (float)TOTAL_FRAMES;
        float cycle = t * WALK_SPEED; /* Number of walk cycles completed. */

        /* Walk cycle phase for each foot (180° out of phase). */
        float phase_l = fmodf(cycle, 1.0f);
        float phase_r = fmodf(cycle + 0.5f, 1.0f);

        /* Forward motion of the whole body. */
        float forward_z = cycle * STRIDE_LENGTH;

        /* Start from rest local poses. */
        memcpy(local_pose, skel.rest_local,
               skel.joint_count * sizeof(mat4_t));

        /* Animate c_traj: forward translation in local space.
         * c_traj is child of c_pos (both at origin). */
        local_pose[idx_traj].m[14] += forward_z;

        /* Animate foot IK targets in local space.
         * These are ROOT bones (no parent), so local = world. */
        vec3_t foot_l_target = walk_foot_pos_(
            phase_l, rest_foot_l.x, rest_foot_l.y,
            rest_foot_l.z + forward_z);
        vec3_t foot_r_target = walk_foot_pos_(
            phase_r, rest_foot_r.x, rest_foot_r.y,
            rest_foot_r.z + forward_z);

        local_pose[idx_foot_l].m[12] = foot_l_target.x;
        local_pose[idx_foot_l].m[13] = foot_l_target.y;
        local_pose[idx_foot_l].m[14] = foot_l_target.z;

        local_pose[idx_foot_r].m[12] = foot_r_target.x;
        local_pose[idx_foot_r].m[13] = foot_r_target.y;
        local_pose[idx_foot_r].m[14] = foot_r_target.z;

        /* Add slight hip bob via root_master (body goes up during swing). */
        if (idx_root_master >= 0) {
            float bob = 0.3f * sinf(cycle * 4.0f * PI);
            local_pose[idx_root_master].m[13] += bob;
        }

        /* Initialize world pose (will be computed by solver's FK). */
        memset(target_pose, 0, skel.joint_count * sizeof(mat4_t));

        /* Phase 1: Animation solver produces target poses via FK + constraints.
         * This resolves IK targets, copy rotation/location, etc. */
        constraint_solver_evaluate(&solver, &skel, local_pose,
                                   target_pose, skel.joint_count);

        /* Phase 2: Feed animation targets through ragdoll pipeline. */
        if (ragdoll_ok) {
            /* Update motor targets from animation solver output. */
            ragdoll_update_motor_targets(&ragdoll, target_pose,
                                         skel.joint_count);

            /* Sync ragdoll body positions from animation targets.
             * In the full engine pipeline, XPBD would solve here.
             * For this visual test, we directly apply animation targets
             * to ragdoll bodies (motor strength = 1.0). */
            anim_bones_to_bodies(target_pose, skel.colliders,
                                 ragdoll.bodies, skel.joint_count);

            /* Sync physics results back to bone world matrices. */
            ragdoll_sync_from_physics(&ragdoll);
            memcpy(pose, ragdoll.bone_world,
                   skel.joint_count * sizeof(mat4_t));
        } else {
            /* Fallback: use constraint solver output directly. */
            memcpy(pose, target_pose, skel.joint_count * sizeof(mat4_t));
        }

        /* ── Draw ─────────────────────────────────────────────── */
        begin_lines_();

        /* Ground grid. */
        draw_ground_grid_(0.0f, 15.0f, 30);

        /* Skeleton. */
        draw_skeleton_(&skel, pose);

        /* Draw foot IK targets as red/green crosses. */
        {
            float sz = 0.3f;
            /* Left foot: green. */
            add_line_((vec3_t){foot_l_target.x - sz, foot_l_target.y,
                               foot_l_target.z},
                      (vec3_t){foot_l_target.x + sz, foot_l_target.y,
                               foot_l_target.z},
                      0.2f, 1.0f, 0.2f);
            add_line_((vec3_t){foot_l_target.x, foot_l_target.y - sz,
                               foot_l_target.z},
                      (vec3_t){foot_l_target.x, foot_l_target.y + sz,
                               foot_l_target.z},
                      0.2f, 1.0f, 0.2f);

            /* Right foot: red. */
            add_line_((vec3_t){foot_r_target.x - sz, foot_r_target.y,
                               foot_r_target.z},
                      (vec3_t){foot_r_target.x + sz, foot_r_target.y,
                               foot_r_target.z},
                      1.0f, 0.2f, 0.2f);
            add_line_((vec3_t){foot_r_target.x, foot_r_target.y - sz,
                               foot_r_target.z},
                      (vec3_t){foot_r_target.x, foot_r_target.y + sz,
                               foot_r_target.z},
                      1.0f, 0.2f, 0.2f);
        }

        /* Camera: follow the skeleton at a slight angle. */
        mat4_t view, proj;
        float cam_angle = 0.8f + t * PI * 0.3f;
        float cam_radius = 25.0f;
        float cam_x = cam_radius * cosf(cam_angle);
        float cam_z = forward_z + cam_radius * sinf(cam_angle);
        float cam_y = 10.0f;
        mat4_look_at((vec3_t){cam_x, cam_y, cam_z},
                     (vec3_t){0.f, 5.0f, forward_z},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(45.0f * PI / 180.0f,
                         (float)WINDOW_W / (float)WINDOW_H,
                         0.1f, 200.0f, &proj);
        mat4_t mvp = mat4_mul(proj, view);

        flush_lines_(&mvp, &shader, u_mvp_loc);

        /* GL errors. */
        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        /* Snapshots. */
        int snap_frames[] = { 0, 30, 60, 89, 119 };
        for (int si = 0; si < 5; ++si) {
            if (f == snap_frames[si]) {
                char path[256];
                snprintf(path, sizeof(path),
                         "tests/output/p005_walk_frame_%03d.ppm", f);
                save_ppm_(path, WINDOW_W, WINDOW_H);
                printf("  Snapshot: %s\n", path);
                break;
            }
        }

        if (capture) {
            fr_video_capture_submit_frame(capture);
            glFlush();
        }
        SDL_GL_SwapWindow(g_window);
        ++frame_count;

        if (recording) {
            uint32_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < frame_interval_ms)
                SDL_Delay(frame_interval_ms - elapsed);
        }
    }

    /* Cleanup. */
    if (capture) fr_video_capture_destroy(capture);
    cleanup_line_buffer_();
    shader_program_destroy(&shader);
    constraint_solver_destroy(&solver);
    if (ragdoll_ok) ragdoll_destroy(&ragdoll);
    free(pose);
    free(local_pose);
    free(target_pose);
    skeleton_def_destroy(&skel);
    if (ibms) free(ibms);
    cleanup_gl_context_();

    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);

    int pass = (frame_count >= TOTAL_FRAMES) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
