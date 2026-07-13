/**
 * @file cornell_demo.c
 * @brief Cornell-box radiosity-lightmap demo (see cornell_demo.h).
 *
 * End-to-end: build the classic Cornell box as diffuse quads, bake a global-
 * illumination lightmap with the offline radiosity baker (src/lightmap), and
 * render the box with a shader that samples the baked SH irradiance atlas by the
 * per-vertex lightmap UV. Self-contained SDL2 + OpenGL 3.3, no networking.
 */
#include <glad/glad.h>

#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cornell_demo.h"

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_bake.h"
#include "ferrum/lightmap/lm_scene.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_types.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"

#define CORNELL_PI 3.14159265358979324f
#define CORNELL_MAX_SURF 64
#define CORNELL_WIN 800

/* ── Scene assembly ─────────────────────────────────────────────── */

/* Mutable builder collecting quads into parallel scene arrays. */
typedef struct cornell_builder {
    lm_surface_t  surfaces[CORNELL_MAX_SURF];
    uint16_t      materials[CORNELL_MAX_SURF];
    lm_material_t table[CORNELL_MAX_SURF];
    uint32_t      count;
} cornell_builder_t;

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

/* Append one parallelogram quad. edge_u x edge_v must point along @p normal. */
static void add_quad(cornell_builder_t *b, vec3_t origin, vec3_t edge_u,
                     vec3_t edge_v, vec3_t normal, vec3_t albedo,
                     vec3_t emissive, uint32_t res_u, uint32_t res_v)
{
    uint32_t i = b->count;
    b->surfaces[i] = (lm_surface_t){ origin,   edge_u,   edge_v, normal,
                                     albedo,   emissive, res_u,  res_v };
    b->materials[i] = (uint16_t)i;               /* one material per surface */
    b->table[i] = (lm_material_t){ albedo, emissive };
    b->count = i + 1;
}

/* Append the 5 lit faces (top + 4 sides, bottom on the floor) of a box centred
 * at (cx,cz), footprint 2*hw by 2*hd, height H, yawed @p yaw radians about Y. */
static void add_rotated_box(cornell_builder_t *b, float cx, float cz, float hw,
                            float hd, float H, float yaw, vec3_t albedo,
                            uint32_t res)
{
    float cs = cosf(yaw), sn = sinf(yaw);
    /* Footprint corners CCW (local A,B,C,D) rotated into the world, y=0. */
    float lx[4] = { -hw, hw, hw, -hw };
    float lz[4] = { -hd, -hd, hd, hd };
    vec3_t base[4];
    for (int i = 0; i < 4; ++i)
        base[i] = v3(cx + lx[i] * cs - lz[i] * sn, 0.0f,
                     cz + lx[i] * sn + lz[i] * cs);
    vec3_t up = v3(0, H, 0);
    vec3_t centre = v3(cx, 0, cz);
    vec3_t dark = v3(0, 0, 0);

    /* Top face at y=H: edges A->D then A->B give an upward normal. */
    add_quad(b, vec3_add(base[0], up), vec3_sub(base[3], base[0]),
             vec3_sub(base[1], base[0]), v3(0, 1, 0), albedo, dark, res, res);

    /* Four vertical side faces, outward normals. */
    for (int i = 0; i < 4; ++i) {
        vec3_t p = base[i];
        vec3_t q = base[(i + 1) % 4];
        vec3_t edge = vec3_sub(q, p);
        vec3_t n = vec3_normalize_safe(vec3_cross(edge, up), 1e-6f);
        vec3_t outward = vec3_sub(vec3_scale(vec3_add(p, q), 0.5f), centre);
        if (vec3_dot(n, outward) >= 0.0f)
            add_quad(b, p, edge, up, n, albedo, dark, res, res);
        else /* keep edge_u x edge_v aligned with the flipped normal */
            add_quad(b, p, up, edge, vec3_scale(n, -1.0f), albedo, dark, res, res);
    }
}

/* Build the Cornell box (5 m room, coloured side walls, ceiling light, two
 * boxes) into @p b. */
static void build_cornell(cornell_builder_t *b)
{
    b->count = 0;
    const float S = 5.0f;
    vec3_t white = v3(0.75f, 0.75f, 0.72f);
    vec3_t red = v3(0.63f, 0.06f, 0.05f);
    vec3_t green = v3(0.14f, 0.45f, 0.09f);
    vec3_t dark = v3(0, 0, 0);
    const uint32_t WR = 28; /* wall luxel resolution */
    const uint32_t BR = 14; /* box-face resolution */

    /* floor +y */
    add_quad(b, v3(0, 0, 0), v3(0, 0, S), v3(S, 0, 0), v3(0, 1, 0), white, dark,
             WR, WR);
    /* ceiling -y */
    add_quad(b, v3(0, S, 0), v3(S, 0, 0), v3(0, 0, S), v3(0, -1, 0), white, dark,
             WR, WR);
    /* back wall +z */
    add_quad(b, v3(0, 0, 0), v3(S, 0, 0), v3(0, S, 0), v3(0, 0, 1), white, dark,
             WR, WR);
    /* left wall +x (red) */
    add_quad(b, v3(0, 0, 0), v3(0, S, 0), v3(0, 0, S), v3(1, 0, 0), red, dark,
             WR, WR);
    /* right wall -x (green) */
    add_quad(b, v3(S, 0, 0), v3(0, 0, S), v3(0, S, 0), v3(-1, 0, 0), green, dark,
             WR, WR);
    /* ceiling light panel, faces down (-y), emissive */
    add_quad(b, v3(1.7f, S - 0.02f, 1.7f), v3(1.6f, 0, 0), v3(0, 0, 1.6f),
             v3(0, -1, 0), dark, v3(14.0f, 13.0f, 11.0f), 10, 10);

    /* tall box (back-left) and short box (front-right), each yawed like the
     * classic Cornell box. */
    add_rotated_box(b, 1.5f, 1.9f, 0.8f, 0.8f, 3.0f, 0.32f, white, BR);
    add_rotated_box(b, 3.4f, 3.5f, 0.75f, 0.75f, 1.5f, -0.40f, white, BR);
}

/* ── GL helpers ─────────────────────────────────────────────────── */

static void *cornell_get_proc(const char *name, void *user)
{
    (void)user;
    return SDL_GL_GetProcAddress(name);
}

static const char *CORNELL_VS =
    "#version 330 core\n"
    "in vec3 in_pos;\n"
    "in vec2 in_uv1;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv1;\n"
    "void main() { v_uv1 = in_uv1; gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";

static const char *CORNELL_FS =
    "#version 330 core\n"
    "in vec2 v_uv1;\n"
    "uniform sampler2D u_lightmap;\n"
    "uniform vec3 u_albedo;\n"
    "uniform vec3 u_emissive;\n"
    "uniform float u_exposure;\n"
    "out vec4 frag;\n"
    "const float PI = 3.14159265;\n"
    "void main() {\n"
    "  vec3 E = texture(u_lightmap, v_uv1).rgb;\n"
    "  vec3 c = u_albedo * E * (u_exposure / PI) + u_emissive;\n"
    "  c = c / (c + vec3(1.0));\n"          /* Reinhard tonemap */
    "  c = pow(c, vec3(1.0 / 2.2));\n"      /* gamma */
    "  frag = vec4(c, 1.0);\n"
    "}\n";

/* Upload the baked RGB atlas as a float texture. */
static GLuint upload_lightmap(const float *rgb, uint32_t w, uint32_t h)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, (GLsizei)w, (GLsizei)h, 0, GL_RGB,
                 GL_FLOAT, rgb);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

/* Build interleaved [pos.xyz, uv1.xy] * 6 vertices per surface, with the uv1
 * remapped into each surface's atlas rect. Returns the vertex count. */
static uint32_t build_vertices(const cornell_builder_t *b,
                               const lm_bake_result_t *bake, float *out,
                               uint32_t out_cap)
{
    uint32_t vn = 0;
    for (uint32_t s = 0; s < b->count; ++s) {
        const lm_surface_t *sf = &b->surfaces[s];
        const lm_atlas_rect_t *rect = &bake->rects[s];
        vec3_t c00 = sf->origin;
        vec3_t c10 = vec3_add(sf->origin, sf->edge_u);
        vec3_t c11 = vec3_add(c10, sf->edge_v);
        vec3_t c01 = vec3_add(sf->origin, sf->edge_v);
        vec3_t corner[6] = { c00, c10, c11, c00, c11, c01 };
        float luv[6][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 },
                            { 0, 0 }, { 1, 1 }, { 0, 1 } };
        for (int i = 0; i < 6; ++i) {
            if ((vn + 1) * 5 > out_cap)
                return vn;
            float au, av;
            lm_atlas_remap_uv(rect, &bake->atlas, luv[i][0], luv[i][1], &au, &av);
            float *v = &out[vn * 5];
            v[0] = corner[i].x; v[1] = corner[i].y; v[2] = corner[i].z;
            v[3] = au; v[4] = av;
            ++vn;
        }
    }
    return vn;
}

/* ── Bake ───────────────────────────────────────────────────────── */

/* Run the radiosity bake; returns the atlas image (arena-allocated) or NULL. */
static float *bake_lightmap(const cornell_builder_t *b, lm_bake_result_t *res,
                            arena_t *arena)
{
    lm_scene_t scene;
    scene.surfaces = b->surfaces;
    scene.surface_materials = b->materials;
    scene.n_surfaces = b->count;
    scene.lights = NULL;
    scene.n_lights = 0;
    scene.materials.entries = b->table;
    scene.materials.count = (uint16_t)b->count;
    scene.materials.fallback = (lm_material_t){ v3(0, 0, 0), v3(0, 0, 0) };

    lm_bake_config_t cfg = { 0 };
    cfg.svo_bounds = (phys_aabb_t){ { -0.5f, -0.5f, -0.5f }, { 5.5f, 5.5f, 5.5f } };
    cfg.svo_depth = 6;
    cfg.atlas_width = 512;
    cfg.atlas_padding = 2;
    cfg.direct_samples = 64;
    cfg.farfield_samples = 0; /* closed box: every reflector is a luxel */
    cfg.farfield_near = 0.2f;
    cfg.farfield_maxdist = 12.0f;
    cfg.solve.near_radius = 10.0f; /* whole room: full radiosity coupling */
    cfg.solve.max_shots = 6000;
    cfg.solve.residual_epsilon = 1e-3f;
    cfg.solve.use_region = false;
    cfg.seed = 1337u;

    printf("[cornell] baking %u surfaces...\n", b->count);
    if (!lm_bake(&scene, &cfg, res, arena)) {
        fprintf(stderr, "[cornell] bake failed\n");
        return NULL;
    }
    uint32_t px = res->atlas.width * res->atlas.height;
    float *img = arena_alloc(arena, _Alignof(float), (size_t)px * 3 * sizeof(float));
    if (!img)
        return NULL;
    lm_bake_readback(res, img);
    printf("[cornell] baked %u luxels into %ux%u atlas\n", res->n_luxels,
           res->atlas.width, res->atlas.height);

    /* Debug: atlas magnitude + a representative floor luxel (surface 0). */
    uint32_t n3 = px * 3;
    float mx = 0.0f, sum = 0.0f;
    for (uint32_t i = 0; i < n3; ++i) {
        if (img[i] > mx) mx = img[i];
        sum += img[i];
    }
    float floor_e = lm_sh9_irradiance(&res->combined.luxels[res->n_luxels / 2].sh[0],
                                      res->combined.luxels[res->n_luxels / 2].normal);
    printf("[cornell] atlas max=%.4f mean=%.4f  mid-luxel E=%.4f\n", mx,
           sum / (float)n3, floor_e);
    return img;
}

/* ── Screenshot ─────────────────────────────────────────────────── */

static void save_ppm(const char *path, int w, int h)
{
    size_t row = (size_t)w * 3;
    uint8_t *rgb = malloc(row * (size_t)h);
    if (!rgb)
        return;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y)
            fwrite(rgb + (size_t)y * row, 1, row, f);
        fclose(f);
        printf("[cornell] screenshot: %s\n", path);
    }
    free(rgb);
}

/* ── Driver ─────────────────────────────────────────────────────── */

int cornell_demo_run(const char *screenshot_path, double seconds)
{
    /* 1. Build + bake (before any GL, so a bake failure costs no window). */
    static cornell_builder_t builder;
    build_cornell(&builder);

    size_t arena_bytes = 96u * 1024u * 1024u;
    void *arena_buf = malloc(arena_bytes);
    if (!arena_buf) {
        fprintf(stderr, "[cornell] out of memory\n");
        return 1;
    }
    arena_t arena;
    arena_init(&arena, arena_buf, arena_bytes);
    lm_bake_result_t bake;
    float *atlas_img = bake_lightmap(&builder, &bake, &arena);
    if (!atlas_img) {
        free(arena_buf);
        return 1;
    }

    /* 2. GL context. */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        free(arena_buf);
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow(
        "cornell_demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        CORNELL_WIN, CORNELL_WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        free(arena_buf);
        return 1;
    }
    SDL_GLContext glc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, glc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(glc);
        SDL_DestroyWindow(win);
        SDL_Quit();
        free(arena_buf);
        return 1;
    }

    /* 3. Shader. */
    gl_loader_t loader = { 0 };
    loader.get_proc_address = cornell_get_proc;
    shader_program_t prog;
    char log[1024] = { 0 };
    if (shader_program_create(&prog, &loader, CORNELL_VS, CORNELL_FS, log,
                              sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "[cornell] shader: %s\n", log);
        SDL_GL_DeleteContext(glc);
        SDL_DestroyWindow(win);
        SDL_Quit();
        free(arena_buf);
        return 1;
    }
    GLuint ph = (GLuint)shader_program_handle(&prog);
    GLint u_mvp = glGetUniformLocation(ph, "u_mvp");
    GLint u_albedo = glGetUniformLocation(ph, "u_albedo");
    GLint u_emissive = glGetUniformLocation(ph, "u_emissive");
    GLint u_exposure = glGetUniformLocation(ph, "u_exposure");
    GLint u_lm = glGetUniformLocation(ph, "u_lightmap");
    GLint a_pos = glGetAttribLocation(ph, "in_pos");
    GLint a_uv1 = glGetAttribLocation(ph, "in_uv1");

    /* 4. Geometry + lightmap texture. */
    static float verts[CORNELL_MAX_SURF * 6 * 5];
    uint32_t vcount = build_vertices(&builder, &bake, verts,
                                     sizeof(verts) / sizeof(verts[0]));
    GLuint vbo = 0, vao = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vcount * 5 * sizeof(float)), verts,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)a_pos);
    glVertexAttribPointer((GLuint)a_pos, 3, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray((GLuint)a_uv1);
    glVertexAttribPointer((GLuint)a_uv1, 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(float), (void *)(3 * sizeof(float)));
    GLuint tex = upload_lightmap(atlas_img, bake.atlas.width, bake.atlas.height);

    /* 5. Camera. */
    mat4_t proj, view;
    mat4_perspective(38.0f * (CORNELL_PI / 180.0f), 1.0f, 0.1f, 100.0f, &proj);
    mat4_look_at(v3(2.5f, 2.5f, 12.6f), v3(2.5f, 2.5f, 2.5f), v3(0, 1, 0), &view);
    mat4_t vp = mat4_mul(proj, view);

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, CORNELL_WIN, CORNELL_WIN);

    /* 6. Render loop. */
    Uint32 start = SDL_GetTicks();
    int shot_saved = 0, frame = 0;
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
                running = 0;
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, vp.m);
        glUniform1f(u_exposure, 3.1f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(u_lm, 0);
        glBindVertexArray(vao);
        for (uint32_t s = 0; s < builder.count; ++s) {
            glUniform3fv(u_albedo, 1, &builder.surfaces[s].albedo.x);
            glUniform3fv(u_emissive, 1, &builder.surfaces[s].emissive.x);
            glDrawArrays(GL_TRIANGLES, (GLint)(s * 6), 6);
        }

        /* Save the screenshot on the 2nd frame (window fully realised). */
        ++frame;
        if (screenshot_path && !shot_saved && frame >= 2) {
            save_ppm(screenshot_path, CORNELL_WIN, CORNELL_WIN);
            shot_saved = 1;
        }
        SDL_GL_SwapWindow(win);

        if (seconds <= 0.0 && shot_saved)
            running = 0;
        if (seconds > 0.0 && (SDL_GetTicks() - start) >= (Uint32)(seconds * 1000.0))
            running = 0;
        SDL_Delay(16);
    }

    glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(arena_buf);
    return 0;
}
