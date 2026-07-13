/**
 * @file p004_visual_sh_lightmap.c
 * @brief Visual test of the shader SH-lightmap ambient term: bake a small GI
 *        scene, upload its 9 SH-coefficient atlases, and render the scene lit
 *        ONLY by the baked SH lightmap (no direct sun) through the core PBR
 *        shader. Proves directional-SH reconstruction (colour bleed + gradients).
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_bake.h"
#include "ferrum/lightmap/lm_scene.h"
#include "ferrum/lightmap/lm_types.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 700
#define MAXS 8

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

static void save_ppm(const char *path, int w, int h) {
    size_t row = (size_t)w * 3; uint8_t *rgb = malloc(row * (size_t)h);
    if (!rgb) return;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) fwrite(rgb + (size_t)y * row, 1, row, f);
        fclose(f); printf("screenshot: %s\n", path); }
    free(rgb);
}

int main(int argc, char **argv) {
    const char *shot = argc > 1 ? argv[1] : "/tmp/p004_sh_lightmap.ppm";

    /* --- Small GI scene: floor, back, red/green walls, ceiling, light panel. --- */
    lm_surface_t surf[MAXS];
    uint16_t mats[MAXS];
    lm_material_t table[MAXS];
    vec3_t white = v3(0.75f, 0.75f, 0.72f), red = v3(0.63f, 0.06f, 0.05f),
           green = v3(0.14f, 0.45f, 0.09f), dark = v3(0, 0, 0);
    const float S = 5.0f; const uint32_t R = 32;
    int n = 0;
#define QUAD(o, eu, ev, nm, alb, em, ru, rv) \
    do { surf[n] = (lm_surface_t){ o, eu, ev, nm, alb, em, ru, rv }; \
         mats[n] = (uint16_t)n; table[n] = (lm_material_t){ alb, em }; ++n; } while (0)
    QUAD(v3(0,0,0), v3(0,0,S), v3(S,0,0), v3(0,1,0), white, dark, R, R);       /* floor */
    QUAD(v3(0,S,0), v3(S,0,0), v3(0,0,S), v3(0,-1,0), white, dark, R, R);      /* ceiling */
    QUAD(v3(0,0,0), v3(S,0,0), v3(0,S,0), v3(0,0,1), white, dark, R, R);       /* back */
    QUAD(v3(0,0,0), v3(0,S,0), v3(0,0,S), v3(1,0,0), red, dark, R, R);         /* left red */
    QUAD(v3(S,0,0), v3(0,0,S), v3(0,S,0), v3(-1,0,0), green, dark, R, R);      /* right green */
    QUAD(v3(1.7f,S-0.02f,1.7f), v3(1.6f,0,0), v3(0,0,1.6f), v3(0,-1,0), dark, v3(14,13,11), 10, 10); /* light */
#undef QUAD

    lm_scene_t scene = { surf, mats, (uint32_t)n, NULL, 0,
                         { table, (uint16_t)n, { dark, dark } } };
    lm_bake_config_t cfg = { 0 };
    cfg.svo_bounds = (phys_aabb_t){ { -0.5f, -0.5f, -0.5f }, { 5.5f, 5.5f, 5.5f } };
    cfg.svo_depth = 6; cfg.atlas_width = 512; cfg.atlas_padding = 2;
    cfg.direct_samples = 64; cfg.farfield_samples = 0;
    cfg.solve.near_radius = 10.0f; cfg.solve.max_shots = 6000;
    cfg.solve.residual_epsilon = 1e-3f; cfg.seed = 7u;

    size_t abytes = 96u * 1024u * 1024u; void *abuf = malloc(abytes);
    arena_t arena; arena_init(&arena, abuf, abytes);
    lm_bake_result_t bake;
    if (!lm_bake(&scene, &cfg, &bake, &arena)) { fprintf(stderr, "bake failed\n"); return 1; }
    uint32_t aw = bake.atlas.width, ah = bake.atlas.height, apx = aw * ah;
    printf("baked %u luxels into %ux%u atlas\n", bake.n_luxels, aw, ah);

    /* --- GL --- */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("sh_lightmap", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    /* Upload the 9 SH coefficient atlases as float textures on units 8..16. */
    float *img = malloc((size_t)apx * 3 * sizeof(float));
    GLuint sh_tex[9];
    glGenTextures(9, sh_tex);
    for (int c = 0; c < 9; ++c) {
        lm_bake_readback_sh(&bake, (uint32_t)c, img);
        glActiveTexture(GL_TEXTURE7 + c); /* SH on units 7..15, past material 0..6 */
        glBindTexture(GL_TEXTURE_2D, sh_tex[c]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, (GLsizei)aw, (GLsizei)ah, 0, GL_RGB, GL_FLOAT, img);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    free(img);

    shader_program_t prog; char log[2048] = { 0 };
    if (pbr_shader_create(&prog, &loader, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1; }
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache, &prog);

    /* Build render geometry: 6 verts/surface, 14 floats (pos,normal,tangent,uv0,uv1). */
    int nv = n * 6; float *vtx = malloc((size_t)nv * 14 * sizeof(float));
    int vi = 0;
    for (int s = 0; s < n; ++s) {
        const lm_surface_t *sf = &surf[s]; const lm_atlas_rect_t *rc = &bake.rects[s];
        vec3_t c00 = sf->origin, c10 = vec3_add(c00, sf->edge_u),
               c11 = vec3_add(c10, sf->edge_v), c01 = vec3_add(c00, sf->edge_v);
        vec3_t corner[6] = { c00, c10, c11, c00, c11, c01 };
        float luv[6][2] = { {0,0},{1,0},{1,1},{0,0},{1,1},{0,1} };
        vec3_t tan = sf->edge_u; float tl = sqrtf(vec3_dot(tan, tan));
        tan = (tl > 0) ? vec3_scale(tan, 1.0f / tl) : v3(1, 0, 0);
        for (int k = 0; k < 6; ++k) {
            float au = ((float)rc->x + luv[k][0] * (float)rc->w) / (float)aw;
            float av = ((float)rc->y + luv[k][1] * (float)rc->h) / (float)ah;
            float *p = &vtx[vi * 14];
            p[0]=corner[k].x; p[1]=corner[k].y; p[2]=corner[k].z;
            p[3]=sf->normal.x; p[4]=sf->normal.y; p[5]=sf->normal.z;
            p[6]=tan.x; p[7]=tan.y; p[8]=tan.z; p[9]=1.0f;
            p[10]=au; p[11]=av; p[12]=au; p[13]=av;
            ++vi;
        }
    }
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)nv * 14 * sizeof(float)), vtx, GL_STATIC_DRAW);
    int comps[5]={3,3,4,2,2}; size_t off[5]={0,3,6,10,12};
    for (int a=0;a<5;a++){ glEnableVertexAttribArray((GLuint)a);
        glVertexAttribPointer((GLuint)a, comps[a], GL_FLOAT, GL_FALSE, 14*sizeof(float), (void*)(off[a]*sizeof(float))); }
    free(vtx);

    mat4_t proj, view;
    mat4_perspective(38.0f*(float)M_PI/180.0f, 1.0f, 0.1f, 100.0f, &proj);
    mat4_look_at(v3(2.5f,2.5f,12.6f), v3(2.5f,2.5f,2.5f), v3(0,1,0), &view);
    mat4_t model = mat4_identity();

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    for (int frame=0; frame<3; ++frame) {
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",view.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",proj.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_model",model.m,0);
        float eye[3]={2.5f,2.5f,12.6f}, zero[3]={0,0,0};
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",eye);
        {float amb[3]={0.35f,0.35f,0.35f}; shader_uniform_set_vec3(&cache,&prog,"u_ambient",amb);} shader_uniform_set_vec3(&cache,&prog,"u_sun_color",zero);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",eye);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",1);
        static const char *sh_names[9] = { "u_sh0","u_sh1","u_sh2","u_sh3","u_sh4",
                                           "u_sh5","u_sh6","u_sh7","u_sh8" };
        for (int c=0;c<9;c++) shader_uniform_set_int(&cache,&prog,sh_names[c],7+c);
        glBindVertexArray(vao);
        for (int s=0;s<n;s++){
            render_material_t m; material_init(&m);
            m.tint[0]=surf[s].albedo.x; m.tint[1]=surf[s].albedo.y; m.tint[2]=surf[s].albedo.z;
            if (s==5){ /* light panel: self-shading emissive so it glows */
                m.emissive_color[0]=1.0f; m.emissive_color[1]=0.95f; m.emissive_color[2]=0.82f;
                m.emissive_strength=1.2f;
            }
            material_bind(&m, 0u, &cache, &prog);
            glDrawArrays(GL_TRIANGLES, s*6, 6);
        }
        if (frame==1) save_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    free(abuf);
    return 0;
}
