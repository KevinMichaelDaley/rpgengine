/**
 * @file lm_gather_cmp.c
 * @brief Headless A/B of the CPU vs GPU GI gather on the same Cornell box (with
 *        two angled inner boxes). Bakes the identical triangle-mesh scene twice
 *        through lm_mesh_bake -- once CPU (cfg.gpu_gather=0), once GPU
 *        (cfg.gpu_gather=1) -- and compares the resulting SH per luxel. Any
 *        systematic ratio between the two isolates a divergence to the gather.
 *
 * Usage: lm_gather_cmp
 * Env: CMP_VOXEL (m, def 0.08), CMP_SAMPLES (def 256), CMP_BOUNCES (def 2).
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/gpu/lm_gpu_gather.h"
#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/light_store.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_forward.h"
#include "ferrum/renderer/render_scene.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 700

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }
static vec3_t g_tint[40]; static int g_emis[40];

/* One axis-parallelogram quad -> a 4-vert / 6-index triangle mesh. */
typedef struct quad { float pos[12], nrm[12], uv[8]; uint32_t idx[6]; } quad_t;
static void make_quad(quad_t *q, vec3_t o, vec3_t eu, vec3_t ev, vec3_t n) {
    vec3_t c[4] = { o, vec3_add(o, eu), vec3_add(vec3_add(o, eu), ev), vec3_add(o, ev) };
    float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    for (int i = 0; i < 4; i++) {
        q->pos[i*3] = c[i].x; q->pos[i*3+1] = c[i].y; q->pos[i*3+2] = c[i].z;
        q->nrm[i*3] = n.x; q->nrm[i*3+1] = n.y; q->nrm[i*3+2] = n.z;
        q->uv[i*2] = uv[i][0]; q->uv[i*2+1] = uv[i][1];
    }
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 }; memcpy(q->idx, idx, sizeof idx);
}
static void solid(uint8_t *b, uint8_t r, uint8_t g, uint8_t bl) {
    for (int i = 0; i < 4; i++) { b[i*3] = r; b[i*3+1] = g; b[i*3+2] = bl; }
}

#define MAXQ 40
static quad_t g_q[MAXQ];
static lm_mesh_t g_m[MAXQ];
static int g_n;

static void set_mesh(int i, vec3_t o, vec3_t eu, vec3_t ev, vec3_t n,
                     const lm_image_t *a, const lm_image_t *e, vec3_t at, vec3_t et) {
    make_quad(&g_q[i], o, eu, ev, n);
    lm_mesh_t *m = &g_m[i]; memset(m, 0, sizeof *m);
    m->positions = g_q[i].pos; m->normals = g_q[i].nrm;
    m->uv0 = g_q[i].uv; m->uv1 = g_q[i].uv; m->indices = g_q[i].idx;
    m->vert_count = 4; m->index_count = 6;
    m->albedo_image = a; m->emissive_image = e; m->albedo = at; m->emissive = et;
    m->material = 0; m->lightmap_resolution = 24;
}

/* Five lit faces of a yawed box (top + 4 sides), appended as quad meshes. */
static void add_box(float cx, float cz, float hw, float hd, float H, float yaw,
                    const lm_image_t *a) {
    float cs = cosf(yaw), sn = sinf(yaw);
    float lx[4] = { -hw, hw, hw, -hw }, lz[4] = { -hd, -hd, hd, hd };
    vec3_t base[4];
    for (int i = 0; i < 4; ++i)
        base[i] = v3(cx + lx[i]*cs - lz[i]*sn, 0.0f, cz + lx[i]*sn + lz[i]*cs);
    vec3_t up = v3(0, H, 0), centre = v3(cx, 0, cz), one = v3(1, 1, 1), zero = v3(0, 0, 0);
    set_mesh(g_n++, vec3_add(base[0], up), vec3_sub(base[3], base[0]),
             vec3_sub(base[1], base[0]), v3(0, 1, 0), a, NULL, one, zero);
    for (int i = 0; i < 4; ++i) {
        vec3_t p = base[i], q = base[(i+1)%4];
        vec3_t edge = vec3_sub(q, p);
        vec3_t nn = vec3_normalize_safe(vec3_cross(edge, up), 1e-6f);
        vec3_t outward = vec3_sub(vec3_scale(vec3_add(p, q), 0.5f), centre);
        if (vec3_dot(nn, outward) >= 0.0f)
            set_mesh(g_n++, p, edge, up, nn, a, NULL, one, zero);
        else
            set_mesh(g_n++, p, up, edge, vec3_scale(nn, -1.0f), a, NULL, one, zero);
    }
}

/* Mean of the SH DC coefficient (c[0]) over all luxels/channels. */
static double sh_dc_mean(const lm_mesh_bake_result_t *res) {
    double s = 0.0; uint32_t n = res->n_luxels;
    for (uint32_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) s += res->combined.luxels[i].sh[c].c[0];
    return n ? s / (double)(3u * n) : 0.0;
}

static const float S = 5.0f;
static uint8_t white[12], red[12], green[12], lit[12];
static lm_image_t iw, ir, ig, il;

static void build_scene(void) {
    g_n = 0;
    solid(white, 190, 190, 180); solid(red, 200, 25, 20);
    solid(green, 35, 185, 45);   solid(lit, 255, 255, 255);
    iw = (lm_image_t){ white, 2, 2, 3, true }; ir = (lm_image_t){ red, 2, 2, 3, true };
    ig = (lm_image_t){ green, 2, 2, 3, true };  il = (lm_image_t){ lit, 2, 2, 3, true };
    vec3_t one = v3(1, 1, 1), zero = v3(0, 0, 0), emit = v3(14, 13, 11);
    set_mesh(g_n++, v3(0,0,0), v3(0,0,S), v3(S,0,0), v3(0,1,0), &iw, NULL, one, zero); /* floor */
    set_mesh(g_n++, v3(0,S,0), v3(S,0,0), v3(0,0,S), v3(0,-1,0), &iw, NULL, one, zero); /* ceil */
    set_mesh(g_n++, v3(0,0,0), v3(S,0,0), v3(0,S,0), v3(0,0,1), &iw, NULL, one, zero); /* back */
    set_mesh(g_n++, v3(0,0,0), v3(0,S,0), v3(0,0,S), v3(1,0,0), &ir, NULL, one, zero); /* left red */
    set_mesh(g_n++, v3(S,0,0), v3(0,0,S), v3(0,S,0), v3(-1,0,0), &ig, NULL, one, zero); /* right grn */
    set_mesh(g_n++, v3(1.7f,S-0.02f,1.7f), v3(1.6f,0,0), v3(0,0,1.6f), v3(0,-1,0),
             NULL, &il, zero, emit); /* ceiling light */
    add_box(1.5f, 1.9f, 0.8f, 0.8f, 3.0f, 0.32f, &iw);   /* tall box */
    add_box(3.4f, 3.5f, 0.75f, 0.75f, 1.5f, -0.40f, &iw); /* short box */

    /* Render tints (albedo colour) per mesh, matching the bake albedo. */
    vec3_t wt = v3(0.75f, 0.75f, 0.72f);
    for (int i = 0; i < g_n; ++i) { g_tint[i] = wt; g_emis[i] = 0; }
    g_tint[3] = v3(0.63f, 0.06f, 0.05f); /* left red */
    g_tint[4] = v3(0.14f, 0.45f, 0.09f); /* right green */
    g_tint[5] = v3(0, 0, 0); g_emis[5] = 1; /* ceiling light */
}

/* Render a baked result through the REAL clustered forward+ driver (same path as
 * hall_lit_dynamic): SH lightmap enabled, sun/ambient off so only the baked GI
 * shows. Builds a static mesh per quad with atlas-remapped uv1. */
static void render_result(const lm_mesh_bake_result_t *res, const gl_loader_t *loader,
                          const char *path) {
    /* SH atlas -> 9 RGB32F textures on units 7..15. */
    uint32_t aw = res->atlas.width, ah = res->atlas.height;
    float *img = malloc((size_t)aw*ah*3*sizeof(float)); uint32_t sh_tex[9]; GLuint gid[9];
    glGenTextures(9, gid);
    for (int c = 0; c < 9; c++) { lm_mesh_bake_readback_sh(res, (uint32_t)c, img);
        glActiveTexture(GL_TEXTURE7+c); glBindTexture(GL_TEXTURE_2D, gid[c]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB32F,(GLsizei)aw,(GLsizei)ah,0,GL_RGB,GL_FLOAT,img);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        sh_tex[c] = gid[c]; }
    free(img);

    /* One static mesh per quad, uv1 remapped into the atlas rect. */
    static_mesh_t sm[40]; render_material_t mats[40];
    static float ruv[40][8];
    for (int m = 0; m < g_n; ++m) {
        const lm_mesh_t *me = &g_m[m]; const lm_atlas_rect_t *rc = &res->rects[m];
        for (int v = 0; v < 4; ++v)
            lm_atlas_remap_uv(rc, &res->atlas, me->uv1[v*2], me->uv1[v*2+1],
                              &ruv[m][v*2], &ruv[m][v*2+1]);
        static_mesh_create_info_t ci; memset(&ci, 0, sizeof ci);
        ci.positions = me->positions; ci.normals = me->normals;
        ci.uv0 = me->uv0; ci.uv1 = ruv[m]; ci.indices = me->indices;
        ci.vertex_count = 4; ci.index_count = 6;
        int st = static_mesh_create(loader, &ci, &sm[m]);
        if (st != STATIC_MESH_OK) fprintf(stderr, "mesh %d create failed: %d\n", m, st);
        material_init(&mats[m]);
        mats[m].tint[0]=g_tint[m].x; mats[m].tint[1]=g_tint[m].y; mats[m].tint[2]=g_tint[m].z;
        if (g_emis[m]) { mats[m].emissive_color[0]=1.0f; mats[m].emissive_color[1]=0.95f;
                         mats[m].emissive_color[2]=0.82f; mats[m].emissive_strength=1.2f; }
    }

    render_renderable_t rb[40]; render_scene_t scene; render_scene_init(&scene, rb, 40);
    float model[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int m = 0; m < g_n; ++m) render_scene_add(&scene, &sm[m], &mats[m], model);
    /* Two intensity-0 lights so the driver creates + binds the cube/spot shadow
     * maps (otherwise those samplers are incomplete -> 0x502 on a 4.3 core ctx).
     * Zero intensity => no lighting contribution; only the baked SH shows. */
    render_light_t lb[4]; render_light_store_t lights; render_light_store_init(&lights, lb, 4);
    { render_light_t p; memset(&p,0,sizeof p); p.kind=RENDER_LIGHT_POINT;
      p.position[0]=2.5f;p.position[1]=2.5f;p.position[2]=2.5f; p.intensity=0.0f;
      p.range=20.0f; p.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&p); }
    { render_light_t s; memset(&s,0,sizeof s); s.kind=RENDER_LIGHT_SPOT;
      s.position[0]=2.5f;s.position[1]=4.9f;s.position[2]=2.5f; s.direction[1]=-1.0f;
      s.intensity=0.0f; s.range=20.0f; s.cos_inner=0.9f; s.cos_outer=0.8f;
      s.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&s); }
    render_camera_t cam; float eye[3]={2.5f,2.5f,12.8f}, tgt[3]={2.5f,2.5f,2.5f}, up[3]={0,1,0};
    render_camera_look_at(&cam, eye, tgt, up, 38.0f*(float)M_PI/180.0f, 1.0f, 0.1f, 100.0f);
    scene.camera = cam; scene.lights = &lights;

    render_forward_config_t fc; memset(&fc, 0, sizeof fc);
    fc.loader = loader; fc.cluster = (cluster_config_t){16,16,24,0.1f,100.0f};
    fc.max_lights = 4; fc.index_capacity = 16u*16u*24u*4u;
    fc.screen_w = (float)WIN; fc.screen_h = (float)WIN;
    fc.sh_enabled = 1; fc.sh_scale = 1.0f; for (int c=0;c<9;c++) fc.sh_tex[c]=sh_tex[c];
    fc.shadow_light = 0; fc.shadow_res = 256; fc.shadow_near = 0.1f; fc.shadow_far = 50.0f; fc.shadow_bias = 0.05f;
    fc.spot_light = 1; fc.spot_res = 256; fc.spot_near = 0.1f; fc.spot_far = 50.0f; fc.spot_bias = 0.05f;
    fc.dir_cascades = 1; fc.dir_static_res = 512; fc.dir_dynamic_res = 512;
    fc.dir_lambda = 0.6f; fc.dir_bias = 0.05f; fc.dir_max_distance = 50.0f;
    fc.sun_dir[0]=0.0f; fc.sun_dir[1]=1.0f; fc.sun_dir[2]=0.0f; /* sun_color stays 0 */
    render_forward_t fwd;
    if (!render_forward_init(&fwd, &fc)) { fprintf(stderr, "render_forward_init failed\n"); return; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    { int vp[4]; glGetIntegerv(GL_VIEWPORT, vp); GLint fb=-1; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);
      fprintf(stderr, "before render: fb=%d viewport %d %d %d %d\n", fb, vp[0],vp[1],vp[2],vp[3]); }
    glViewport(0,0,WIN,WIN);
    for (int frame = 0; frame < 2; ++frame) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0,0,WIN,WIN);
        glClearColor(0.1f,0.0f,0.0f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        render_forward_render(&fwd, &scene);
        GLint fb=-1; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);
        GLenum e; while((e=glGetError())!=GL_NO_ERROR) fprintf(stderr,"  GLerr 0x%x (fb now %d)\n",e,fb);
    }
    size_t row=(size_t)WIN*3; uint8_t *rgb=malloc(row*WIN);
    glReadPixels(0,0,WIN,WIN,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    { long s=0; for(size_t i=0;i<row*WIN;++i) s+=rgb[i]; fprintf(stderr,"render %s: pixel sum=%ld\n",path,s); }
    FILE *f=fopen(path,"wb");
    if(f){ fprintf(f,"P6\n%d %d\n255\n",WIN,WIN);
        for(int y=WIN-1;y>=0;--y) fwrite(rgb+(size_t)y*row,1,row,f); fclose(f);
        printf("wrote %s\n",path); }
    free(rgb);
    render_forward_destroy(&fwd);
    for (int m = 0; m < g_n; ++m) static_mesh_destroy(&sm[m]);
    glDeleteTextures(9, gid);
}

static void configure(lm_bake_config_t *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->svo_bounds = (phys_aabb_t){ { -0.5f, -0.5f, -0.5f }, { 5.5f, 5.5f, 5.5f } };
    cfg->voxel_size = getenv("CMP_VOXEL") ? (float)atof(getenv("CMP_VOXEL")) : 0.08f;
    cfg->atlas_width = 2048; cfg->atlas_padding = 2; cfg->direct_samples = 0;
    cfg->farfield_samples = getenv("CMP_SAMPLES") ? (uint32_t)atoi(getenv("CMP_SAMPLES")) : 256u;
    cfg->gi_bounces = getenv("CMP_BOUNCES") ? (uint32_t)atoi(getenv("CMP_BOUNCES")) : 2u;
    cfg->gi_threads = 0u; cfg->gi_batch = 64u;
    /* Closed box: transition beyond the diagonal so nothing goes far-field. */
    cfg->farfield_near = 12.0f; cfg->farfield_maxdist = 1e9f; cfg->seed = 7u;
    cfg->sky.kind = LM_SKY_CONSTANT; cfg->sky.color = v3(0, 0, 0); /* dark: only the panel lights */
}

int main(void) {
    /* GL 4.3 core for the compute path. */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL init\n"); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("cmp", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL);
    SDL_GLContext gc = SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { fprintf(stderr, "glad\n"); return 1; }
    gl_loader_t loader = { sdl_get_proc, NULL };

    build_scene();
    lm_material_t fb = { { 0, 0, 0 }, { 0, 0, 0 } };
    lm_mesh_scene_t scene = { g_m, (uint32_t)g_n, NULL, 0, { NULL, 0, fb } };

    /* SEPARATE arena per bake: both results must stay valid until render time,
     * so the GPU bake must not overwrite the CPU result's backing store. */
    static char abuf_cpu[200 * 1024 * 1024];
    static char abuf_gpu[200 * 1024 * 1024];
    arena_t arena_c, arena_g;

    int gpu_only = getenv("CMP_GPU_ONLY") && atoi(getenv("CMP_GPU_ONLY"));
    double cpu_dc = 0.0; lm_mesh_bake_result_t rc; int have_cpu = 0;
    if (!gpu_only) {
        /* ---- CPU bake ---- */
        arena_init(&arena_c, abuf_cpu, sizeof abuf_cpu);
        lm_bake_config_t cpu; configure(&cpu); cpu.gpu_gather = 0;
        if (!lm_mesh_bake(&scene, &cpu, &rc, &arena_c)) { fprintf(stderr, "cpu bake\n"); return 1; }
        cpu_dc = sh_dc_mean(&rc); have_cpu = 1;
        printf("CPU  luxels=%u  meanSHdc=%.5f\n", rc.n_luxels, cpu_dc);
        fflush(stdout);
    }

    /* ---- GPU bake (own arena, identical config) ---- */
    if (!lm_gpu_gather_init(&loader)) { fprintf(stderr, "gpu init\n"); return 1; }
    arena_init(&arena_g, abuf_gpu, sizeof abuf_gpu);
    lm_bake_config_t gpu; configure(&gpu); gpu.gpu_gather = 1;
    lm_mesh_bake_result_t rg;
    if (!lm_mesh_bake(&scene, &gpu, &rg, &arena_g)) { fprintf(stderr, "gpu bake\n"); return 1; }
    double gpu_dc = sh_dc_mean(&rg);
    printf("GPU  luxels=%u  meanSHdc=%.5f\n", rg.n_luxels, gpu_dc);
    if (have_cpu) printf("ratio GPU/CPU = %.3f\n", cpu_dc != 0.0 ? gpu_dc / cpu_dc : 0.0);

    /* --- Render bakes through the real forward+ driver (default FB) --- */
    if (have_cpu) { render_result(&rc, &loader, "/tmp/cornell_cpu.ppm"); SDL_GL_SwapWindow(win); }
    render_result(&rg, &loader, "/tmp/cornell_gpu.ppm"); SDL_GL_SwapWindow(win);

    lm_gpu_gather_shutdown();
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
