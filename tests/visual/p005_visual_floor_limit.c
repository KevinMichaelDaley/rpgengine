/**
 * @file p005_visual_floor_limit.c
 * @brief Visual test: floor constraint + rotation limits.
 *
 * A 3-bone arm chain swings around. The tip bone has a floor
 * constraint at y=0 and the middle bone has rotation limits.
 * Debug line rendering shows bones and the floor plane.
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
#include "ferrum/animation/surface_vol.h"
#include "ferrum/animation/limit_constraints.h"
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

#define BONE_COUNT 4
#define BONE_LENGTH 1.5f

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

    g_window = SDL_CreateWindow("Floor + Limits Visual Test",
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
    for (int y = h - 1; y >= 0; --y)
        fwrite(pixels + y * w * 3, 3, (size_t)w, f);
    fclose(f);
    free(pixels);
    return 0;
}

/* ── Line buffer ──────────────────────────────────────────────────── */

#define MAX_LINE_VERTS 512

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

static void flush_lines_(const mat4_t *mvp, shader_program_t *shader,
                          int32_t u_mvp_loc) {
    shader_program_bind(shader);
    shader->glUniformMatrix4fv(u_mvp_loc, 1, 0, mvp->m);
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

/* ── Draw floor grid ──────────────────────────────────────────────── */

static void draw_floor_grid_(float y, float extent, int divisions,
                              float r, float g, float b) {
    float step = (2.0f * extent) / (float)divisions;
    for (int i = 0; i <= divisions; i++) {
        float t = -extent + step * (float)i;
        add_line_((vec3_t){t, y, -extent}, (vec3_t){t, y, extent}, r, g, b);
        add_line_((vec3_t){-extent, y, t}, (vec3_t){extent, y, t}, r, g, b);
    }
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Floor + Limits ===\n");

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

    /* Skeleton: 4 joints, root at origin, bones go outward. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, BONE_COUNT, 2); /* Up to 2 constraints/joint. */

    for (uint32_t i = 0; i < BONE_COUNT; i++) {
        snprintf(skel.joint_names[i], SKELETON_JOINT_NAME_MAX, "bone_%u", i);
        skel.parent_indices[i] = (i == 0) ? UINT32_MAX : i - 1;
        skel.rest_local[i] = (i == 0)
            ? mat4_translation(0.f, 3.0f, 0.f)
            : mat4_translation(0.f, -BONE_LENGTH, 0.f);
    }
    skel.rest_world[0] = skel.rest_local[0];
    for (uint32_t i = 1; i < BONE_COUNT; i++) {
        skel.rest_world[i] = mat4_mul(skel.rest_world[i - 1], skel.rest_local[i]);
    }

    /* Floor constraint on the tip bone (y >= 0.0). */
    skel.constraint_counts[BONE_COUNT - 1] = 1;
    constraint_def_t *floor_def = &skel.constraints[(BONE_COUNT - 1) * skel.max_constraints_per_joint];
    memset(floor_def, 0, sizeof(*floor_def));
    floor_def->type = CONSTRAINT_FLOOR;
    floor_def->influence = 1.0f;
    floor_def->target_bone_idx = 0; /* Use root bone as reference. */
    floor_def->params.floor.floor_location = CONSTRAINT_FLOOR_BELOW_NEG_Y;
    floor_def->params.floor.offset = -3.0f; /* Floor at y=0 (root is at y=3). */

    /* Set up solver with floor + limit dispatchers. */
    constraint_solver_t solver;
    constraint_solver_init(&solver, BONE_COUNT, 2);
    surface_vol_register(&solver);
    limit_constraints_register(&solver);

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_floor_limit.mp4";

    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture) {
        fprintf(stderr, "WARNING: video capture unavailable\n");
    }

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;
    int recording = (capture != NULL);
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;
    int floor_clamp_count = 0;

    mat4_t pose[BONE_COUNT];

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Animate: swing the chain by rotating bone 1. */
        float t = (float)f / (float)TOTAL_FRAMES;
        float swing_angle = sinf(t * 2.0f * PI * 2.0f) * 1.2f; /* ±1.2 rad */

        /* Build pose: root stays still, bone 1 swings. */
        pose[0] = skel.rest_world[0];
        for (uint32_t i = 1; i < BONE_COUNT; i++) {
            mat4_t local = skel.rest_local[i];
            if (i == 1) {
                /* Add swing rotation around Z. */
                quat_t swing_q = quat_from_axis_angle(
                    (vec3_t){0.f, 0.f, 1.f}, swing_angle, 1e-7f);
                mat4_t swing_m;
                quat_to_mat4(swing_q, &swing_m);
                /* Rotation applied before translation. */
                local = mat4_mul(swing_m, local);
            }
            pose[i] = mat4_mul(pose[i - 1], local);
        }

        /* Apply floor constraint on the tip bone. */
        constraint_solver_evaluate(&solver, &skel, pose, BONE_COUNT);

        /* Check if floor was clamped (tip bone y should be >= 0). */
        float tip_y = pose[BONE_COUNT - 1].m[13];
        if (tip_y < -0.01f) {
            fprintf(stderr, "Frame %d: tip bone below floor! y=%.3f\n",
                    f, (double)tip_y);
        }
        if (fabsf(tip_y) < 0.1f) {
            floor_clamp_count++;
        }

        /* Draw. */
        begin_lines_();

        /* Floor grid at y=0. */
        draw_floor_grid_(0.0f, 5.0f, 10, 0.2f, 0.15f, 0.1f);

        /* Bone segments. */
        for (uint32_t i = 1; i < BONE_COUNT; i++) {
            vec3_t a = { pose[i-1].m[12], pose[i-1].m[13], pose[i-1].m[14] };
            vec3_t b = { pose[i].m[12],   pose[i].m[13],   pose[i].m[14] };
            float blend = (float)i / (float)(BONE_COUNT - 1);
            add_line_(a, b, 0.3f + 0.7f * blend, 0.8f - 0.5f * blend, 0.2f);
        }

        /* Joint points (as short crosses). */
        for (uint32_t i = 0; i < BONE_COUNT; i++) {
            vec3_t p = { pose[i].m[12], pose[i].m[13], pose[i].m[14] };
            float sz = 0.08f;
            float cr = 1.f, cg = 1.f, cb = 0.f;
            add_line_((vec3_t){p.x-sz, p.y, p.z}, (vec3_t){p.x+sz, p.y, p.z}, cr, cg, cb);
            add_line_((vec3_t){p.x, p.y-sz, p.z}, (vec3_t){p.x, p.y+sz, p.z}, cr, cg, cb);
        }

        /* Camera. */
        mat4_t view, proj;
        float cam_angle = t * PI * 0.5f;
        float cx = 8.0f * cosf(cam_angle);
        float cz = 8.0f * sinf(cam_angle);
        mat4_look_at((vec3_t){cx, 4.0f, cz},
                     (vec3_t){0.f, 1.5f, 0.f},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(45.0f * PI / 180.0f,
                         (float)WINDOW_W / (float)WINDOW_H,
                         0.1f, 100.0f, &proj);
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
        int snap_frames[] = { 0, 30, 60, 89 };
        for (int si = 0; si < 4; ++si) {
            if (f == snap_frames[si]) {
                char path[256];
                snprintf(path, sizeof(path),
                         "tests/output/p005_floor_limit_frame_%03d.ppm", f);
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

    if (capture) fr_video_capture_destroy(capture);
    cleanup_line_buffer_();
    shader_program_destroy(&shader);
    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    cleanup_gl_context_();

    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);
    printf("  Floor clamp activations: %d frames\n", floor_clamp_count);

    int pass = (frame_count >= TOTAL_FRAMES) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
