/**
 * @file p006_visual_cube_tower.c
 * @brief Visual test: 13x13x12 tower of stacked cubes on a ground plane.
 *
 * 2028 dynamic box bodies stacked 5m above a halfspace ground collider.
 * Physics runs at 240 Hz with 7 worker threads, ticked as fast as
 * possible (no realtime pacing).  Renders wireframe boxes + ground grid.
 * Reports physics tick throughput vs realtime.
 *
 * PASS if simulation completes without crash and no GL errors.
 */

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/job/system.h"

/* ── Constants ────────────────────────────────────────────────────── */

#define WINDOW_W     800
#define WINDOW_H     600
#define TARGET_FPS   30
#define DURATION_SEC 10
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f

#define TOWER_X       13
#define TOWER_Z       13
#define TOWER_Y       12
#define BOX_COUNT     (TOWER_X * TOWER_Z * TOWER_Y) /* 2028 */
#define BOX_HALF      0.5f  /* half-extent of each cube */
#define BOX_SIZE      (BOX_HALF * 2.0f)
#define GROUND_Y      0.0f
#define STACK_BASE_Y  5.0f  /* bottom of stack above ground */

#define PHYS_HZ       60
#define PHYS_DT       (1.0f / (float)PHYS_HZ)
#define WORKER_COUNT  7

/* Physics ticks per render frame (run all physics, then render). */
#define TICKS_PER_FRAME (PHYS_HZ / TARGET_FPS) /* 60/30 = 2 */

#define MAX_LINE_VERTS (BOX_COUNT * 24 + 2048) /* 12 edges * 2 verts + grid */

/* ── Line shader sources ──────────────────────────────────────────── */

static const char *LINE_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "}\n";

static const char *LINE_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = vec4(v_color, 1.0);\n"
    "}\n";

/* ── GL context ───────────────────────────────────────────────────── */

static SDL_Window   *g_window;
static SDL_GLContext  g_gl_ctx;

static int init_gl_context_(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    g_window = SDL_CreateWindow("Cube Tower",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                WINDOW_W, WINDOW_H,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!g_window) return -1;
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) { SDL_DestroyWindow(g_window); SDL_Quit(); return -1; }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(g_gl_ctx); SDL_DestroyWindow(g_window);
        SDL_Quit(); return -1;
    }
    (void)glGetError();
    return 0;
}

static void cleanup_gl_context_(void) {
    SDL_GL_DeleteContext(g_gl_ctx);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
}

/* ── Shader compilation ───────────────────────────────────────────── */

static GLuint compile_shader_(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile: %s\n", log);
    }
    return s;
}

static GLuint create_program_(const char *vs, const char *fs) {
    GLuint v = compile_shader_(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader_(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "Program link: %s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* ── Dynamic line buffer ──────────────────────────────────────────── */

typedef struct { float pos[3]; float col[3]; } line_vertex_t;
static line_vertex_t *g_line_verts;
static int g_line_vert_count;
static int g_line_vert_cap;
static GLuint g_line_vao, g_line_vbo;

static void init_line_buffer_(int capacity) {
    g_line_vert_cap = capacity;
    g_line_verts = (line_vertex_t *)calloc((size_t)capacity,
                                            sizeof(line_vertex_t));
    glGenVertexArrays(1, &g_line_vao);
    glGenBuffers(1, &g_line_vbo);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)capacity * sizeof(line_vertex_t)),
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

static void add_line_(float ax, float ay, float az,
                       float bx, float by, float bz,
                       float r, float g, float bl) {
    if (g_line_vert_count + 2 > g_line_vert_cap) return;
    line_vertex_t *v = &g_line_verts[g_line_vert_count];
    v[0].pos[0] = ax; v[0].pos[1] = ay; v[0].pos[2] = az;
    v[0].col[0] = r;  v[0].col[1] = g;  v[0].col[2] = bl;
    v[1].pos[0] = bx; v[1].pos[1] = by; v[1].pos[2] = bz;
    v[1].col[0] = r;  v[1].col[1] = g;  v[1].col[2] = bl;
    g_line_vert_count += 2;
}

static void flush_lines_(GLuint program, GLint u_mvp, const float *mvp) {
    glUseProgram(program);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    glBindVertexArray(g_line_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)((size_t)g_line_vert_count *
                                 sizeof(line_vertex_t)),
                    g_line_verts);
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
        add_line_(t, y, -extent, t, y, extent,
                  brightness, brightness * 0.8f, brightness * 0.6f);
        add_line_(-extent, y, t, extent, y, t,
                  brightness, brightness * 0.8f, brightness * 0.6f);
    }
}

/** Draw a wireframe box given center position and orientation quaternion. */
static void draw_box_wireframe_(phys_vec3_t pos, phys_quat_t orient,
                                  float hx, float hy, float hz,
                                  float r, float g, float b) {
    /* 8 corners in local space. */
    phys_vec3_t corners[8] = {
        {-hx, -hy, -hz}, { hx, -hy, -hz},
        { hx,  hy, -hz}, {-hx,  hy, -hz},
        {-hx, -hy,  hz}, { hx, -hy,  hz},
        { hx,  hy,  hz}, {-hx,  hy,  hz},
    };
    /* Transform to world space. */
    float wx[8], wy[8], wz[8];
    for (int i = 0; i < 8; i++) {
        phys_vec3_t rotated = quat_rotate_vec3(orient, corners[i]);
        wx[i] = pos.x + rotated.x;
        wy[i] = pos.y + rotated.y;
        wz[i] = pos.z + rotated.z;
    }
    /* 12 edges: bottom 4, top 4, verticals 4. */
    int edges[][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], e = edges[i][1];
        add_line_(wx[a], wy[a], wz[a], wx[e], wy[e], wz[e], r, g, b);
    }
}

/* ── Wall-clock timer ─────────────────────────────────────────────── */

static uint64_t clock_ns_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Cube Tower (%dx%dx%d = %d boxes) ===\n",
           TOWER_X, TOWER_Z, TOWER_Y, BOX_COUNT);

    if (init_gl_context_() != 0) {
        fprintf(stderr, "Failed to init GL context\n");
        return 1;
    }

    /* Line shader. */
    GLuint line_prog = create_program_(LINE_VERT_SRC, LINE_FRAG_SRC);
    GLint u_mvp_loc = glGetUniformLocation(line_prog, "u_mvp");
    init_line_buffer_(MAX_LINE_VERTS);

    /* ── Job system (7 workers) ───────────────────────────────────── */

    job_system_t job_sys;
    if (job_system_create(&job_sys, WORKER_COUNT, 4096,
                           256u * 1024u, 4096, 0) != JOB_CREATE_OK) {
        fprintf(stderr, "job_system_create failed\n"); return 1;
    }
    if (job_system_start(&job_sys) != 0) {
        fprintf(stderr, "job_system_start failed\n"); return 1;
    }
    printf("  Job system: %d workers\n", WORKER_COUNT);

    /* ── Physics world (240 Hz) ───────────────────────────────────── */

    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = BOX_COUNT + 64;
    wcfg.max_colliders = BOX_COUNT + 64;
    wcfg.manifold_cache_size = 512 * 1024;
    wcfg.frame_arena_size = 128u * 1024u * 1024u; /* 128 MB */
    wcfg.fixed_dt = PHYS_DT;
    wcfg.gravity = (phys_vec3_t){0.0f, -9.81f, 0.0f};

    phys_world_t world;
    if (phys_world_init(&world, &wcfg) != 0) {
        fprintf(stderr, "phys_world_init failed\n"); return 1;
    }
    printf("  World: %d Hz, dt=%.4f\n", PHYS_HZ, PHYS_DT);

    /* Ground halfspace (infinite plane at y=0, normal +Y). */
    uint32_t ground_id = phys_world_create_body(&world);
    {
        phys_body_t *ground = phys_world_get_body(&world, ground_id);
        ground->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        ground->flags |= PHYS_BODY_FLAG_STATIC;
        ground->friction = 0.6f;
        ground->restitution = 0.2f;
        phys_world_set_halfspace_collider(&world, ground_id,
            (phys_vec3_t){0.0f, 1.0f, 0.0f}, 0.0f);
    }

    /* ── Create 13x13x12 tower of boxes ───────────────────────────── */

    uint32_t box_ids[BOX_COUNT];
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};
    phys_vec3_t half_ext = {BOX_HALF, BOX_HALF, BOX_HALF};

    /* Center the tower on the origin in X and Z. */
    float tower_origin_x = -(float)(TOWER_X - 1) * BOX_SIZE * 0.5f;
    float tower_origin_z = -(float)(TOWER_Z - 1) * BOX_SIZE * 0.5f;

    int box_idx = 0;
    for (int iy = 0; iy < TOWER_Y; iy++) {
        for (int iz = 0; iz < TOWER_Z; iz++) {
            for (int ix = 0; ix < TOWER_X; ix++) {
                uint32_t bid = phys_world_create_body(&world);
                box_ids[box_idx++] = bid;

                phys_body_t *b = phys_world_get_body(&world, bid);
                b->position = (phys_vec3_t){
                    tower_origin_x + (float)ix * BOX_SIZE,
                    STACK_BASE_Y + BOX_HALF + (float)iy * BOX_SIZE,
                    tower_origin_z + (float)iz * BOX_SIZE,
                };
                b->orientation = identity;
                b->friction = 0.6f;
                b->restitution = 0.1f;
                phys_body_set_mass(b, 1.0f);
                phys_body_set_box_inertia(b, 1.0f, half_ext);

                b->tier = PHYS_TIER_2_VISIBLE;

                phys_world_set_box_collider(&world, bid,
                    half_ext,
                    (phys_vec3_t){0.0f, 0.0f, 0.0f},
                    identity);
            }
        }
    }
    printf("  Created %d boxes starting at Y=%.1f\n", BOX_COUNT, STACK_BASE_Y);

    /* ── Physics job context ──────────────────────────────────────── */

    phys_job_context_t phys_jobs;
    phys_job_context_init(&phys_jobs, &job_sys);

    /* ── Rendering setup ──────────────────────────────────────────── */

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.95f, 0.95f, 0.95f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;
    uint64_t total_phys_ticks = 0;
    uint64_t total_phys_ns = 0;

    /* Camera parameters: look at the tower from a high angle. */
    float tower_center_y = STACK_BASE_Y + (float)TOWER_Y * BOX_SIZE * 0.5f;
    float cam_dist = (float)TOWER_X * BOX_SIZE * 2.0f;

    printf("  Running %d render frames, %d physics ticks each (%d total ticks)\n",
           TOTAL_FRAMES, TICKS_PER_FRAME, TOTAL_FRAMES * TICKS_PER_FRAME);
    printf("  Simulated time: %.1f seconds\n",
           (float)(TOTAL_FRAMES * TICKS_PER_FRAME) * PHYS_DT);

    /* ── Main loop ────────────────────────────────────────────────── */

    for (int f = 0; f < TOTAL_FRAMES; ++f) {

        /* ── Physics: tick as fast as possible ────────────────────── */
        uint64_t phys_start = clock_ns_();
        for (int t = 0; t < TICKS_PER_FRAME; t++) {
            phys_world_tick_parallel(&world, NULL, &phys_jobs);
            total_phys_ticks++;
        }
        uint64_t phys_end = clock_ns_();
        total_phys_ns += (phys_end - phys_start);

        /* Benchmark logging every 30 frames (1 second of render time). */
        if ((f + 1) % TARGET_FPS == 0) {
            double sim_time = (double)total_phys_ticks * (double)PHYS_DT;
            double wall_time = (double)total_phys_ns / 1e9;
            double realtime_ratio = (wall_time > 0.0) ? sim_time / wall_time : 0.0;
            printf("  [f%03d] %lu ticks in %.3fs wall = %.1fx realtime"
                   " (%.2f ms/tick)\n",
                   f + 1,
                   (unsigned long)total_phys_ticks,
                   wall_time,
                   realtime_ratio,
                   (wall_time * 1000.0) / (double)total_phys_ticks);
        }

        /* ── Render ───────────────────────────────────────────────── */

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Orbit camera slowly around the tower. */
        float angle = (float)f / (float)TOTAL_FRAMES * 2.0f * PI;
        float cam_x = sinf(angle) * cam_dist;
        float cam_z = cosf(angle) * cam_dist;
        float cam_y = tower_center_y + cam_dist * 0.4f;

        float aspect = (float)WINDOW_W / (float)WINDOW_H;
        mat4_t view, proj;
        mat4_look_at((vec3_t){cam_x, cam_y, cam_z},
                     (vec3_t){0.f, tower_center_y * 0.5f, 0.f},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(45.0f * (PI / 180.0f), aspect,
                         0.1f, cam_dist * 4.0f, &proj);
        mat4_t vp = mat4_mul(proj, view);

        begin_lines_();

        /* Draw ground grid. */
        draw_ground_grid_(GROUND_Y, cam_dist * 0.5f, 40);

        /* Draw all boxes as wireframes. */
        for (int i = 0; i < BOX_COUNT; i++) {
            const phys_body_t *b = phys_world_get_body(&world, box_ids[i]);
            if (!b) continue;

            /* Color by height: blue at bottom, red at top. */
            float t = (b->position.y - GROUND_Y) /
                      ((float)TOWER_Y * BOX_SIZE + STACK_BASE_Y);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float r = t;
            float g = 0.2f;
            float bl = 1.0f - t;

            draw_box_wireframe_(b->position, b->orientation,
                                BOX_HALF, BOX_HALF, BOX_HALF, r, g, bl);
        }

        flush_lines_(line_prog, u_mvp_loc, vp.m);

        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        SDL_GL_SwapWindow(g_window);
        ++frame_count;

        /* Poll events so the window stays responsive. */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto done;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                goto done;
        }
    }

done:

    /* ── Final benchmark report ───────────────────────────────────── */
    {
        double sim_time = (double)total_phys_ticks * (double)PHYS_DT;
        double wall_time = (double)total_phys_ns / 1e9;
        double realtime_ratio = (wall_time > 0.0) ? sim_time / wall_time : 0.0;
        double ms_per_tick = (wall_time > 0.0)
            ? (wall_time * 1000.0) / (double)total_phys_ticks : 0.0;

        printf("\n  === BENCHMARK RESULTS ===\n");
        printf("  Bodies:          %d dynamic + 1 static\n", BOX_COUNT);
        printf("  Physics rate:    %d Hz\n", PHYS_HZ);
        printf("  Workers:         %d\n", WORKER_COUNT);
        printf("  Total ticks:     %lu\n", (unsigned long)total_phys_ticks);
        printf("  Simulated time:  %.2f s\n", sim_time);
        printf("  Wall-clock time: %.3f s\n", wall_time);
        printf("  Avg ms/tick:     %.3f ms\n", ms_per_tick);
        printf("  Realtime ratio:  %.2fx\n", realtime_ratio);
        if (realtime_ratio >= 1.0)
            printf("  Status:          %.1fx FASTER than realtime\n",
                   realtime_ratio);
        else
            printf("  Status:          %.1f%% of realtime (SLOWER)\n",
                   realtime_ratio * 100.0);
    }

    /* Spot check: print a few body positions. */
    printf("\n  Sample body positions:\n");
    for (int i = 0; i < BOX_COUNT; i += BOX_COUNT / 5) {
        const phys_body_t *b = phys_world_get_body(&world, box_ids[i]);
        if (b) {
            printf("    box[%4d] pos=(%.2f, %.2f, %.2f)\n",
                   i, b->position.x, b->position.y, b->position.z);
        }
    }

    /* ── Cleanup ──────────────────────────────────────────────────── */

    cleanup_line_buffer_();
    glDeleteProgram(line_prog);
    phys_job_context_destroy(&phys_jobs);
    phys_world_destroy(&world);
    job_system_shutdown(&job_sys);
    cleanup_gl_context_();

    printf("\n  Frames rendered: %d/%d\n", frame_count, TOTAL_FRAMES);
    printf("  GL errors: %d\n", gl_error_count);

    int pass = (frame_count > 0) && (gl_error_count == 0);
    printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
