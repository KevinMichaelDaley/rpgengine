/**
 * @file p005_visual_tracking.c
 * @brief Visual test: damped track and copy-rotation constraints.
 *
 * Two bones: one tracks a moving target using DAMPED_TRACK, another
 * copies the first's rotation. Debug line rendering shows orientation
 * axes at each joint.
 *
 * PASS if frame_count >= 90 and no GL errors.
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
#include "ferrum/animation/copy_track.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/video_capture.h"

#define WINDOW_W     640
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 3
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f

#define BONE_COUNT 3
#define AXIS_LEN   0.6f

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
    "    gl_PointSize = 10.0;\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = vec4(v_color, 1.0);\n"
    "}\n";

/* ── GL context (same boilerplate) ────────────────────────────────── */

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
    g_window = SDL_CreateWindow("Tracking Visual Test",
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

/* ── Line buffer ──────────────────────────────────────────────────── */

#define MAX_LINE_VERTS 512

typedef struct { float pos[3]; float col[3]; } line_vertex_t;
static line_vertex_t g_line_verts[MAX_LINE_VERTS];
static int g_line_vert_count;
static GLuint g_line_vao, g_line_vbo;

static void init_line_buffer_(void) {
    glGenVertexArrays(1, &g_line_vao);
    glGenBuffers(1, &g_line_vbo);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_line_verts), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(line_vertex_t), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(line_vertex_t),
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

static void flush_lines_(const mat4_t *mvp, shader_program_t *sh, int32_t loc) {
    shader_program_bind(sh);
    sh->glUniformMatrix4fv(loc, 1, 0, mvp->m);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(g_line_vert_count * sizeof(line_vertex_t)),
                    g_line_verts);
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, g_line_vert_count);
    glBindVertexArray(0);
}

static void cleanup_line_buffer_(void) {
    glDeleteVertexArrays(1, &g_line_vao);
    glDeleteBuffers(1, &g_line_vbo);
}

/* ── Draw orientation axes at a transform ─────────────────────────── */

static void draw_axes_(const mat4_t *m, float len, float alpha) {
    vec3_t origin = { m->m[12], m->m[13], m->m[14] };
    /* X axis (red). */
    vec3_t xend = { origin.x + m->m[0] * len,
                    origin.y + m->m[1] * len,
                    origin.z + m->m[2] * len };
    add_line_(origin, xend, 1.0f * alpha, 0.2f * alpha, 0.2f * alpha);
    /* Y axis (green). */
    vec3_t yend = { origin.x + m->m[4] * len,
                    origin.y + m->m[5] * len,
                    origin.z + m->m[6] * len };
    add_line_(origin, yend, 0.2f * alpha, 1.0f * alpha, 0.2f * alpha);
    /* Z axis (blue). */
    vec3_t zend = { origin.x + m->m[8] * len,
                    origin.y + m->m[9] * len,
                    origin.z + m->m[10] * len };
    add_line_(origin, zend, 0.2f * alpha, 0.2f * alpha, 1.0f * alpha);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Tracking + Copy Rotation ===\n");

    if (init_gl_context_() != 0) return 1;

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

    /*
     * Skeleton:
     *   Bone 0: root at origin
     *   Bone 1: tracker at (-2, 0, 0) — has DAMPED_TRACK
     *   Bone 2: copier at (2, 0, 0) — has COPY_ROTATION from bone 1
     *
     * A "target" bone is virtual — we move bone 0 to act as the
     * tracked target. The tracker bone tracks bone 0.
     */
    skeleton_def_t skel;
    skeleton_def_init(&skel, BONE_COUNT, 1);

    snprintf(skel.joint_names[0], SKELETON_JOINT_NAME_MAX, "target");
    skel.parent_indices[0] = UINT32_MAX;
    skel.rest_local[0] = mat4_translation(0.f, 3.f, 0.f);

    snprintf(skel.joint_names[1], SKELETON_JOINT_NAME_MAX, "tracker");
    skel.parent_indices[1] = UINT32_MAX; /* Independent bone. */
    skel.rest_local[1] = mat4_translation(-2.f, 0.f, 0.f);

    snprintf(skel.joint_names[2], SKELETON_JOINT_NAME_MAX, "copier");
    skel.parent_indices[2] = UINT32_MAX; /* Independent bone. */
    skel.rest_local[2] = mat4_translation(2.f, 0.f, 0.f);

    for (uint32_t i = 0; i < BONE_COUNT; i++)
        skel.rest_world[i] = skel.rest_local[i];

    /* Damped track on bone 1 → tracks bone 0. */
    skel.constraint_counts[1] = 1;
    constraint_def_t *track_def = &skel.constraints[1 * skel.max_constraints_per_joint];
    memset(track_def, 0, sizeof(*track_def));
    track_def->type = CONSTRAINT_DAMPED_TRACK;
    track_def->influence = 1.0f;
    track_def->target_bone_idx = 0;
    track_def->params.damped_track.track_axis = CONSTRAINT_AXIS_Y;

    /* Copy rotation on bone 2 → copies from bone 1. */
    skel.constraint_counts[2] = 1;
    constraint_def_t *copy_def = &skel.constraints[2 * skel.max_constraints_per_joint];
    memset(copy_def, 0, sizeof(*copy_def));
    copy_def->type = CONSTRAINT_COPY_ROTATION;
    copy_def->influence = 1.0f;
    copy_def->target_bone_idx = 1;
    copy_def->params.copy_rotation.use_x = true;
    copy_def->params.copy_rotation.use_y = true;
    copy_def->params.copy_rotation.use_z = true;

    constraint_solver_t solver;
    constraint_solver_init(&solver, BONE_COUNT, 1);
    copy_track_register(&solver);

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_tracking.mp4";

    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture) fprintf(stderr, "WARNING: video capture unavailable\n");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;
    int recording = (capture != NULL);
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;

    mat4_t pose[BONE_COUNT];

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float t = (float)f / (float)TOTAL_FRAMES;

        /* Animate target bone (bone 0) in a figure-8 pattern. */
        float angle = t * 2.0f * PI;
        float tx = 2.0f * sinf(angle);
        float ty = 3.0f + 1.5f * sinf(angle * 2.0f);
        float tz = 1.5f * cosf(angle);

        /* Set up poses. */
        pose[0] = mat4_translation(tx, ty, tz);
        pose[1] = skel.rest_world[1];
        pose[2] = skel.rest_world[2];

        /* Evaluate constraints. */
        constraint_solver_evaluate(&solver, &skel, pose, BONE_COUNT);

        /* Draw. */
        begin_lines_();

        /* Draw target as a bright white cross. */
        {
            float sz = 0.15f;
            vec3_t tp = { tx, ty, tz };
            add_line_((vec3_t){tp.x-sz, tp.y, tp.z}, (vec3_t){tp.x+sz, tp.y, tp.z},
                      1.f, 1.f, 1.f);
            add_line_((vec3_t){tp.x, tp.y-sz, tp.z}, (vec3_t){tp.x, tp.y+sz, tp.z},
                      1.f, 1.f, 1.f);
            add_line_((vec3_t){tp.x, tp.y, tp.z-sz}, (vec3_t){tp.x, tp.y, tp.z+sz},
                      1.f, 1.f, 1.f);
        }

        /* Draw orientation axes for tracker and copier. */
        draw_axes_(&pose[1], AXIS_LEN, 1.0f);
        draw_axes_(&pose[2], AXIS_LEN, 0.7f);

        /* Labels via small offset lines. */
        {
            vec3_t tracker_pos = { pose[1].m[12], pose[1].m[13], pose[1].m[14] };
            vec3_t copier_pos  = { pose[2].m[12], pose[2].m[13], pose[2].m[14] };
            /* Connect them with a dotted-style faint line. */
            add_line_(tracker_pos, copier_pos, 0.3f, 0.3f, 0.3f);
        }

        /* Camera. */
        mat4_t view, proj;
        float cam_angle = t * PI * 0.3f + 0.5f;
        float cx = 10.0f * cosf(cam_angle);
        float cz = 10.0f * sinf(cam_angle);
        mat4_look_at((vec3_t){cx, 5.0f, cz},
                     (vec3_t){0.f, 2.0f, 0.f},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(45.0f * PI / 180.0f,
                         (float)WINDOW_W / (float)WINDOW_H,
                         0.1f, 100.0f, &proj);
        mat4_t mvp = mat4_mul(proj, view);

        flush_lines_(&mvp, &shader, u_mvp_loc);

        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        int snap_frames[] = { 0, 30, 60, 89 };
        for (int si = 0; si < 4; ++si) {
            if (f == snap_frames[si]) {
                char path[256];
                snprintf(path, sizeof(path),
                         "tests/output/p005_tracking_frame_%03d.ppm", f);
                save_ppm_(path, WINDOW_W, WINDOW_H);
                printf("  Snapshot: %s\n", path);
                break;
            }
        }

        if (capture) { fr_video_capture_submit_frame(capture); glFlush(); }
        SDL_GL_SwapWindow(g_window);
        ++frame_count;

        if (recording) {
            uint32_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < frame_interval_ms)
                SDL_Delay(frame_interval_ms - elapsed);
        }
    }

    if (capture) fr_video_capture_destroy(capture);
    cleanup_line_buffer_();
    shader_program_destroy(&shader);
    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    cleanup_gl_context_();

    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);

    int pass = (frame_count >= TOTAL_FRAMES) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
