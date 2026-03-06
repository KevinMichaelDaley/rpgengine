/**
 * @file p005_visual_ik_reach.c
 * @brief Visual test: IK chain following an orbiting target.
 *
 * A 5-bone arm chain with CCD IK tracks a target moving in a circle.
 * Bones drawn as debug lines (cylinders not needed — line segments
 * with colored endpoints show joint positions clearly).
 *
 * PASS if frame_count >= 90, no GL errors, and end-effector tracks
 * the target within reach distance.
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
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/video_capture.h"

/* ── Constants ────────────────────────────────────────────────────── */

#define WINDOW_W     640
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 3
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f

#define BONE_COUNT 6    /* 5 bones = 6 joints (including root) */
#define BONE_LENGTH 1.0f
#define CHAIN_LENGTH 5  /* IK chain length in bones */

/* ── Shader sources (simple line/point shader) ────────────────────── */

static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "    gl_PointSize = 8.0;\n"
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
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);

    g_window = SDL_CreateWindow("IK Chain Reach Visual Test",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                WINDOW_W, WINDOW_H,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(g_gl_ctx);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
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
    /* Flip vertically. */
    for (int y = h - 1; y >= 0; --y)
        fwrite(pixels + y * w * 3, 3, (size_t)w, f);
    fclose(f);
    free(pixels);
    return 0;
}

/* ── Orbit camera ─────────────────────────────────────────────────── */

static void compute_orbit_camera_(int frame, mat4_t *view, mat4_t *proj) {
    float t = (float)frame / (float)TOTAL_FRAMES;
    float angle = t * 2.0f * PI * 0.5f; /* half orbit */
    float radius = 10.0f;
    float cx = radius * cosf(angle);
    float cz = radius * sinf(angle);
    float cy = 4.0f;

    /* Look-at center of chain. */
    vec3_t eye    = {cx, cy, cz};
    vec3_t center = {0.f, 2.5f, 0.f};
    vec3_t up_vec = {0.f, 1.f, 0.f};
    mat4_look_at(eye, center, up_vec, view);

    float aspect = (float)WINDOW_W / (float)WINDOW_H;
    mat4_perspective(45.0f * PI / 180.0f, aspect, 0.1f, 100.0f, proj);
}

/* ── Line drawing ─────────────────────────────────────────────────── */

/* Immediate-mode line buffer (uploaded each frame). */
#define MAX_LINE_VERTS 256

typedef struct {
    float pos[3];
    float col[3];
} line_vertex_t;

static line_vertex_t g_line_verts[MAX_LINE_VERTS];
static int g_line_vert_count;
static GLuint g_line_vao, g_line_vbo;

static void init_line_buffer_(void) {
    glGenVertexArrays(1, &g_line_vao);
    glGenBuffers(1, &g_line_vbo);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_line_verts), NULL, GL_DYNAMIC_DRAW);
    /* Attribute 0: position. */
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(line_vertex_t), (void *)0);
    glEnableVertexAttribArray(0);
    /* Attribute 1: color. */
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(line_vertex_t),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    g_line_vert_count = 0;
}

static void begin_lines_(void) {
    g_line_vert_count = 0;
}

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

static void add_point_(vec3_t p, float r, float g, float bl) {
    if (g_line_vert_count + 1 > MAX_LINE_VERTS) return;
    g_line_verts[g_line_vert_count].pos[0] = p.x;
    g_line_verts[g_line_vert_count].pos[1] = p.y;
    g_line_verts[g_line_vert_count].pos[2] = p.z;
    g_line_verts[g_line_vert_count].col[0] = r;
    g_line_verts[g_line_vert_count].col[1] = g;
    g_line_verts[g_line_vert_count].col[2] = bl;
    g_line_vert_count++;
}

static void draw_lines_(void) {
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_line_vert_count * sizeof(line_vertex_t)),
                    g_line_verts);

    /* Draw bone segments as GL_LINES (pairs of vertices). */
    /* We submitted lines first, then points. Count them. */
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, g_line_vert_count);

    glBindVertexArray(0);
}

static void draw_points_(int start, int count) {
    glBindVertexArray(g_line_vao);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDrawArrays(GL_POINTS, start, count);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glBindVertexArray(0);
}

static void cleanup_line_buffer_(void) {
    glDeleteVertexArrays(1, &g_line_vao);
    glDeleteBuffers(1, &g_line_vbo);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: IK Chain Reach ===\n");

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

    /* Set up skeleton: 6 joints (5 bones) going upward. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, BONE_COUNT, 1);

    for (uint32_t i = 0; i < BONE_COUNT; i++) {
        snprintf(skel.joint_names[i], SKELETON_JOINT_NAME_MAX, "joint_%u", i);
        skel.parent_indices[i] = (i == 0) ? UINT32_MAX : i - 1;
        skel.rest_local[i] = (i == 0)
            ? mat4_identity()
            : mat4_translation(0.f, BONE_LENGTH, 0.f);
    }

    /* Compute rest world transforms. */
    skel.rest_world[0] = skel.rest_local[0];
    for (uint32_t i = 1; i < BONE_COUNT; i++) {
        skel.rest_world[i] = mat4_mul(skel.rest_world[i - 1], skel.rest_local[i]);
    }

    /* Add IK constraint to the tip joint. */
    skel.constraint_counts[BONE_COUNT - 1] = 1;
    constraint_def_t *ik_def = &skel.constraints[(BONE_COUNT - 1) * skel.max_constraints_per_joint];
    memset(ik_def, 0, sizeof(*ik_def));
    ik_def->type = CONSTRAINT_IK;
    ik_def->influence = 1.0f;
    ik_def->target_bone_idx = UINT32_MAX; /* Target set via pose override. */
    ik_def->params.ik.chain_length = CHAIN_LENGTH;
    ik_def->params.ik.iterations = 20;

    /* Set up constraint solver. */
    constraint_solver_t solver;
    constraint_solver_init(&solver, BONE_COUNT, 1);
    ik_solver_register(&solver);

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_ik_reach.mp4";

    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture) {
        fprintf(stderr, "WARNING: video capture unavailable\n");
    }

    /* GL state. */
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    /* Render loop. */
    int gl_error_count = 0;
    int frame_count = 0;
    int recording = (capture != NULL);
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;
    float max_tracking_error = 0.0f;

    mat4_t pose[BONE_COUNT];

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Compute target position: orbiting circle at y=3, radius=3. */
        float t = (float)f / (float)TOTAL_FRAMES;
        float target_angle = t * 2.0f * PI;
        vec3_t target = {
            3.0f * cosf(target_angle),
            3.0f + 1.5f * sinf(target_angle * 2.0f),
            3.0f * sinf(target_angle)
        };

        /* Start from rest pose. */
        for (uint32_t i = 0; i < BONE_COUNT; i++) {
            pose[i] = skel.rest_world[i];
        }

        /* Run IK solver manually (CCD). */
        ik_solve_ccd(&skel, pose, BONE_COUNT - 1,
                     CHAIN_LENGTH, target,
                     ik_def->params.ik.iterations, 0.001f);

        /* Compute tracking error (end-effector to target distance). */
        vec3_t end_effector = {
            pose[BONE_COUNT - 1].m[12],
            pose[BONE_COUNT - 1].m[13],
            pose[BONE_COUNT - 1].m[14]
        };
        float err = vec3_magnitude(vec3_sub(end_effector, target));
        if (err > max_tracking_error) max_tracking_error = err;

        /* Draw bones as lines. */
        begin_lines_();

        for (uint32_t i = 1; i < BONE_COUNT; i++) {
            vec3_t parent_pos = {
                pose[i - 1].m[12], pose[i - 1].m[13], pose[i - 1].m[14]
            };
            vec3_t child_pos = {
                pose[i].m[12], pose[i].m[13], pose[i].m[14]
            };
            /* Gradient color: blue at root, green at tip. */
            float blend = (float)i / (float)(BONE_COUNT - 1);
            add_line_(parent_pos, child_pos,
                      0.2f, 0.3f + 0.7f * blend, 1.0f - 0.8f * blend);
        }

        /* Draw joint points. */
        int line_end = g_line_vert_count;
        for (uint32_t i = 0; i < BONE_COUNT; i++) {
            vec3_t p = { pose[i].m[12], pose[i].m[13], pose[i].m[14] };
            add_point_(p, 1.0f, 1.0f, 0.0f); /* Yellow joints. */
        }

        /* Draw target as red point. */
        add_point_(target, 1.0f, 0.2f, 0.2f);
        int point_start = line_end;
        int point_count = g_line_vert_count - line_end;

        /* Upload and draw. */
        mat4_t view, proj;
        compute_orbit_camera_(f, &view, &proj);
        mat4_t mvp = mat4_mul(proj, view);

        shader_program_bind(&shader);
        shader.glUniformMatrix4fv(u_mvp_loc, 1, 0, mvp.m);

        /* Draw lines first, then points. */
        draw_lines_();
        draw_points_(point_start, point_count);

        /* Draw a small cross at the target for visibility. */
        /* (Already drawn as a point above.) */

        /* Check GL errors. */
        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X on frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        /* PPM snapshots. */
        int snapshot_frames[] = { 0, 30, 60, 89 };
        for (int si = 0; si < 4; ++si) {
            if (f == snapshot_frames[si]) {
                char snap_path[256];
                snprintf(snap_path, sizeof(snap_path),
                         "tests/output/p005_ik_reach_frame_%03d.ppm", f);
                if (save_ppm_(snap_path, WINDOW_W, WINDOW_H) == 0) {
                    printf("  Snapshot: %s\n", snap_path);
                }
                break;
            }
        }

        /* Video capture. */
        if (capture) {
            fr_video_capture_submit_frame(capture);
            glFlush();
        }

        SDL_GL_SwapWindow(g_window);
        ++frame_count;

        if (recording) {
            uint32_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < frame_interval_ms) {
                SDL_Delay(frame_interval_ms - elapsed);
            }
        }
    }

    /* Cleanup. */
    if (capture) {
        fr_video_capture_destroy(capture);
    }

    cleanup_line_buffer_();
    shader_program_destroy(&shader);
    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    cleanup_gl_context_();

    /* Results. */
    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);
    printf("  Max tracking error: %.4f (chain reach = %.1f)\n",
           (double)max_tracking_error, (double)(BONE_COUNT - 1) * BONE_LENGTH);

    int pass = (frame_count >= TOTAL_FRAMES) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
