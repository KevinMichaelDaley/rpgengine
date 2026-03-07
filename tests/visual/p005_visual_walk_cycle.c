/**
 * @file p005_visual_walk_cycle.c
 * @brief Visual test: walk cycle with full parallel physics pipeline.
 *
 * Loads humanoid.fskel + humanoid.glb, creates an animated entity in
 * a real physics world with job system + tick runner.  IK targets
 * (feet + trajectory) move forward each tick to drive a walk cycle.
 * Rigid body boxes are placed on the ground for the character to bump
 * into.  Renders the GPU-skinned mesh plus bone-only debug lines.
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
#include "ferrum/animation/bone_collider.h"
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
#include "ferrum/renderer/skinning_shader.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/renderer/gltf/gltf_loader.h"
#include "ferrum/renderer/video_capture.h"

/* ── Constants ────────────────────────────────────────────────────── */

#define WINDOW_W     640
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 6
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)
#define PI 3.14159265358979323846f
#define DT (1.0f / TARGET_FPS)

#define MAX_SKINNED_MESHES 24

/* Walk cycle parameters. */
#define STRIDE_LENGTH 1.5f
#define STEP_HEIGHT   0.8f
#define WALK_SPEED    0.8f

/* Obstacle placement: boxes appear at z ~ OBSTACLE_Z. */
#define NUM_OBSTACLES  4
#define OBSTACLE_Z_START 12.0f
#define OBSTACLE_SPACING  3.0f

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
    "    vec3 light_dir = normalize(vec3(0.5, 1.0, 0.3));\n"
    "    float ndotl = max(dot(normalize(v_normal), light_dir), 0.2);\n"
    "    vec3 base_color = vec3(0.75, 0.60, 0.50);\n"
    "    frag_color = vec4(base_color * ndotl, 1.0);\n"
    "}\n";

/* ── Line shader sources ─────────────────────────────────────────── */

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
    g_window = SDL_CreateWindow("Walk Cycle + Physics",
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

/**
 * @brief Draw only deform bone connections (skip c_ and MCH- bones).
 */
static void draw_skeleton_(const skeleton_def_t *skel, const mat4_t *pose) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        uint32_t pi = skel->parent_indices[i];
        if (pi == UINT32_MAX || pi >= skel->joint_count) continue;
        const char *name = skel->joint_names[i];
        if (name[0] == 'c' && name[1] == '_') continue;
        if (name[0] == 'M' && name[1] == 'C' && name[2] == 'H') continue;
        vec3_t child  = { pose[i].m[12],  pose[i].m[13],  pose[i].m[14] };
        vec3_t parent = { pose[pi].m[12], pose[pi].m[13], pose[pi].m[14] };
        float r = 0.2f, g = 0.8f, b = 0.3f;
        if (skel->colliders && skel->colliders[i].shape_type != BONE_COLLIDER_NONE) {
            r = 0.9f; g = 0.9f; b = 0.2f;
        }
        add_line_(parent, child, r, g, b);
    }
}

/** Draw a wireframe box at position with given half-extents. */
static void draw_wireframe_box_(vec3_t pos, vec3_t he,
                                 float r, float g, float b) {
    float x = pos.x, y = pos.y, z = pos.z;
    /* Top face. */
    add_line_((vec3_t){x - he.x, y + he.y, z - he.z},
              (vec3_t){x + he.x, y + he.y, z - he.z}, r, g, b);
    add_line_((vec3_t){x + he.x, y + he.y, z - he.z},
              (vec3_t){x + he.x, y + he.y, z + he.z}, r, g, b);
    add_line_((vec3_t){x + he.x, y + he.y, z + he.z},
              (vec3_t){x - he.x, y + he.y, z + he.z}, r, g, b);
    add_line_((vec3_t){x - he.x, y + he.y, z + he.z},
              (vec3_t){x - he.x, y + he.y, z - he.z}, r, g, b);
    /* Bottom face. */
    add_line_((vec3_t){x - he.x, y - he.y, z - he.z},
              (vec3_t){x + he.x, y - he.y, z - he.z}, r*0.5f, g*0.5f, b*0.5f);
    add_line_((vec3_t){x + he.x, y - he.y, z - he.z},
              (vec3_t){x + he.x, y - he.y, z + he.z}, r*0.5f, g*0.5f, b*0.5f);
    add_line_((vec3_t){x + he.x, y - he.y, z + he.z},
              (vec3_t){x - he.x, y - he.y, z + he.z}, r*0.5f, g*0.5f, b*0.5f);
    add_line_((vec3_t){x - he.x, y - he.y, z + he.z},
              (vec3_t){x - he.x, y - he.y, z - he.z}, r*0.5f, g*0.5f, b*0.5f);
    /* Vertical edges. */
    add_line_((vec3_t){x - he.x, y - he.y, z - he.z},
              (vec3_t){x - he.x, y + he.y, z - he.z}, r*0.7f, g*0.7f, b*0.7f);
    add_line_((vec3_t){x + he.x, y - he.y, z - he.z},
              (vec3_t){x + he.x, y + he.y, z - he.z}, r*0.7f, g*0.7f, b*0.7f);
    add_line_((vec3_t){x + he.x, y - he.y, z + he.z},
              (vec3_t){x + he.x, y + he.y, z + he.z}, r*0.7f, g*0.7f, b*0.7f);
    add_line_((vec3_t){x - he.x, y - he.y, z + he.z},
              (vec3_t){x - he.x, y + he.y, z + he.z}, r*0.7f, g*0.7f, b*0.7f);
}

/* ── Find bone by name ────────────────────────────────────────────── */

static int find_bone_(const skeleton_def_t *skel, const char *name) {
    for (uint32_t i = 0; i < skel->joint_count; i++) {
        if (strcmp(skel->joint_names[i], name) == 0) return (int)i;
    }
    return -1;
}

/* ── Walk cycle foot position ─────────────────────────────────────── */

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

/* ── Obstacle box descriptor ──────────────────────────────────────── */

typedef struct obstacle_box {
    uint32_t body_id;
    vec3_t   position;
    vec3_t   half_extents;
} obstacle_box_t;

/* ── Animation substep callback ───────────────────────────────────── */

typedef struct walk_ctx {
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
} walk_ctx_t;

static void walk_substep_cb_(void *user, struct phys_world *world,
                              uint32_t substep, float substep_dt) {
    walk_ctx_t *ctx = (walk_ctx_t *)user;
    skeleton_def_t *skel = ctx->skel;
    uint32_t n = skel->joint_count;
    (void)substep_dt;

    /* Accumulate time only on first substep of each tick. */
    if (substep == 0) {
        ctx->time_acc += DT;
    }
    float t = ctx->time_acc;
    float cycle = t * WALK_SPEED;
    float phase_l = fmodf(cycle, 1.0f);
    float phase_r = fmodf(cycle + 0.5f, 1.0f);
    float forward_z = cycle * STRIDE_LENGTH;

    /* Build local pose from rest + animation offsets. */
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

    /* Evaluate constraint solver → target_pose (world-space). */
    memset(ctx->target_pose, 0, n * sizeof(mat4_t));
    constraint_solver_evaluate(ctx->solver, skel, ctx->local_pose,
                               ctx->target_pose, n);

    /* Drive all bodies toward animation targets (blend deltas). */
    phys_anim_entity_drive_toward(ctx->anim_ent, world,
                                   ctx->target_pose, n, 0.8f);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Walk Cycle + Physics ===\n");

    if (init_gl_context_() != 0) { return 1; }

    /* Line shader. */
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

    /* Skinning shader. */
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

    /* Find control bones. */
    int idx_foot_l = find_bone_(&skel, "c_foot_ik.l");
    int idx_foot_r = find_bone_(&skel, "c_foot_ik.r");
    int idx_traj   = find_bone_(&skel, "c_traj");
    int idx_root_master = find_bone_(&skel, "c_root_master.x");
    printf("  IK bones: foot_l=%d foot_r=%d traj=%d root_master=%d\n",
           idx_foot_l, idx_foot_r, idx_traj, idx_root_master);

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

    /* Constraint solver. */
    constraint_solver_t solver;
    constraint_solver_init(&solver, skel.joint_count,
                           skel.max_constraints_per_joint);
    ik_solver_register(&solver);
    copy_track_register(&solver);
    limit_constraints_register(&solver);
    surface_vol_register(&solver);
    transform_map_register(&solver);

    mat4_t *local_pose  = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
    mat4_t *target_pose = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));

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

    /* Ground plane. */
    uint32_t ground_id = phys_world_create_body(&world);
    {
        phys_body_t *ground = phys_world_get_body(&world, ground_id);
        ground->flags |= PHYS_BODY_FLAG_STATIC;
        phys_world_set_halfspace_collider(&world, ground_id,
            (phys_vec3_t){0.0f, 1.0f, 0.0f}, 0.0f);
    }

    /* Obstacle boxes on the ground for the character to bump into. */
    obstacle_box_t obstacles[NUM_OBSTACLES];
    for (int i = 0; i < NUM_OBSTACLES; i++) {
        float z = OBSTACLE_Z_START + (float)i * OBSTACLE_SPACING;
        /* Stagger left/right of center. */
        float x = (i % 2 == 0) ? -1.5f : 1.5f;
        float half_h = 0.5f + 0.3f * (float)i;
        vec3_t he = {0.6f, half_h, 0.6f};
        vec3_t pos = {x, half_h, z};

        uint32_t bi = phys_world_create_body(&world);
        phys_body_t *body = phys_world_get_body(&world, bi);
        body->position = (phys_vec3_t){pos.x, pos.y, pos.z};
        body->inv_mass = 1.0f / 5.0f; /* 5 kg boxes */
        body->inv_inertia_diag = (phys_vec3_t){3.0f, 3.0f, 3.0f};
        phys_world_set_box_collider(&world, bi,
            (phys_vec3_t){he.x, he.y, he.z},
            (phys_vec3_t){0, 0, 0},
            (phys_quat_t){0, 0, 0, 1});

        obstacles[i].body_id = bi;
        obstacles[i].position = pos;
        obstacles[i].half_extents = he;
        printf("  Obstacle %d: pos=(%.1f, %.1f, %.1f) he=(%.1f, %.1f, %.1f)\n",
               i, (double)pos.x, (double)pos.y, (double)pos.z,
               (double)he.x, (double)he.y, (double)he.z);
    }

    /* Shift skeleton so lowest bone sits on the ground plane (Y=0). */
    float skel_min_y = 1e10f;
    float skel_max_y = -1e10f;
    for (uint32_t i = 0; i < skel.joint_count; i++) {
        float y = skel.rest_world[i].m[13];
        if (y < skel_min_y) skel_min_y = y;
        if (y > skel_max_y) skel_max_y = y;
    }
    float skel_height = skel_max_y - skel_min_y;
    float skel_y_offset = -skel_min_y; /* shift feet to Y=0 */
    mat4_t *initial_pose = (mat4_t *)malloc(skel.joint_count * sizeof(mat4_t));
    for (uint32_t i = 0; i < skel.joint_count; i++) {
        initial_pose[i] = skel.rest_world[i];
        initial_pose[i].m[13] += skel_y_offset;
    }

    /* Create animated entity. */
    phys_anim_entity_t anim_ent;
    if (!phys_anim_entity_create(&anim_ent, &world, &skel, initial_pose)) {
        fprintf(stderr, "phys_anim_entity_create failed\n"); return 1;
    }
    printf("  Animated entity: %u bodies, %u joints\n",
           anim_ent.body_count, anim_ent.joint_count);

    /* Walk animation context. */
    walk_ctx_t walk_ctx;
    memset(&walk_ctx, 0, sizeof(walk_ctx));
    walk_ctx.skel            = &skel;
    walk_ctx.anim_ent        = &anim_ent;
    walk_ctx.solver          = &solver;
    walk_ctx.local_pose      = local_pose;
    walk_ctx.target_pose     = target_pose;
    walk_ctx.idx_foot_l      = idx_foot_l;
    walk_ctx.idx_foot_r      = idx_foot_r;
    walk_ctx.idx_traj        = idx_traj;
    walk_ctx.idx_root_master = idx_root_master;
    walk_ctx.rest_foot_l     = rest_foot_l;
    walk_ctx.rest_foot_r     = rest_foot_r;

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

    /* Set animation substep callback on the world. */
    world.anim_substep_cb   = walk_substep_cb_;
    world.anim_substep_user = &walk_ctx;

    phys_game_state_t game_state;
    phys_game_state_init(&game_state);
    phys_player_state_t player0 = {
        .position = {0.0f, 0.0f, 0.0f},
        .interaction_radius = 50.0f,
    };
    phys_game_state_set_player(&game_state, 0, &player0);
    tick_runner.game_state = &game_state;

    phys_tick_runner_start(&tick_runner);
    printf("  Tick runner started\n");

    /* Video capture. */
    system("mkdir -p tests/output");
    fr_video_capture_desc_t cap_desc = {0};
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/p005_walk_cycle.mp4";
    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture)
        fprintf(stderr, "WARNING: video capture unavailable\n");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.06f, 0.06f, 0.10f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    int gl_error_count = 0;
    int frame_count = 0;
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;

    /* ── Render loop ──────────────────────────────────────────────── */

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Sync bone transforms from physics. */
        phys_anim_entity_sync_from_world(&anim_ent, &world, &skel);

        /* Compute bone palette: palette[j] = bone_world[j] * IBM[j]. */
        uint32_t n = skel.joint_count < ibm_count ? skel.joint_count : ibm_count;
        for (uint32_t j = 0; j < n; j++) {
            mat4_t bw = anim_ent.bone_world[j];
            mat4_t ibm;
            memcpy(&ibm, &ibms[j], sizeof(mat4_t));
            mat4_t result = mat4_mul(bw, ibm);
            memcpy(&bone_matrices[j * 16], result.m, 16 * sizeof(float));
        }

        /* Upload bone palette. */
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bone_ssbo);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        (GLsizeiptr)(n * 16 * sizeof(float)),
                        bone_matrices);

        /* Track actual root body position for camera. */
        float char_x = anim_ent.bone_world[6].m[12];  /* root.x */
        float char_y = anim_ent.bone_world[6].m[13];
        float char_z = anim_ent.bone_world[6].m[14];

        /* Camera: follow from side, slightly behind. */
        mat4_t view, proj;
        float cam_angle = 0.8f;
        float cam_radius = skel_height * 1.5f;
        float cam_x = char_x + cam_radius * cosf(cam_angle);
        float cam_z = char_z + cam_radius * sinf(cam_angle);
        float cam_y = char_y;
        mat4_look_at((vec3_t){cam_x, cam_y, cam_z},
                     (vec3_t){char_x, char_y - skel_height * 0.1f, char_z},
                     (vec3_t){0.f, 1.f, 0.f}, &view);
        mat4_perspective(45.0f * PI / 180.0f,
                         (float)WINDOW_W / (float)WINDOW_H,
                         0.1f, cam_radius * 10.0f, &proj);
        mat4_t vp = mat4_mul(proj, view);

        /* Draw skinned mesh. */
        glUseProgram(skin_shader.program.handle);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bone_ssbo);
        glUniformMatrix4fv(u_vp_loc, 1, 0, vp.m);
        skeletal_mesh_bind(&skinned_mesh);
        for (uint32_t s = 0; s < skinned_mesh.base.submesh_count; s++)
            skeletal_mesh_draw_submesh(&skinned_mesh, s);
        skeletal_mesh_unbind();

        /* Draw bone lines + ground grid + obstacles. */
        begin_lines_();
        draw_ground_grid_(0.0f, skel_height * 3.0f, 60);
        draw_skeleton_(&skel, anim_ent.bone_world);

        /* Draw obstacle boxes at current physics positions. */
        for (int i = 0; i < NUM_OBSTACLES; i++) {
            const phys_body_t *ob = phys_world_get_body(&world,
                                                         obstacles[i].body_id);
            if (!ob) continue;
            vec3_t pos = {ob->position.x, ob->position.y, ob->position.z};
            draw_wireframe_box_(pos, obstacles[i].half_extents,
                                0.8f, 0.3f, 0.2f);
        }

        glDisable(GL_DEPTH_TEST);
        flush_lines_(&vp, &line_shader, u_mvp_loc);
        glEnable(GL_DEPTH_TEST);

        /* GL errors. */
        GLenum gl_err = glGetError();
        while (gl_err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X frame %d\n", gl_err, f);
            ++gl_error_count;
            gl_err = glGetError();
        }

        /* Snapshots at key frames. */
        int snap_frames[] = { 0, 30, 60, 90, 120, 150, 179 };
        for (int si = 0; si < 7; si++) {
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

        uint32_t elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < frame_interval_ms)
            SDL_Delay(frame_interval_ms - elapsed);
    }

    /* Cleanup. */
    phys_tick_runner_stop(&tick_runner);
    if (capture) fr_video_capture_destroy(capture);
    constraint_solver_destroy(&solver);
    free(initial_pose);
    free(local_pose);
    free(target_pose);
    cleanup_line_buffer_();
    shader_program_destroy(&line_shader);
    skinning_shader_destroy(&skin_shader);
    free(bone_matrices);
    glDeleteBuffers(1, &bone_ssbo);
    skeletal_mesh_destroy(&skinned_mesh);
    phys_anim_entity_destroy(&anim_ent);
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
