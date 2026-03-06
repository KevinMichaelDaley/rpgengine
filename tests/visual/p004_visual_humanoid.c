/**
 * @file p004_visual_humanoid.c
 * @brief Visual test: load and render humanoid.glb skeletal meshes.
 *
 * Loads asset_src/humanoid.glb via the glTF loader, creates skeletal
 * meshes for all skinned sub-meshes, renders them in bind pose using
 * the skinning shader + bone palette pipeline, with an orbit camera,
 * and records video via fr_video_capture_t.
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
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/skinning_shader.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/renderer/gltf/gltf_loader.h"
#include "ferrum/renderer/video_capture.h"

/* ── Constants ────────────────────────────────────────────────────── */

#define WINDOW_W     640
#define WINDOW_H     480
#define TARGET_FPS   30
#define DURATION_SEC 3
#define TOTAL_FRAMES (TARGET_FPS * DURATION_SEC)

#define MAX_SKINNED_MESHES 24

#define PI 3.14159265358979323846f

/* ── Skinning shader sources ───────────────────────────────────────── */

/**
 * Vertex shader matching skeletal_mesh_t attribute layout:
 *   location 0 = positions (vec3)
 *   location 1 = normals (vec3)
 *   location 6 = bone_weights (vec4)
 *   location 7 = bone_indices (ivec4)
 *
 * Uses SSBO for bone palette (supports 333+ bones).
 * Renders in bind pose: each bone transform is identity.
 */
static const char *SKINNING_VERT_SRC =
    "#version 430 core\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_norm;\n"
    "layout(location = 6) in vec4 in_weights;\n"
    "layout(location = 7) in ivec4 in_indices;\n"
    "layout(std430, binding = 0) buffer BonePalette { mat4 bones[]; };\n"
    "uniform mat4 u_view_proj;\n"
    "uniform vec3 u_color;\n"
    "out vec3 v_normal;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    mat4 skin = bones[in_indices.x] * in_weights.x +\n"
    "                bones[in_indices.y] * in_weights.y +\n"
    "                bones[in_indices.z] * in_weights.z +\n"
    "                bones[in_indices.w] * in_weights.w;\n"
    "    vec4 world = skin * vec4(in_pos, 1.0);\n"
    "    v_normal = mat3(skin) * in_norm;\n"
    "    v_color = u_color;\n"
    "    gl_Position = u_view_proj * world;\n"
    "}\n";

static const char *SKINNING_FRAG_SRC =
    "#version 430 core\n"
    "in vec3 v_normal;\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 light_dir = normalize(vec3(0.5, 1.0, 0.3));\n"
    "    float ndotl = max(dot(normalize(v_normal), light_dir), 0.2);\n"
    "    frag_color = vec4(v_color * ndotl, 1.0);\n"
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);

    g_window = SDL_CreateWindow("Humanoid glTF Visual Test",
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
    size_t row_bytes = (size_t)w * 3;
    uint8_t *rgb = (uint8_t *)malloc(row_bytes * (size_t)h);
    if (!rgb) return -1;

    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);

    FILE *f = fopen(path, "wb");
    if (!f) { free(rgb); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) {
        fwrite(rgb + (size_t)y * row_bytes, 1, row_bytes, f);
    }
    fclose(f);
    free(rgb);
    return 0;
}

/* ── Orbit camera ─────────────────────────────────────────────────── */

static void compute_orbit_camera_(int frame, mat4_t *view, mat4_t *proj) {
    float t = (float)frame / (float)TOTAL_FRAMES;
    float angle = t * 2.0f * PI;
    float radius = 25.0f;
    float cam_y = 5.0f;

    vec3_t eye = {
        radius * sinf(angle),
        cam_y,
        radius * cosf(angle)
    };
    vec3_t target = { 0.0f, 2.0f, 0.0f };
    vec3_t up     = { 0.0f, 1.0f, 0.0f };

    mat4_look_at(eye, target, up, view);

    float aspect = (float)WINDOW_W / (float)WINDOW_H;
    mat4_perspective(45.0f * (PI / 180.0f), aspect, 0.5f, 200.0f, proj);
}

/* ── Per-mesh color palette ───────────────────────────────────────── */

static void mesh_color_(int idx, float out[3]) {
    /* Cycle through distinct colors. */
    static const float palette[][3] = {
        {0.85f, 0.65f, 0.55f}, /* skin tone */
        {0.70f, 0.55f, 0.45f},
        {0.30f, 0.30f, 0.80f}, /* clothing blue */
        {0.25f, 0.25f, 0.70f},
        {0.60f, 0.20f, 0.20f}, /* dark red */
        {0.80f, 0.75f, 0.60f},
        {0.40f, 0.60f, 0.40f}, /* green accent */
        {0.50f, 0.50f, 0.50f}, /* grey */
        {0.90f, 0.80f, 0.65f},
        {0.70f, 0.30f, 0.50f},
        {0.60f, 0.60f, 0.30f},
        {0.35f, 0.55f, 0.75f},
    };
    int n = (int)(sizeof(palette) / sizeof(palette[0]));
    int ci = idx % n;
    out[0] = palette[ci][0];
    out[1] = palette[ci][1];
    out[2] = palette[ci][2];
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Visual Test: Humanoid glTF Skeletal Mesh ===\n");

    /* 1. GL context. */
    if (init_gl_context_() != 0) return 1;

    /* 2. Skinning shader (custom source matching skeletal_mesh layout). */
    skinning_shader_t shader;
    char log_buf[1024];
    skinning_shader_status_t ss = skinning_shader_create_from_source(
        &shader, &g_loader,
        SKINNING_VERT_SRC, SKINNING_FRAG_SRC,
        log_buf, sizeof(log_buf));
    if (ss != SKINNING_SHADER_OK) {
        fprintf(stderr, "Skinning shader compile/link failed (%d): %s\n",
                ss, log_buf);
        cleanup_gl_context_();
        return 1;
    }
    /* Get uniform locations from the underlying program. */
    int32_t u_vp_loc    = glGetUniformLocation(shader.program.handle, "u_view_proj");
    int32_t u_color_loc = glGetUniformLocation(shader.program.handle, "u_color");
    printf("  Skinning shader created (u_view_proj=%d, u_color=%d)\n",
           u_vp_loc, u_color_loc);

    /* 3. Load glTF scene. */
    gltf_scene_t *scene = NULL;
    gltf_status_t gs = gltf_scene_load("asset_src/humanoid.glb", &scene);
    if (gs != GLTF_OK) {
        fprintf(stderr, "gltf_scene_load failed: %d\n", gs);
        skinning_shader_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }
    uint32_t mesh_count = gltf_scene_mesh_count(scene);
    uint32_t joint_count = gltf_scene_joint_count(scene);
    printf("  Loaded humanoid.glb: %u meshes, %u joints\n",
           mesh_count, joint_count);

    /* 4. Create skeletal meshes for all skinned sub-meshes. */
    skeletal_mesh_t skinned_meshes[MAX_SKINNED_MESHES];
    int skinned_count = 0;

    for (uint32_t i = 0; i < mesh_count && skinned_count < MAX_SKINNED_MESHES; i++) {
        gltf_mesh_info_t info;
        if (gltf_scene_mesh_info(scene, i, &info) != GLTF_OK) continue;
        if (!info.is_skinned) continue;

        gs = gltf_scene_create_skeletal_mesh(scene, i, &g_loader,
                                              &skinned_meshes[skinned_count]);
        if (gs == GLTF_OK) {
            printf("  Created skeletal mesh %d: '%s' (%u verts, %u bones)\n",
                   skinned_count, info.name, info.vertex_count,
                   skinned_meshes[skinned_count].bone_count);
            skinned_count++;
        } else {
            fprintf(stderr, "  WARNING: failed to create mesh '%s': %d\n",
                    info.name, gs);
        }
    }
    printf("  Total skinned meshes created: %d\n", skinned_count);

    if (skinned_count == 0) {
        fprintf(stderr, "No skinned meshes created — FAIL\n");
        gltf_scene_destroy(scene);
        skinning_shader_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }

    /* 5. Bone palette: raw SSBO for bind-pose matrices.
     * We create the SSBO directly rather than going through
     * bone_palette_buffer_t to ensure correct binding. */
    GLuint bone_ssbo = 0;

    /* Compute bind-pose bone matrices from the skeleton hierarchy.
     * bone[j] = joint_world_transform[j] × inverse_bind_matrix[j]
     * This correctly positions all mesh parts relative to their
     * shared armature, even when meshes have different node pivots. */
    float *bone_matrices = (float *)malloc((size_t)joint_count * 16 * sizeof(float));
    if (!bone_matrices) {
        fprintf(stderr, "Failed to allocate bone matrices\n");
        for (int i = 0; i < skinned_count; ++i) skeletal_mesh_destroy(&skinned_meshes[i]);
        gltf_scene_destroy(scene);
        skinning_shader_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }
    gs = gltf_scene_compute_bind_pose(scene, bone_matrices, joint_count);
    if (gs != GLTF_OK) {
        fprintf(stderr, "gltf_scene_compute_bind_pose failed: %d\n", gs);
        free(bone_matrices);
        for (int i = 0; i < skinned_count; ++i) skeletal_mesh_destroy(&skinned_meshes[i]);
        gltf_scene_destroy(scene);
        skinning_shader_destroy(&shader);
        cleanup_gl_context_();
        return 1;
    }
    printf("  Bind pose computed for %u joints\n", joint_count);

    /* Upload to SSBO at binding point 0. */
    glGenBuffers(1, &bone_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bone_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 (GLsizeiptr)((size_t)joint_count * 16 * sizeof(float)),
                 bone_matrices, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bone_ssbo);
    printf("  Bone SSBO: handle=%u, %u joints, %zu bytes\n",
           bone_ssbo, joint_count,
           (size_t)joint_count * 16 * sizeof(float));

    /* 6. Video capture. */
    fr_video_capture_desc_t cap_desc;
    cap_desc.width       = WINDOW_W;
    cap_desc.height      = WINDOW_H;
    cap_desc.fps         = TARGET_FPS;
    cap_desc.output_path = "tests/output/humanoid_skeletal.mp4";

    fr_video_capture_t *capture = fr_video_capture_create(&cap_desc);
    if (!capture) {
        fprintf(stderr, "WARNING: video capture unavailable, "
                        "rendering without capture\n");
    }

    /* 7. GL state. */
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.12f, 0.18f, 1.0f);
    glViewport(0, 0, WINDOW_W, WINDOW_H);

    /* 8. Render loop. */
    int gl_error_count = 0;
    int frame_count = 0;
    int recording = (capture != NULL);
    uint32_t frame_interval_ms = 1000 / TARGET_FPS;

    int snapshot_frames[] = { 0, 22, 45, 67, 89 };
    int num_snapshots = (int)(sizeof(snapshot_frames) / sizeof(snapshot_frames[0]));

    for (int f = 0; f < TOTAL_FRAMES; ++f) {
        uint32_t frame_start = SDL_GetTicks();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4_t view, proj;
        compute_orbit_camera_(f, &view, &proj);
        mat4_t vp = mat4_mul(proj, view);

        /* Bind skinning shader + SSBO bone palette. */
        glUseProgram(shader.program.handle);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bone_ssbo);

        /* Set view-projection uniform. */
        glUniformMatrix4fv(u_vp_loc, 1, 0, vp.m);

        /* Draw all skinned meshes at origin (bind pose). */
        for (int i = 0; i < skinned_count; ++i) {
            float color[3];
            mesh_color_(i, color);
            glUniform3fv(u_color_loc, 1, color);

            skeletal_mesh_bind(&skinned_meshes[i]);
            uint32_t sub_count = skinned_meshes[i].base.submesh_count;
            if (sub_count > 0) {
                for (uint32_t s = 0; s < sub_count; ++s) {
                    skeletal_mesh_draw_submesh(&skinned_meshes[i], s);
                }
            } else {
                skeletal_mesh_draw_submesh(&skinned_meshes[i], 0);
            }
            skeletal_mesh_unbind();
        }

        /* Check GL errors. */
        GLenum err = glGetError();
        while (err != GL_NO_ERROR) {
            fprintf(stderr, "GL error 0x%04X on frame %d\n", err, f);
            ++gl_error_count;
            err = glGetError();
        }

        /* PPM snapshots at key frames. */
        for (int si = 0; si < num_snapshots; ++si) {
            if (f == snapshot_frames[si]) {
                char snap_path[256];
                snprintf(snap_path, sizeof(snap_path),
                         "tests/output/humanoid_frame_%03d.ppm", f);
                if (save_ppm_(snap_path, WINDOW_W, WINDOW_H) == 0) {
                    printf("  Snapshot: %s\n", snap_path);
                }
                break;
            }
        }

        /* Capture frame. */
        if (capture) {
            fr_video_capture_submit_frame(capture);
            glFlush();
        }

        SDL_GL_SwapWindow(g_window);
        ++frame_count;

        /* Frame pacing only when recording. */
        if (recording) {
            uint32_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < frame_interval_ms) {
                SDL_Delay(frame_interval_ms - elapsed);
            }
        }
    }

    /* 9. Cleanup. */
    if (capture) {
        fr_video_capture_destroy(capture);
        capture = NULL;
    }

    /* Check output file. */
    {
        FILE *check = fopen("tests/output/humanoid_skeletal.mp4", "rb");
        if (check) {
            fseek(check, 0, SEEK_END);
            long file_size = ftell(check);
            fclose(check);
            printf("  Video output: %ld bytes\n", file_size);
        } else {
            printf("  Video output: not found\n");
        }
    }

    free(bone_matrices);
    glDeleteBuffers(1, &bone_ssbo);
    for (int i = 0; i < skinned_count; ++i) {
        skeletal_mesh_destroy(&skinned_meshes[i]);
    }
    gltf_scene_destroy(scene);
    skinning_shader_destroy(&shader);
    cleanup_gl_context_();

    /* 9. Verdict. */
    printf("\nRendered %d frames, GL errors: %d, skinned meshes: %d\n",
           frame_count, gl_error_count, skinned_count);
    if (frame_count >= TOTAL_FRAMES && gl_error_count == 0) {
        printf("PASS\n");
        return 0;
    } else {
        printf("FAIL (frames=%d/%d, gl_errors=%d)\n",
               frame_count, TOTAL_FRAMES, gl_error_count);
        return 1;
    }
}
