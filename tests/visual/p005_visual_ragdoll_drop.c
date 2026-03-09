/**
 * @file p005_visual_ragdoll_drop.c
 * @brief Visual test: ragdoll drop onto ground plane with skeletal mesh.
 *
 * Loads humanoid.fskel + humanoid.glb, registers the skeleton as an
 * animated entity in a real physics world with job system + tick runner,
 * starts it at ground level and lets it tip over under gravity.
 * Renders the GPU-skinned mesh plus bone-only debug lines for 3 seconds.
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
#include "ferrum/animation/bone_collider.h"
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
#include "ferrum/renderer/skinning_shader.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/renderer/gltf/gltf_loader.h"
#include "ferrum/renderer/video_capture.h"

/* ── Constants ────────────────────────────────────────────────────── */

/* Temporary debug: defined in tgs_solve_par.c, controls Jacobian dump. */
extern int g_tgs_debug_dump_counter;

#define WINDOW_W     640
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 12
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f

#define MAX_SKINNED_MESHES 24

#define GROUND_Y     0.0f
#define DT           (1.0f / TARGET_FPS)

/* ── Skinning shader sources ──────────────────────────────────────── */

static const char *SKINNING_VERT_SRC =
    "#version 430 core\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_norm;\n"
    "layout(location = 6) in vec4 in_weights;\n"
    "layout(location = 7) in ivec4 in_indices;\n"
    "layout(std430, binding = 0) buffer BonePalette { mat4 bones[]; };\n"
    "uniform mat4 u_view_proj;\n"
    "out vec3 v_normal;\n"
    "out vec3 v_world_pos;\n"
    "void main() {\n"
    "    mat4 skin = bones[in_indices.x] * in_weights.x +\n"
    "                bones[in_indices.y] * in_weights.y +\n"
    "                bones[in_indices.z] * in_weights.z +\n"
    "                bones[in_indices.w] * in_weights.w;\n"
    "    vec4 world = skin * vec4(in_pos, 1.0);\n"
    "    v_normal = mat3(skin) * in_norm;\n"
    "    v_world_pos = world.xyz;\n"
    "    gl_Position = u_view_proj * world;\n"
    "}\n";

static const char *SKINNING_FRAG_SRC =
    "#version 430 core\n"
    "in vec3 v_normal;\n"
    "in vec3 v_world_pos;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 light_dir = normalize(vec3(0.7, 0.5, 0.7));\n"
    "    float ndotl = max(dot(normalize(v_normal), light_dir), 0.0);\n"
    "    ndotl = ndotl * 0.7 + 0.3;\n"
    "    vec3 base_color = vec3(0.85, 0.70, 0.60);\n"
    "    frag_color = vec4(base_color * ndotl, 1.0);\n"
    "}\n";

/* ── Line shader sources (for bone overlay + grid) ────────────────── */

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
static gl_loader_t   g_loader;

static void *sdl_get_proc_(const char *name, void *ud) {
    (void)ud;
    return SDL_GL_GetProcAddress(name);
}

static int init_gl_context_(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    g_window = SDL_CreateWindow("Ragdoll Drop",
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
    glLineWidth(1.0f);
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

/* ── Draw skeleton as lines (deform bones only) ──────────────────── */

/**
 * @brief Draw only "real" bone connections.
 *
 * Skips control/mechanism bones (names starting with 'c_' or 'MCH-')
 * which are Rigify internal controls, not actual deformation bones.
 * This avoids the long stray lines between distant mechanism bones.
 */
static void draw_skeleton_(const skeleton_def_t *skel, const mat4_t *pose) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t pi = skel->parent_indices[i];
        if (pi == UINT32_MAX || pi >= skel->joint_count) continue;

        /* Skip mechanism/control bones. */
        const char *name = skel->joint_names[i];
        if (name[0] == 'c' && name[1] == '_') continue;
        if (name[0] == 'M' && name[1] == 'C' && name[2] == 'H') continue;

        vec3_t child  = { pose[i].m[12],  pose[i].m[13],  pose[i].m[14] };
        vec3_t parent = { pose[pi].m[12], pose[pi].m[13], pose[pi].m[14] };

        /* Color by bone type: collider bodies are brighter. */
        float r = 0.2f, g = 0.8f, b = 0.3f;
        if (skel->colliders && skel->colliders[i].shape_type != BONE_COLLIDER_NONE) {
            r = 0.9f; g = 0.9f; b = 0.2f;
        }
        add_line_(parent, child, r, g, b);
    }
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Ragdoll Drop ===\n");

    if (init_gl_context_() != 0) { return 1; }

    /* Line shader (for bone overlay + ground grid). */
    shader_program_t line_shader;
    char log_buf[1024];
    int rc = shader_program_create(&line_shader, &g_loader,
                                   LINE_VERT_SRC, LINE_FRAG_SRC,
                                   log_buf, sizeof(log_buf));
    if (rc != 0) {
        fprintf(stderr, "Line shader failed: %s\n", log_buf);
        cleanup_gl_context_(); return 1;
    }
    int32_t u_mvp_loc = line_shader.glGetUniformLocation(
        line_shader.handle, "u_mvp");
    init_line_buffer_();

    /* Skinning shader (for skeletal mesh). */
    skinning_shader_t skin_shader;
    skinning_shader_status_t ss = skinning_shader_create_from_source(
        &skin_shader, &g_loader,
        SKINNING_VERT_SRC, SKINNING_FRAG_SRC,
        log_buf, sizeof(log_buf));
    if (ss != SKINNING_SHADER_OK) {
        fprintf(stderr, "Skinning shader failed (%d): %s\n", ss, log_buf);
        cleanup_line_buffer_(); shader_program_destroy(&line_shader);
        cleanup_gl_context_(); return 1;
    }
    int32_t u_vp_loc = glGetUniformLocation(skin_shader.program.handle,
                                             "u_view_proj");

    /* Load skeleton (fskel). */
    skeleton_def_t skel;
    mat4_t *ibms = NULL;
    uint32_t ibm_count = 0;
    if (!fskel_load("asset_src/humanoid.fskel", &skel, &ibms, &ibm_count)) {
        fprintf(stderr, "Failed to load humanoid.fskel\n");
        cleanup_line_buffer_(); shader_program_destroy(&line_shader);
        skinning_shader_destroy(&skin_shader);
        cleanup_gl_context_(); return 1;
    }
    printf("  Loaded fskel: %u joints, %u IBMs\n", skel.joint_count, ibm_count);

    /* Load skeletal mesh (fvma — same bone ordering as fskel). */
    FILE *fvma_fp = fopen("asset_src/humanoid.fvma", "rb");
    if (!fvma_fp) {
        fprintf(stderr, "Failed to open humanoid.fvma\n");
        skeleton_def_destroy(&skel); if (ibms) free(ibms);
        cleanup_line_buffer_(); shader_program_destroy(&line_shader);
        skinning_shader_destroy(&skin_shader);
        cleanup_gl_context_(); return 1;
    }
    fseek(fvma_fp, 0, SEEK_END);
    long fvma_size = ftell(fvma_fp);
    fseek(fvma_fp, 0, SEEK_SET);
    uint8_t *fvma_data = (uint8_t *)malloc((size_t)fvma_size);
    fread(fvma_data, 1, (size_t)fvma_size, fvma_fp);
    fclose(fvma_fp);

    skeletal_mesh_t skinned_mesh;
    int sm_rc = skeletal_mesh_create_from_fvma(&g_loader, fvma_data,
                                                (size_t)fvma_size,
                                                &skinned_mesh);
    free(fvma_data);
    if (sm_rc != 0) {
        fprintf(stderr, "skeletal_mesh_create_from_fvma failed: %d\n", sm_rc);
        skeleton_def_destroy(&skel); if (ibms) free(ibms);
        cleanup_line_buffer_(); shader_program_destroy(&line_shader);
        skinning_shader_destroy(&skin_shader);
        cleanup_gl_context_(); return 1;
    }
    printf("  Loaded FVMA skeletal mesh: %u submeshes, %u bones\n",
           skinned_mesh.base.submesh_count, skinned_mesh.bone_count);

    /* Bone palette SSBO. */
    uint32_t palette_count = skel.joint_count;
    float *bone_matrices = (float *)calloc(palette_count * 16, sizeof(float));
    GLuint bone_ssbo = 0;
    glGenBuffers(1, &bone_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bone_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 (GLsizeiptr)(palette_count * 16 * sizeof(float)),
                 NULL, GL_DYNAMIC_DRAW);

    /* ── Physics setup ──────────────────────────────────────────── */

    job_system_t phys_job_sys;
    if (job_system_create(&phys_job_sys, 2, 4096, 256u * 1024u, 4096, 0)
        != JOB_CREATE_OK) {
        fprintf(stderr, "job_system_create failed\n"); return 1;
    }
    if (job_system_start(&phys_job_sys) != 0) {
        fprintf(stderr, "job_system_start failed\n"); return 1;
    }

    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 512;
    wcfg.max_colliders = 512;
    wcfg.max_joints = 512;
    phys_world_t world;
    if (phys_world_init(&world, &wcfg) != 0) {
        fprintf(stderr, "phys_world_init failed\n"); return 1;
    }
    /* Bump gravity to compensate for uniform damping drag. */
    world.config.gravity = (phys_vec3_t){0.0f, -12.0f, 0.0f};

    /* Ground plane. */
    uint32_t ground_id = phys_world_create_body(&world);
    {
        phys_body_t *ground = phys_world_get_body(&world, ground_id);
        ground->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        ground->flags |= PHYS_BODY_FLAG_STATIC;
        phys_world_set_halfspace_collider(&world, ground_id,
            (phys_vec3_t){0.0f, 1.0f, 0.0f}, 0.0f);
    }

    /* Position skeleton: shift so lowest bone sits at GROUND_Y + drop height. */
    float min_y = 1e10f;
    float max_y = -1e10f;
    for (uint32_t i = 0; i < skel.joint_count; i++) {
        float y = skel.rest_world[i].m[13];
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    float model_height = max_y - min_y;
    float drop_height = model_height * 0.5f; /* drop from half its height */
    float y_offset = GROUND_Y - min_y + drop_height;

    mat4_t *initial_pose = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
    for (uint32_t i = 0; i < skel.joint_count; i++) {
        initial_pose[i] = skel.rest_world[i];
        initial_pose[i].m[13] += y_offset;
    }

    /* Create animated entity. */
    phys_anim_entity_t anim_ent;
    if (!phys_anim_entity_create(&anim_ent, &world, &skel, initial_pose)) {
        fprintf(stderr, "phys_anim_entity_create failed\n"); return 1;
    }
    printf("  Animated entity: %u bodies, %u joints (pure ragdoll)\n",
           anim_ent.body_count, anim_ent.joint_count);
    {
        uint32_t included_pairs = 0;
        uint32_t excluded_pairs = 0;
        for (uint32_t i = 0; i < skel.joint_count; ++i) {
            uint32_t bi = anim_ent.body_indices[i];
            if (bi == UINT32_MAX) continue;
            const phys_body_t *ba = phys_world_get_body(&world, bi);
            if (!ba || (ba->flags & PHYS_BODY_FLAG_NO_BROADPHASE)) continue;
            for (uint32_t j = i + 1; j < skel.joint_count; ++j) {
                uint32_t bj = anim_ent.body_indices[j];
                if (bj == UINT32_MAX) continue;
                const phys_body_t *bb = phys_world_get_body(&world, bj);
                if (!bb || (bb->flags & PHYS_BODY_FLAG_NO_BROADPHASE)) continue;
                if (phys_world_is_pair_excluded(&world, bi, bj)) {
                    excluded_pairs++;
                } else {
                    included_pairs++;
                }
            }
        }
        fprintf(stderr, "  rig pairs: excluded=%u included=%u\n",
                excluded_pairs, included_pairs);
    }

    /* Physics job context + tick runner. */
    phys_job_context_t phys_jobs;
    phys_job_context_init(&phys_jobs, &phys_job_sys);

    fr_topic_channel_config_t chan_cfg = {
        .capacity = 64, .capacity_bytes = 64 * 1024,
        .max_message_size = 1024,
        .backpressure = FR_TOPIC_BACKPRESSURE_FAIL
    };
    fr_topic_channel_t *cmd_channel = fr_topic_channel_create(&chan_cfg);

    phys_tick_runner_t tick_runner;
    phys_tick_runner_init(&tick_runner, &world, &phys_jobs,
                          cmd_channel, NULL, NULL, NULL);

    phys_game_state_t game_state;
    phys_game_state_init(&game_state);
    /* Position the player at the model center so all bones are in T0. */
    float model_center_y = model_height * 0.5f + GROUND_Y + drop_height;
    phys_player_state_t player0 = {
        .position = {0.0f, model_center_y, 0.0f},
        .interaction_radius = model_height * 2.0f,
    };
    phys_game_state_set_player(&game_state, 0, &player0);
    tick_runner.game_state = &game_state;

    phys_tick_runner_start(&tick_runner);
    printf("  Tick runner started\n");

    /* Enable substep-level diagnostic dump for first few ticks.
     * The flag is checked by tick_parallel.c each substep. */
    world.debug_substep_dump = 0;

    /* Dump full Jacobian rows for first solver iteration (24 joints). */
    g_tgs_debug_dump_counter = 30;

    /* CSV log: per-frame bone X,Z positions for displacement analysis. */
    FILE *csv_log = fopen("tests/output/p005_bone_xz.csv", "w");
    if (csv_log) {
        fprintf(csv_log, "frame");
        for (uint32_t i = 0; i < skel.joint_count; i++) {
            const char *bname = (skel.joint_names) ? skel.joint_names[i] : "?";
            fprintf(csv_log, ",%s_x,%s_z", bname, bname);
        }
        fprintf(csv_log, "\n");
    }

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc = {0};
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_ragdoll_drop.mp4";
    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture)
        fprintf(stderr, "WARNING: video capture unavailable\n");

    glEnable(GL_DEPTH_TEST);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;

    /* ── Render loop ──────────────────────────────────────────────── */

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Turn off substep dump after first 3 render frames
         * (each render frame = ~2 physics ticks at 60Hz/30fps). */
        if (f == 3) {
            world.debug_substep_dump = 0;
        }

        /* Sync bone world transforms from physics bodies. */
        phys_anim_entity_sync_from_world(&anim_ent, &world, &skel);

        /* Write bone X,Z positions to CSV every frame. */
        if (csv_log) {
            fprintf(csv_log, "%d", f);
            for (uint32_t i = 0; i < skel.joint_count; i++) {
                uint32_t idx = anim_ent.body_indices[i];
                if (idx == UINT32_MAX) {
                    fprintf(csv_log, ",0.0,0.0");
                } else {
                    const phys_body_t *b = phys_world_get_body(&world, idx);
                    if (b) {
                        fprintf(csv_log, ",%.5f,%.5f", b->position.x, b->position.z);
                    } else {
                        fprintf(csv_log, ",0.0,0.0");
                    }
                }
            }
            fprintf(csv_log, "\n");
        }

        /* Track foot bones every frame for pop detection. */
        if (f >= 30 && f <= 120) {
            /* Foot.R = bone 15, Foot.L = bone 19 */
            for (int foot = 0; foot < 2; foot++) {
                uint32_t bi = (foot == 0) ? 15 : 19;
                uint32_t idx = anim_ent.body_indices[bi];
                if (idx == UINT32_MAX) continue;
                const phys_body_t *b = phys_world_get_body(&world, idx);
                if (!b) continue;
                float y = b->position.y;
                float vy = b->linear_vel.y;
                float speed = sqrtf(b->linear_vel.x * b->linear_vel.x +
                                    b->linear_vel.y * b->linear_vel.y +
                                    b->linear_vel.z * b->linear_vel.z);
                if (speed > 3.0f || y < -0.1f || (f % 10 == 0)) {
                    fprintf(stderr, "  f%03d %s Y=%.3f vy=%.2f spd=%.2f\n",
                            f, foot == 0 ? "Foot.R" : "Foot.L",
                            y, vy, speed);
                }
            }
        }

        /* Diagnostic: print per-body Y and joint anchor errors at key frames. */
        if (f <= 5 || f == 10 || f == 20 || f == 45 || f == 90 || f == 120
            || f == 150 || f == 179) {
            fprintf(stderr, "  --- f%03d per-body ---\n", f);
            float min_y = 999.0f;
            for (uint32_t bi = 0; bi < skel.joint_count; bi++) {
                uint32_t idx = anim_ent.body_indices[bi];
                if (idx == UINT32_MAX) continue;
                const phys_body_t *b = phys_world_get_body(&world, idx);
                if (!b) continue;
                const char *tag = (b->flags & PHYS_BODY_FLAG_NO_BROADPHASE)
                                ? "G" : "C";
                const char *bname = (bi < skel.joint_count && skel.joint_names)
                                  ? skel.joint_names[bi] : "?";
                float ang_spd = sqrtf(b->angular_vel.x * b->angular_vel.x +
                                      b->angular_vel.y * b->angular_vel.y +
                                      b->angular_vel.z * b->angular_vel.z);
                fprintf(stderr, "    b%02u[%s] %-15s pos=(%.3f,%.3f,%.3f) vel=(%.2f,%.2f,%.2f) w=%.2f\n",
                        bi, tag, bname,
                        b->position.x, b->position.y, b->position.z,
                        b->linear_vel.x, b->linear_vel.y, b->linear_vel.z,
                        ang_spd);
                if (b->position.y < min_y) min_y = b->position.y;
            }
            fprintf(stderr, "    min_Y=%.3f  joints=%u\n",
                    min_y, world.joint_count);
            /* Measure joint anchor errors (world-space distance).
             * Skip distance-limit joints (type PHYS_JOINT_DISTANCE)
             * since they intentionally keep anchors separated. */
            float max_err = 0.0f;
            uint32_t worst_joint = 0;
            for (uint32_t ji = 0; ji < world.joint_count; ji++) {
                const phys_joint_t *j = &world.joints[ji];
                if (j->type == PHYS_JOINT_DISTANCE) continue;
                if (j->body_a >= world.body_pool.capacity ||
                    j->body_b >= world.body_pool.capacity) continue;
                const phys_body_t *ba = &world.body_pool.bodies_curr[j->body_a];
                const phys_body_t *bb = &world.body_pool.bodies_curr[j->body_b];
                phys_vec3_t wa = vec3_add(ba->position,
                    quat_rotate_vec3(ba->orientation, j->local_anchor_a));
                phys_vec3_t wb = vec3_add(bb->position,
                    quat_rotate_vec3(bb->orientation, j->local_anchor_b));
                phys_vec3_t diff = vec3_sub(wa, wb);
                float err = sqrtf(vec3_dot(diff, diff));
                if (err > max_err) { max_err = err; worst_joint = ji; }
            }
            /* Compute max angular limit violation across all joints. */
            float max_ang_viol = 0.0f;
            uint32_t worst_ang_joint = 0;
            for (uint32_t ji = 0; ji < world.joint_count; ji++) {
                const phys_joint_t *jj = &world.joints[ji];
                if (jj->type != PHYS_JOINT_CONE_TWIST) continue;
                if (jj->body_a >= world.body_pool.capacity ||
                    jj->body_b >= world.body_pool.capacity) continue;
                const phys_body_t *ba2 = &world.body_pool.bodies_curr[jj->body_a];
                const phys_body_t *bb2 = &world.body_pool.bodies_curr[jj->body_b];
                phys_quat_t qa = ba2->orientation, qb = bb2->orientation;
                /* q_current = qb * conj(qa), matching engine convention. */
                phys_quat_t qa_inv = {-qa.x, -qa.y, -qa.z, qa.w};
                phys_quat_t q_rel = quat_mul(qb, qa_inv);
                phys_quat_t rest = jj->rest_relative_orient;
                phys_quat_t rest_inv = {-rest.x, -rest.y, -rest.z, rest.w};
                phys_quat_t q_err = quat_mul(rest_inv, q_rel);
                if (q_err.w < 0.0f) {
                    q_err.x = -q_err.x; q_err.y = -q_err.y;
                    q_err.z = -q_err.z; q_err.w = -q_err.w;
                }
                float sv = sqrtf(q_err.x*q_err.x + q_err.y*q_err.y
                               + q_err.z*q_err.z);
                float ax = 0, ay = 0, az = 0;
                if (sv > 1e-8f) {
                    float angle = 2.0f * asinf(sv < 1.0f ? sv : 1.0f);
                    ax = angle * q_err.x / sv;
                    ay = angle * q_err.y / sv;
                    az = angle * q_err.z / sv;
                }
                float v = 0;
                if ((jj->limit_axes & 1) && ax < jj->limit_min[0]) v += jj->limit_min[0] - ax;
                if ((jj->limit_axes & 1) && ax > jj->limit_max[0]) v += ax - jj->limit_max[0];
                if ((jj->limit_axes & 2) && ay < jj->limit_min[1]) v += jj->limit_min[1] - ay;
                if ((jj->limit_axes & 2) && ay > jj->limit_max[1]) v += ay - jj->limit_max[1];
                if ((jj->limit_axes & 4) && az < jj->limit_min[2]) v += jj->limit_min[2] - az;
                if ((jj->limit_axes & 4) && az > jj->limit_max[2]) v += az - jj->limit_max[2];
                if (v > max_ang_viol) { max_ang_viol = v; worst_ang_joint = ji; }
            }
            fprintf(stderr, "    max_anchor_err=%.4f (j%u)  max_ang_viol=%.4f (j%u)\n",
                    max_err, worst_joint, max_ang_viol, worst_ang_joint);
            /* Dump ALL joint errors at key frames. */
            if (f == 0 || f == 5 || f == 10 || f == 45 || f == 60 || f == 90 || f == 120) {
                for (uint32_t ji = 0; ji < world.joint_count; ji++) {
                    const phys_joint_t *jj = &world.joints[ji];
                    if (jj->body_a >= world.body_pool.capacity ||
                        jj->body_b >= world.body_pool.capacity) continue;
                    const phys_body_t *ba2 = &world.body_pool.bodies_curr[jj->body_a];
                    const phys_body_t *bb2 = &world.body_pool.bodies_curr[jj->body_b];
                    phys_vec3_t wa2 = vec3_add(ba2->position,
                        quat_rotate_vec3(ba2->orientation, jj->local_anchor_a));
                    phys_vec3_t wb2 = vec3_add(bb2->position,
                        quat_rotate_vec3(bb2->orientation, jj->local_anchor_b));
                    phys_vec3_t d2 = vec3_sub(wa2, wb2);
                    float e2 = sqrtf(vec3_dot(d2, d2));
                    const char *jtype = jj->type == PHYS_JOINT_CONE_TWIST ? "CT"
                                      : jj->type == PHYS_JOINT_DISTANCE  ? "DL"
                                      : jj->type == PHYS_JOINT_BALL      ? "BL"
                                      : jj->type == 10 /* lock */        ? "LK"
                                      :                                     "??";
                    /* Compute angular error: relative rotation vs rest pose,
                     * decomposed into rotation vector components. */
                    float ang_x = 0.0f, ang_y = 0.0f, ang_z = 0.0f;
                    float viol_x = 0.0f, viol_y = 0.0f, viol_z = 0.0f;
                    if (jj->type == PHYS_JOINT_CONE_TWIST &&
                        jj->body_a < world.body_pool.capacity &&
                        jj->body_b < world.body_pool.capacity) {
                        phys_quat_t qa = ba2->orientation;
                        phys_quat_t qb = bb2->orientation;
                        /* q_current = qb * conj(qa), matching engine convention. */
                        phys_quat_t qa_inv = {-qa.x, -qa.y, -qa.z, qa.w};
                        phys_quat_t q_rel = quat_mul(qb, qa_inv);
                        /* q_err = conj(rest) * q_rel */
                        phys_quat_t rest = jj->rest_relative_orient;
                        phys_quat_t rest_inv = {-rest.x, -rest.y, -rest.z, rest.w};
                        phys_quat_t q_err = quat_mul(rest_inv, q_rel);
                        /* Ensure w > 0 for consistent rotation vector. */
                        if (q_err.w < 0.0f) {
                            q_err.x = -q_err.x; q_err.y = -q_err.y;
                            q_err.z = -q_err.z; q_err.w = -q_err.w;
                        }
                        /* Rotation vector: 2 * asin(|v|) * v/|v| */
                        float sv = sqrtf(q_err.x*q_err.x + q_err.y*q_err.y
                                       + q_err.z*q_err.z);
                        if (sv > 1e-8f) {
                            float angle = 2.0f * asinf(sv < 1.0f ? sv : 1.0f);
                            ang_x = angle * q_err.x / sv;
                            ang_y = angle * q_err.y / sv;
                            ang_z = angle * q_err.z / sv;
                        }
                        /* Compute limit violation per axis. */
                        if (jj->limit_axes & 1) {
                            if (ang_x < jj->limit_min[0]) viol_x = ang_x - jj->limit_min[0];
                            else if (ang_x > jj->limit_max[0]) viol_x = ang_x - jj->limit_max[0];
                        }
                        if (jj->limit_axes & 2) {
                            if (ang_y < jj->limit_min[1]) viol_y = ang_y - jj->limit_min[1];
                            else if (ang_y > jj->limit_max[1]) viol_y = ang_y - jj->limit_max[1];
                        }
                        if (jj->limit_axes & 4) {
                            if (ang_z < jj->limit_min[2]) viol_z = ang_z - jj->limit_min[2];
                            else if (ang_z > jj->limit_max[2]) viol_z = ang_z - jj->limit_max[2];
                        }
                    }
                    float max_viol = fabsf(viol_x);
                    if (fabsf(viol_y) > max_viol) max_viol = fabsf(viol_y);
                    if (fabsf(viol_z) > max_viol) max_viol = fabsf(viol_z);
                    fprintf(stderr, "      j%02u[%s] b%u→b%u pos_err=%.4f"
                            " ang=(%.2f,%.2f,%.2f) viol=(%.3f,%.3f,%.3f)\n",
                            ji, jtype, jj->body_a, jj->body_b, e2,
                            ang_x, ang_y, ang_z,
                            viol_x, viol_y, viol_z);
                }
            }
            /* Dump worst joint anchor details when error is large. */
            if (max_err > 0.1f) {
                const phys_joint_t *wj = &world.joints[worst_joint];
                const phys_body_t *wba = &world.body_pool.bodies_curr[wj->body_a];
                const phys_body_t *wbb = &world.body_pool.bodies_curr[wj->body_b];
                phys_vec3_t wa = vec3_add(wba->position,
                    quat_rotate_vec3(wba->orientation, wj->local_anchor_a));
                phys_vec3_t wb = vec3_add(wbb->position,
                    quat_rotate_vec3(wbb->orientation, wj->local_anchor_b));
                fprintf(stderr, "      joint %u: bodies %u-%u type=%d\n",
                        worst_joint, wj->body_a, wj->body_b, wj->type);
                fprintf(stderr, "      anchor_a local=(%.4f,%.4f,%.4f) world=(%.4f,%.4f,%.4f)\n",
                        wj->local_anchor_a.x, wj->local_anchor_a.y, wj->local_anchor_a.z,
                        wa.x, wa.y, wa.z);
                fprintf(stderr, "      anchor_b local=(%.4f,%.4f,%.4f) world=(%.4f,%.4f,%.4f)\n",
                        wj->local_anchor_b.x, wj->local_anchor_b.y, wj->local_anchor_b.z,
                        wb.x, wb.y, wb.z);
                fprintf(stderr, "      body_a pos=(%.4f,%.4f,%.4f)\n",
                        wba->position.x, wba->position.y, wba->position.z);
                fprintf(stderr, "      body_b pos=(%.4f,%.4f,%.4f)\n",
                        wbb->position.x, wbb->position.y, wbb->position.z);
            }
        }

        /* Debug: print bone 1 (Ribcage) position + velocity every 15 frames. */
        if (f % 15 == 0) {
            uint32_t bi1 = anim_ent.body_indices[1];
            if (bi1 != UINT32_MAX) {
                const phys_body_t *b1 = phys_world_get_body(&world, bi1);
                if (b1)
                    fprintf(stderr, "  f%03d Ribcage: pos=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f) tier=%u flags=0x%x\n",
                            f, b1->position.x, b1->position.y, b1->position.z,
                            b1->linear_vel.x, b1->linear_vel.y, b1->linear_vel.z,
                            b1->tier, b1->flags);
            }
            fprintf(stderr, "  tick_count=%lu\n", (unsigned long)phys_world_tick_count(&world));
        }

        /* Debug: compare body pos vs bone_world pos for key bones. */
        if (f == 45 || f == 90) {
            fprintf(stderr, "  --- f%03d body vs bone_world ---\n", f);
            for (uint32_t j = 0; j < skel.joint_count && j < 6; j++) {
                uint32_t bi = anim_ent.body_indices[j];
                if (bi == UINT32_MAX) continue;
                const phys_body_t *b = phys_world_get_body(&world, bi);
                if (!b) continue;
                mat4_t bw = anim_ent.bone_world[j];
                fprintf(stderr, "    bone[%u] %-12s body=(%.3f,%.3f,%.3f) bw=(%.3f,%.3f,%.3f) delta=(%.4f,%.4f,%.4f)\n",
                        j, skel.joint_names[j],
                        b->position.x, b->position.y, b->position.z,
                        bw.m[12], bw.m[13], bw.m[14],
                        bw.m[12] - b->position.x,
                        bw.m[13] - b->position.y,
                        bw.m[14] - b->position.z);
            }
        }

        /* Compute bone palette: palette[j] = bone_world[j] * IBM[j]. */
        uint32_t n = skel.joint_count < ibm_count ? skel.joint_count : ibm_count;
        for (uint32_t j = 0; j < n; j++) {
            mat4_t bw = anim_ent.bone_world[j];
            mat4_t ibm;
            memcpy(&ibm, &ibms[j], sizeof(mat4_t));
            mat4_t result = mat4_mul(bw, ibm);
            memcpy(&bone_matrices[j * 16], result.m, 16 * sizeof(float));
        }
        /* Debug: on frame 0, dump first few bone palette matrices
         * to verify they are near-identity (rest pose). */
        if (f == 0) {
            fprintf(stderr, "  --- Frame 0 bone palette ---\n");
            for (uint32_t j = 0; j < n && j < 6; j++) {
                const float *m = &bone_matrices[j * 16];
                fprintf(stderr, "  bone[%u]:\n", j);
                fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                        m[0], m[4], m[8], m[12]);
                fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                        m[1], m[5], m[9], m[13]);
                fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                        m[2], m[6], m[10], m[14]);
                fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                        m[3], m[7], m[11], m[15]);
            }
            /* Also dump bone_world[0] and ibm[0] separately. */
            fprintf(stderr, "  bone_world[0]:\n");
            for (int r = 0; r < 4; r++)
                fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                    anim_ent.bone_world[0].m[r],
                    anim_ent.bone_world[0].m[4+r],
                    anim_ent.bone_world[0].m[8+r],
                    anim_ent.bone_world[0].m[12+r]);
            fprintf(stderr, "  ibm[0]:\n");
            for (int r = 0; r < 4; r++)
                fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                    ibms[0].m[r], ibms[0].m[4+r],
                    ibms[0].m[8+r], ibms[0].m[12+r]);
            fprintf(stderr, "  FVMA bones=%u, fskel joints=%u, ibm_count=%u\n",
                    skinned_mesh.bone_count, skel.joint_count, ibm_count);
            /* Compare fskel IBM[0] vs FVMA IBM[0]. */
            if (skinned_mesh.inv_bind_matrices) {
                fprintf(stderr, "  FVMA ibm[0]:\n");
                const float *fi = skinned_mesh.inv_bind_matrices;
                for (int r = 0; r < 4; r++)
                    fprintf(stderr, "    [%7.3f %7.3f %7.3f %7.3f]\n",
                        fi[r], fi[4+r], fi[8+r], fi[12+r]);
            }
        }
        /* Debug: on frame 90, dump palette to check for stretched bones. */
        if (f == 90) {
            fprintf(stderr, "  --- Frame 90 bone palette trans ---\n");
            for (uint32_t j = 0; j < n; j++) {
                const float *m = &bone_matrices[j * 16];
                float det3 = m[0]*(m[5]*m[10]-m[6]*m[9])
                           - m[4]*(m[1]*m[10]-m[2]*m[9])
                           + m[8]*(m[1]*m[6]-m[2]*m[5]);
                fprintf(stderr, "  b%02u t=(%.2f,%.2f,%.2f) det=%.3f\n",
                        j, m[12], m[13], m[14], det3);
            }
        }

        /* Upload bone palette to SSBO. */
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bone_ssbo);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        (GLsizeiptr)(n * 16 * sizeof(float)),
                        bone_matrices);

        /* Camera: perspective 3/4 view looking down at the ragdoll. */
        float aspect = (float)WINDOW_W / (float)WINDOW_H;
        mat4_t view, proj;
        mat4_look_at((vec3_t){-1.5f, model_height * 1.8f, 2.5f},
                     (vec3_t){0.f, model_height * 0.3f, 0.f},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(50.0f * (PI / 180.0f), aspect,
                        0.1f, model_height * 20.0f, &proj);
        mat4_t vp = mat4_mul(proj, view);

        /* Draw skinned mesh. */
        glUseProgram(skin_shader.program.handle);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bone_ssbo);
        glUniformMatrix4fv(u_vp_loc, 1, 0, vp.m);
        skeletal_mesh_bind(&skinned_mesh);
        for (uint32_t s = 0; s < skinned_mesh.base.submesh_count; s++)
            skeletal_mesh_draw_submesh(&skinned_mesh, s);
        skeletal_mesh_unbind();

        /* Draw bone lines + ground grid overlay. */
        begin_lines_();
        draw_ground_grid_(GROUND_Y, model_height * 2.0f, 40);
        draw_skeleton_(&skel, anim_ent.bone_world);
        glDisable(GL_DEPTH_TEST);
        flush_lines_(&vp, &line_shader, u_mvp_loc);
        glEnable(GL_DEPTH_TEST);

        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        /* Snapshots at start, quarter, mid, end. */
        if (f == 0 || f == 5 || f == 45 || f == TOTAL_FRAMES / 2 || f == TOTAL_FRAMES - 1) {
            char path[256];
            snprintf(path, sizeof(path),
                     "tests/output/p005_ragdoll_drop_frame_%03d.ppm", f);
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
        if (elapsed < frame_interval_ms)
            SDL_Delay(frame_interval_ms - elapsed);
    }

    /* Verify the ragdoll dropped. */
    float avg_y = 0.0f;
    uint32_t counted = 0;
    for (uint32_t i = 0; i < anim_ent.bone_count; i++) {
        uint32_t bi = anim_ent.body_indices[i];
        if (bi == UINT32_MAX) continue;
        const phys_body_t *body = phys_world_get_body(&world, bi);
        if (body) { avg_y += body->position.y; counted++; }
    }
    if (counted > 0) avg_y /= (float)counted;
    printf("  Final avg body Y: %.2f (should be near %.1f)\n",
           (double)avg_y, (double)GROUND_Y);

    /* Cleanup. */
    if (csv_log) fclose(csv_log);
    phys_tick_runner_stop(&tick_runner);
    if (capture) fr_video_capture_destroy(capture);
    cleanup_line_buffer_();
    shader_program_destroy(&line_shader);
    skinning_shader_destroy(&skin_shader);
    free(bone_matrices);
    glDeleteBuffers(1, &bone_ssbo);
    skeletal_mesh_destroy(&skinned_mesh);
    phys_anim_entity_destroy(&anim_ent);
    free(initial_pose);
    phys_tick_runner_destroy(&tick_runner);
    phys_job_context_destroy(&phys_jobs);
    if (cmd_channel) fr_topic_channel_destroy(cmd_channel);
    phys_world_destroy(&world);
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
