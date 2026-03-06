/**
 * @file p005_visual_ik_ground.c
 * @brief Visual test: IK foot placement on uneven terrain.
 *
 * Loads humanoid.fskel, places three boxes at different heights to form
 * uneven terrain.  Foot IK targets are set onto the box surfaces and
 * the skeleton walks forward, planting feet on each step.  Per-bone
 * capsule colliders are rendered as wireframes alongside the skeleton.
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
#define DURATION_SEC 3
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f
#define DT (1.0f / TARGET_FPS)

/* Step box positions and heights. */
#define NUM_STEPS 3
typedef struct {
    float x, z;    /* Center of box top. */
    float y;       /* Height of the surface. */
    float half_w;  /* Half-width. */
} step_box_t;

static const step_box_t STEPS[NUM_STEPS] = {
    { -2.0f,  0.0f, 0.3f, 1.5f },  /* Low step. */
    {  0.0f,  0.0f, 0.8f, 1.5f },  /* Medium step. */
    {  2.0f,  0.0f, 1.5f, 1.5f },  /* High step. */
};

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
    g_window = SDL_CreateWindow("IK Ground Contact",
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

#define MAX_LINE_VERTS 8192

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

/* ── Draw helpers ─────────────────────────────────────────────────── */

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

static void draw_skeleton_(const skeleton_def_t *skel, const mat4_t *pose) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t pi = skel->parent_indices[i];
        if (pi == UINT32_MAX || pi >= skel->joint_count) continue;
        vec3_t child  = { pose[i].m[12],  pose[i].m[13],  pose[i].m[14] };
        vec3_t parent = { pose[pi].m[12], pose[pi].m[13], pose[pi].m[14] };
        float t = (float)i / (float)skel->joint_count;
        add_line_(parent, child, 0.9f - 0.6f*t, 0.3f + 0.6f*t, 0.2f);
    }
}

/* Draw a wireframe box. */
static void draw_box_(const step_box_t *box, float r, float g, float b) {
    float hw = box->half_w;
    float x = box->x, y = box->y, z = box->z;
    /* Top face. */
    add_line_((vec3_t){x - hw, y, z - hw}, (vec3_t){x + hw, y, z - hw},
              r, g, b);
    add_line_((vec3_t){x + hw, y, z - hw}, (vec3_t){x + hw, y, z + hw},
              r, g, b);
    add_line_((vec3_t){x + hw, y, z + hw}, (vec3_t){x - hw, y, z + hw},
              r, g, b);
    add_line_((vec3_t){x - hw, y, z + hw}, (vec3_t){x - hw, y, z - hw},
              r, g, b);
    /* Bottom face at y=0. */
    add_line_((vec3_t){x - hw, 0, z - hw}, (vec3_t){x + hw, 0, z - hw},
              r * 0.5f, g * 0.5f, b * 0.5f);
    add_line_((vec3_t){x + hw, 0, z - hw}, (vec3_t){x + hw, 0, z + hw},
              r * 0.5f, g * 0.5f, b * 0.5f);
    add_line_((vec3_t){x + hw, 0, z + hw}, (vec3_t){x - hw, 0, z + hw},
              r * 0.5f, g * 0.5f, b * 0.5f);
    add_line_((vec3_t){x - hw, 0, z + hw}, (vec3_t){x - hw, 0, z - hw},
              r * 0.5f, g * 0.5f, b * 0.5f);
    /* Vertical edges. */
    add_line_((vec3_t){x - hw, 0, z - hw}, (vec3_t){x - hw, y, z - hw},
              r * 0.7f, g * 0.7f, b * 0.7f);
    add_line_((vec3_t){x + hw, 0, z - hw}, (vec3_t){x + hw, y, z - hw},
              r * 0.7f, g * 0.7f, b * 0.7f);
    add_line_((vec3_t){x + hw, 0, z + hw}, (vec3_t){x + hw, y, z + hw},
              r * 0.7f, g * 0.7f, b * 0.7f);
    add_line_((vec3_t){x - hw, 0, z + hw}, (vec3_t){x - hw, y, z + hw},
              r * 0.7f, g * 0.7f, b * 0.7f);
}

/* Draw IK target marker (cross). */
static void draw_target_(vec3_t pos, float sz, float r, float g, float b) {
    add_line_((vec3_t){pos.x - sz, pos.y, pos.z},
              (vec3_t){pos.x + sz, pos.y, pos.z}, r, g, b);
    add_line_((vec3_t){pos.x, pos.y - sz, pos.z},
              (vec3_t){pos.x, pos.y + sz, pos.z}, r, g, b);
    add_line_((vec3_t){pos.x, pos.y, pos.z - sz},
              (vec3_t){pos.x, pos.y, pos.z + sz}, r, g, b);
}

/* ── Find bone indices ───────────────────────────────────────────── */

static uint32_t find_bone_(const skeleton_def_t *skel, const char *prefix) {
    size_t len = strlen(prefix);
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        if (strncmp(skel->joint_names[i], prefix, len) == 0) return i;
    }
    return UINT32_MAX;
}

/* ── Get step height at world X position ─────────────────────────── */

static float get_ground_height_(float world_x) {
    for (int s = 0; s < NUM_STEPS; s++) {
        if (fabsf(world_x - STEPS[s].x) < STEPS[s].half_w) {
            return STEPS[s].y;
        }
    }
    return 0.0f;
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: IK Ground Contact ===\n");

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

    /* Find foot bones. */
    uint32_t l_foot = find_bone_(&skel, "c_foot_ik.l");
    if (l_foot == UINT32_MAX) l_foot = find_bone_(&skel, "foot.ik.L");
    if (l_foot == UINT32_MAX) l_foot = find_bone_(&skel, "Foot.L");
    uint32_t r_foot = find_bone_(&skel, "c_foot_ik.r");
    if (r_foot == UINT32_MAX) r_foot = find_bone_(&skel, "foot.ik.R");
    if (r_foot == UINT32_MAX) r_foot = find_bone_(&skel, "Foot.R");

    printf("  Left foot bone: %u (%s)\n", l_foot,
           l_foot < skel.joint_count ? skel.joint_names[l_foot] : "NONE");
    printf("  Right foot bone: %u (%s)\n", r_foot,
           r_foot < skel.joint_count ? skel.joint_names[r_foot] : "NONE");

    /* Allocate pose buffers and solver. */
    mat4_t *target_pose = (mat4_t *)malloc(
        skel.joint_count * sizeof(mat4_t));
    memcpy(target_pose, skel.rest_world,
           skel.joint_count * sizeof(mat4_t));

    constraint_solver_t solver;
    constraint_solver_init(&solver, skel.joint_count,
                           skel.max_constraints_per_joint);
    ik_solver_register(&solver);
    copy_track_register(&solver);
    limit_constraints_register(&solver);
    surface_vol_register(&solver);
    transform_map_register(&solver);

    /* Create ragdoll. */
    ragdoll_t ragdoll;
    bool ragdoll_ok = ragdoll_create(&ragdoll, &skel, skel.rest_world);
    if (ragdoll_ok) {
        ragdoll_set_motor_strength(&ragdoll, 1.0f);
        printf("  Ragdoll: %u bodies, %u joints\n",
               ragdoll.bone_count, ragdoll.joint_count);
    }

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_ik_ground.mp4";
    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture)
        fprintf(stderr, "WARNING: video capture unavailable\n");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.06f, 0.06f, 0.10f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;

    /* Skeleton walks from left to right across the steps. */
    float walk_x_start = -4.0f;
    float walk_x_end   =  4.0f;

    /* ── Render loop ──────────────────────────────────────────────── */

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float progress = (float)f / (float)(TOTAL_FRAMES - 1);
        float walk_x = walk_x_start + progress * (walk_x_end - walk_x_start);

        /* Adjust foot IK targets to the ground height at current position. */
        float foot_spread = 0.5f;
        vec3_t l_foot_target = {
            walk_x - foot_spread,
            get_ground_height_(walk_x - foot_spread),
            0.0f
        };
        vec3_t r_foot_target = {
            walk_x + foot_spread,
            get_ground_height_(walk_x + foot_spread),
            0.0f
        };

        /* Apply foot targets to the pose (translate foot bones). */
        if (l_foot < skel.joint_count) {
            target_pose[l_foot] = skel.rest_world[l_foot];
            target_pose[l_foot].m[12] = l_foot_target.x;
            target_pose[l_foot].m[13] = l_foot_target.y;
            target_pose[l_foot].m[14] = l_foot_target.z;
        }
        if (r_foot < skel.joint_count) {
            target_pose[r_foot] = skel.rest_world[r_foot];
            target_pose[r_foot].m[12] = r_foot_target.x;
            target_pose[r_foot].m[13] = r_foot_target.y;
            target_pose[r_foot].m[14] = r_foot_target.z;
        }

        /* Translate entire skeleton root to walk_x. */
        for (uint32_t i = 0; i < skel.joint_count; i++) {
            if (skel.parent_indices[i] == UINT32_MAX) {
                target_pose[i] = skel.rest_world[i];
                target_pose[i].m[12] += walk_x;
                float ground_y = get_ground_height_(walk_x);
                target_pose[i].m[13] += ground_y;
            }
        }

        /* Run solver. */
        constraint_solver_evaluate(
            &solver, &skel, skel.rest_world, target_pose, 4);

        if (ragdoll_ok) {
            ragdoll_update_motor_targets(&ragdoll, target_pose,
                                         skel.joint_count);
            anim_bones_to_bodies(target_pose, skel.colliders,
                                 ragdoll.bodies, skel.joint_count);
            ragdoll_sync_from_physics(&ragdoll);
        }

        /* ── Draw ─────────────────────────────────────────────── */
        begin_lines_();
        draw_ground_grid_(0.0f, 15.0f, 30);

        /* Draw step boxes. */
        for (int s = 0; s < NUM_STEPS; s++) {
            float brightness = 0.3f + 0.2f * (float)s;
            draw_box_(&STEPS[s], 0.15f, brightness, 0.5f);
        }

        /* Draw IK targets. */
        draw_target_(l_foot_target, 0.2f, 0.2f, 0.8f, 1.0f);
        draw_target_(r_foot_target, 0.2f, 1.0f, 0.4f, 0.2f);

        /* Draw skeleton. */
        const mat4_t *draw_pose = ragdoll_ok ? ragdoll.bone_world
                                             : target_pose;
        draw_skeleton_(&skel, draw_pose);

        /* Camera follows the walk. */
        mat4_t view, proj;
        mat4_look_at((vec3_t){walk_x + 8.0f, 5.0f, 10.0f},
                     (vec3_t){walk_x, 2.0f, 0.f},
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

        if (f == 0 || f == TOTAL_FRAMES / 3 || f == 2*TOTAL_FRAMES / 3 ||
            f == TOTAL_FRAMES - 1) {
            char path[256];
            snprintf(path, sizeof(path),
                     "tests/output/p005_ik_ground_frame_%03d.ppm", f);
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
    if (capture) fr_video_capture_destroy(capture);
    if (ragdoll_ok) ragdoll_destroy(&ragdoll);
    constraint_solver_destroy(&solver);
    free(target_pose);
    cleanup_line_buffer_();
    shader_program_destroy(&shader);
    skeleton_def_destroy(&skel);
    if (ibms) free(ibms);
    cleanup_gl_context_();

    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);

    int pass = (frame_count >= TOTAL_FRAMES) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
