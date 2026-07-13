/**
 * @file p004_visual_scene.c
 * @brief Visual test of the scene submission interface: build static_mesh
 *        spheres, submit them (mesh + material + transform) with a camera and a
 *        light store into a render_scene, then iterate and draw. Screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/light_store.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 768

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

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

static void translate(float m[16], float x, float y, float z) {
    memset(m, 0, 16 * sizeof(float));
    m[0]=m[5]=m[10]=m[15]=1.0f; m[12]=x; m[13]=y; m[14]=z;
}

int main(int argc, char **argv) {
    const char *shot = argc > 1 ? argv[1] : "/tmp/p004_scene.ppm";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("scene", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    shader_program_t prog; char log[2048] = { 0 };
    if (pbr_shader_create(&prog, &loader, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1; }
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache, &prog);
    GLuint ph = (GLuint)shader_program_handle(&prog);

    /* Meshes. */
    static_mesh_t sphere; static_mesh_create_sphere(&loader, 1.0f, 48, 32, &sphere);

    /* Materials: rough grey, gold metal, smooth red. */
    render_material_t m_grey, m_gold, m_red;
    material_init(&m_grey); m_grey.roughness_min=m_grey.roughness_max=0.6f;
    m_grey.tint[0]=m_grey.tint[1]=m_grey.tint[2]=0.8f;
    material_init(&m_gold); m_gold.metalness=1.0f; m_gold.roughness_min=m_gold.roughness_max=0.25f;
    m_gold.tint[0]=1.0f; m_gold.tint[1]=0.78f; m_gold.tint[2]=0.34f;
    material_init(&m_red); m_red.roughness_min=m_red.roughness_max=0.15f;
    m_red.tint[0]=0.8f; m_red.tint[1]=0.1f; m_red.tint[2]=0.08f;

    /* Scene: three spheres in a row. */
    render_renderable_t backing[8];
    render_scene_t scene; render_scene_init(&scene, backing, 8);
    float mdl[16];
    translate(mdl, -3.0f, 0, 0); render_scene_add(&scene, &sphere, &m_grey, mdl);
    translate(mdl,  0.0f, 0, 0); render_scene_add(&scene, &sphere, &m_gold, mdl);
    translate(mdl,  3.0f, 0, 0); render_scene_add(&scene, &sphere, &m_red, mdl);

    /* Camera. */
    float eye[3]={0,1.5f,10}, tgt[3]={0,0,0}, up[3]={0,1,0};
    render_camera_look_at(&scene.camera, eye, tgt, up, 42.0f*(float)M_PI/180.0f, 1.0f, 0.1f, 100.0f);

    /* Lights. */
    render_light_t lbk[4]; render_light_store_t store; render_light_store_init(&store, lbk, 4);
    render_light_t la; memset(&la,0,sizeof la); la.kind=RENDER_LIGHT_POINT;
    la.position[0]=-5; la.position[1]=5; la.position[2]=6; la.color[0]=14; la.color[1]=13; la.color[2]=11;
    la.intensity=1; la.range=30; la.flags=RENDER_LIGHT_FLAG_REALTIME;
    render_light_t lb=la; lb.position[0]=5; lb.color[0]=4; lb.color[1]=6; lb.color[2]=14;
    render_light_add(&store,&la); render_light_add(&store,&lb);
    scene.lights = &store;

    int32_t lt[32]; float lp[96],ld[96],lc[96],lr[32],lci[32],lco[32];
    uint32_t nl = render_light_store_pack(scene.lights, lt,lp,ld,lc,lr,lci,lco,32);

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    for (int frame=0; frame<3; ++frame) {
        glClearColor(0.04f,0.04f,0.06f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",scene.camera.view,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",scene.camera.proj,0);
        float zero[3]={0,0,0}, amb[3]={0.03f,0.03f,0.04f};
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",scene.camera.eye);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_color",zero);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",scene.camera.eye);
        shader_uniform_set_vec3(&cache,&prog,"u_ambient",amb);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",0);
        glUniform1i(glGetUniformLocation(ph,"u_light_count"), (int)nl);
        glUniform1iv(glGetUniformLocation(ph,"u_light_type"),(int)nl,lt);
        glUniform3fv(glGetUniformLocation(ph,"u_light_pos"),(int)nl,lp);
        glUniform3fv(glGetUniformLocation(ph,"u_light_dir"),(int)nl,ld);
        glUniform3fv(glGetUniformLocation(ph,"u_light_color"),(int)nl,lc);
        glUniform1fv(glGetUniformLocation(ph,"u_light_range"),(int)nl,lr);
        glUniform1fv(glGetUniformLocation(ph,"u_light_cos_inner"),(int)nl,lci);
        glUniform1fv(glGetUniformLocation(ph,"u_light_cos_outer"),(int)nl,lco);
        /* Iterate the submitted scene and draw each renderable. */
        for (uint32_t i=0; i<scene.count; ++i) {
            const render_renderable_t *r = &scene.items[i];
            shader_uniform_set_mat4(&cache,&prog,"u_model",r->model,0);
            material_bind(r->material, 0u, &cache, &prog);
            static_mesh_bind(r->mesh);
            for (uint32_t s=0; s<r->mesh->submesh_count; ++s) static_mesh_draw_submesh(r->mesh, s);
        }
        if (frame==1) save_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    static_mesh_destroy(&sphere);
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
