/**
 * @file hall_lit_dynamic.c
 * @brief The romanesque hall rendered through the clustered forward+ driver with
 *        BOTH the baked SH lightmap (static GI) AND many dynamic clustered point
 *        lights -- "lightmapped and dynamic" in one pass. Loads the dual-UV
 *        dmeshes + a serialized .flm lightmap (FLM arg or /tmp/hall_prod.flm),
 *        remaps each mesh's uv1 into the atlas, and submits a render_scene to
 *        render_forward with sh_enabled + point lights. PPM screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <dirent.h>
#include <math.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/memory/arena.h"
#include "ferrum/mesh/dmesh_loader.h"
#include "ferrum/mesh/obj_loader.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/light_store.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_forward.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/resource/gpu_cmd_queue.h"
#include "ferrum/renderer/resource/gpu_executor.h"
#include "ferrum/renderer/resource/gpu_registry.h"
#include "ferrum/renderer/texture.h"

/* --- Resource-paradigm creation: fibers decode + package, the render-thread
 * executor calls the renderer's own texture_create / static_mesh_create. --- */
typedef struct tex_load {
    gpu_cmd_queue_t *queue; arena_t *arena;
    const char *path; texture_format_t fmt; texture_t *out;
    int w, h; unsigned char *px; /* filled by the fiber. */
} tex_load_t;

#define GL_TEX_MAX_ANISO 0x84FE
#define GL_MAX_TEX_MAX_ANISO 0x84FF
/* Render-thread finaliser: create + upload the texture_t and set filtering. */
static void finalize_texture(void *ctx, void *user) {
    tex_load_t *t = (tex_load_t *)ctx; const gl_loader_t *l = (const gl_loader_t *)user;
    texture_create(t->out, l);
    texture_upload_2d(t->out, t->fmt, (uint32_t)t->w, (uint32_t)t->h, t->px, true);
    texture_set_sampler(t->out, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT);
    GLfloat maxa = 1.0f; glGetFloatv(GL_MAX_TEX_MAX_ANISO, &maxa); if (maxa > 16.0f) maxa = 16.0f;
    glBindTexture(GL_TEXTURE_2D, texture_handle(t->out));
    glTexParameterf(GL_TEXTURE_2D, GL_TEX_MAX_ANISO, maxa);
}
/* Fiber: decode the PNG into the arena, then enqueue the finaliser. A missing or
 * corrupt asset must NEVER stall or silently drop the texture (that leaves the
 * material bound to an uncreated handle) -- warn and substitute a bright
 * magenta/black debug checkerboard so the surface is obviously unshaded and the
 * load proceeds. */
static void tex_load_fiber(void *ud) {
    tex_load_t *t = (tex_load_t *)ud;
    int w = 0, h = 0, n = 0; unsigned char *src = stbi_load(t->path, &w, &h, &n, 3);
    unsigned char *copy;
    if (!src) {
        fprintf(stderr, "WARN: texture load failed '%s' -- using debug checkerboard\n", t->path);
        w = h = 64; size_t sz = (size_t)w * (size_t)h * 3u;
        copy = arena_alloc(t->arena, 16u, sz);
        if (copy) for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            int on = ((x >> 3) ^ (y >> 3)) & 1;
            unsigned char *p = copy + ((size_t)y * w + x) * 3;
            p[0] = on ? 255 : 0; p[1] = 0; p[2] = on ? 255 : 0;
        }
    } else {
        size_t sz = (size_t)w * (size_t)h * 3u;
        copy = arena_alloc(t->arena, 16u, sz);
        if (copy) memcpy(copy, src, sz);
        stbi_image_free(src);
    }
    if (copy) { t->w = w; t->h = h; t->px = copy;
        gpu_cmd_t c; memset(&c, 0, sizeof c); c.type = GPU_CMD_CUSTOM;
        c.execute = finalize_texture; c.ctx = t;
        while (!gpu_cmd_push(t->queue, &c)) sched_yield(); }
}
typedef struct mesh_load { static_mesh_create_info_t info; static_mesh_t *out; } mesh_load_t;
static void finalize_mesh(void *ctx, void *user) {
    mesh_load_t *m = (mesh_load_t *)ctx;
    static_mesh_create((const gl_loader_t *)user, &m->info, m->out);
}
/* One baked SH9 lightmap coefficient atlas (RGB32F, clamped) as an asset. */
typedef struct sh_load { int unit; uint32_t w, h; const float *px; GLuint *out; } sh_load_t;
static void finalize_sh(void *ctx, void *user) {
    sh_load_t *s = (sh_load_t *)ctx; (void)user;
    glGenTextures(1, s->out);
    glActiveTexture(GL_TEXTURE0 + (GLenum)s->unit);
    glBindTexture(GL_TEXTURE_2D, *s->out);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, (GLsizei)s->w, (GLsizei)s->h, 0, GL_RGB, GL_FLOAT, s->px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* Build the 9 SH-coeff atlases as GL_TEXTURE_2D_ARRAY (one layer per chunk page,
 * rpg-yfa4). perchunk=0: a single 1-layer atlas from <lmfile>. perchunk=1: read
 * <lmfile>_manifest.bin (per-mesh layer+rect) + <lmfile>_c*.flm (one per layer,
 * ascending chunk order). Fills sh_tex[9], per-mesh mrect[nm]+mlayer[nm], array
 * dims. Meshes are in the renderer's sorted-dmesh order = the bake's. */
/* ── Streaming lightmap residency (rpg-ojuq) ───────────────────────────────
 * All per-chunk SH coeff images live in host RAM; only the chunks whose geometry
 * is on-screen (found by a lightmap-index prepass) are paged into a bounded
 * GL_TEXTURE_2D_ARRAY. A page table maps chunk id -> resident layer (-1 = out). */
#define SH_MAX_RESIDENT 12
typedef struct { int w, h; float *coeff[9]; } sh_chunk_ram_t;
typedef struct {
    int            n_chunks;
    uint32_t       aw, ah;                 /* uniform array layer size (max chunk) */
    sh_chunk_ram_t *ram;                   /* [n_chunks] host-cached coeffs */
    GLuint         tex[9];                 /* 9 arrays, SH_MAX_RESIDENT layers */
    int           *page;                   /* [n_chunks] chunk -> layer (-1) */
    int            slot_chunk[SH_MAX_RESIDENT]; /* layer -> chunk (-1 free) */
    int            slot_used[SH_MAX_RESIDENT];  /* last frame each slot was needed */
    int            frame;
    int            resident;               /* live layer count */
    /* Lightmap-index prepass: render chunk-id to an R32UI target, read it back. */
    GLuint         pp_prog, pp_fbo, pp_col, pp_depth;
    int            pp_w, pp_h;
    uint32_t      *pp_read;                /* pp_w*pp_h readback buffer */
    uint8_t       *vis;                    /* [n_chunks] visible-this-frame flags */
} sh_stream_t;

/* Upload chunk c's 9 coeff images from RAM into array layer `slot`. */
static void sh_stream_upload(sh_stream_t *s, int c, int slot)
{
    const sh_chunk_ram_t *r = &s->ram[c];
    for (int k = 0; k < 9; ++k) {
        glActiveTexture(GL_TEXTURE0 + 7u + (GLenum)k);
        glBindTexture(GL_TEXTURE_2D_ARRAY, s->tex[k]);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot,
                        (GLsizei)r->w, (GLsizei)r->h, 1, GL_RGB, GL_FLOAT, r->coeff[k]);
    }
    s->page[c] = slot; s->slot_chunk[slot] = c;
}

/* Ensure chunk c is resident; page it in (evicting the least-recently-needed
 * slot if full). Returns its layer, or -1 if it can't be cached. */
static int sh_stream_touch(sh_stream_t *s, int c)
{
    if (c < 0 || c >= s->n_chunks) return -1;
    if (s->page[c] >= 0) { s->slot_used[s->page[c]] = s->frame; return s->page[c]; }
    int slot = -1;
    for (int i = 0; i < SH_MAX_RESIDENT; ++i) if (s->slot_chunk[i] < 0) { slot = i; break; }
    if (slot < 0) { /* evict the slot needed longest ago */
        int oldest = s->frame + 1;
        for (int i = 0; i < SH_MAX_RESIDENT; ++i)
            if (s->slot_used[i] < oldest) { oldest = s->slot_used[i]; slot = i; }
        if (s->slot_chunk[slot] >= 0) s->page[s->slot_chunk[slot]] = -1;
    }
    sh_stream_upload(s, c, slot);
    s->slot_used[slot] = s->frame;
    return slot;
}

static int load_sh_arrays(const char *lmfile, int perchunk, int nm,
                          GLuint sh_tex[9], lm_atlas_rect_t *mrect, int *mlayer,
                          uint32_t *out_w, uint32_t *out_h)
{
    uint32_t aw = 0, ah = 0, nlayers = 1;
    for (int i = 0; i < nm; ++i) { mlayer[i] = 0; mrect[i] = (lm_atlas_rect_t){0,0,0,0}; }

    if (perchunk) {
        char mp[600]; snprintf(mp, sizeof mp, "%s_manifest.bin", lmfile);
        FILE *mf = fopen(mp, "rb"); if (!mf) { fprintf(stderr, "no manifest %s\n", mp); return -1; }
        char mg[4]; uint32_t hdr[4];
        if (fread(mg,1,4,mf)!=4 || memcmp(mg,"ZLM1",4) || fread(hdr,sizeof hdr,1,mf)!=1) { fclose(mf); return -1; }
        uint32_t nm_m = hdr[0]; nlayers = hdr[1]; aw = hdr[2]; ah = hdr[3];
        for (uint32_t i = 0; i < nm_m; ++i) {
            int32_t L; uint32_t r[4];
            if (fread(&L,4,1,mf)!=1 || fread(r,4,4,mf)!=4) break;
            if ((int)i < nm) { mlayer[i] = L < 0 ? 0 : (int)L;
                mrect[i] = (lm_atlas_rect_t){ r[2], r[3], r[0], r[1] }; } /* w,h,x,y */
        }
        fclose(mf);
    } else {
        FILE *lf = fopen(lmfile, "rb"); if (!lf) { fprintf(stderr, "open %s failed\n", lmfile); return -1; }
        char mg[4]; uint32_t nc, nmh;
        if (fread(mg,1,4,lf)!=4 || memcmp(mg,"FLM1",4) || fread(&aw,4,1,lf)!=1 ||
            fread(&ah,4,1,lf)!=1 || fread(&nc,4,1,lf)!=1 || fread(&nmh,4,1,lf)!=1) { fclose(lf); return -1; }
        fclose(lf);
    }
    *out_w = aw; *out_h = ah;

    for (int c = 0; c < 9; ++c) {
        glGenTextures(1, &sh_tex[c]);
        glActiveTexture(GL_TEXTURE0 + 7u + (GLenum)c);
        glBindTexture(GL_TEXTURE_2D_ARRAY, sh_tex[c]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB32F, (GLsizei)aw, (GLsizei)ah, (GLsizei)nlayers,
                     0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    float *buf = NULL; uint32_t L = 0;
    uint32_t cc_max = perchunk ? 100000u : 1u;
    for (uint32_t cc = 0; cc < cc_max && (perchunk ? L < nlayers : L < 1); ++cc) {
        char path[600];
        if (perchunk) snprintf(path, sizeof path, "%s_c%03u.flm", lmfile, cc);
        else snprintf(path, sizeof path, "%s", lmfile);
        FILE *lf = fopen(path, "rb"); if (!lf) { if (perchunk) continue; free(buf); return -1; }
        char mg[4]; uint32_t caw, cah, nc, nmh;
        if (fread(mg,1,4,lf)!=4 || memcmp(mg,"FLM1",4) || fread(&caw,4,1,lf)!=1 ||
            fread(&cah,4,1,lf)!=1 || fread(&nc,4,1,lf)!=1 || fread(&nmh,4,1,lf)!=1) { fclose(lf); if (perchunk) continue; free(buf); return -1; }
        size_t cpix = (size_t)caw * cah * 3;
        buf = realloc(buf, cpix * sizeof(float));
        for (int c = 0; c < 9; ++c) {
            if (fread(buf, sizeof(float), cpix, lf) != cpix) break;
            glActiveTexture(GL_TEXTURE0 + 7u + (GLenum)c);
            glBindTexture(GL_TEXTURE_2D_ARRAY, sh_tex[c]);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, (GLint)L,
                            (GLsizei)caw, (GLsizei)cah, 1, GL_RGB, GL_FLOAT, buf);
        }
        if (!perchunk) /* single mode: rects come from the flm itself */
            for (uint32_t i = 0; i < nmh && (int)i < nm; ++i) { fread(&mrect[i], sizeof(lm_atlas_rect_t), 1, lf); mlayer[i] = 0; }
        fclose(lf);
        ++L;
    }
    free(buf);
    if (perchunk && L != nlayers) fprintf(stderr, "WARN: loaded %u/%u chunk layers\n", L, nlayers);
    return 0;
}

/* Load the per-chunk manifest + cache every chunk's coeffs in RAM, and create a
 * bounded SH texture array (SH_MAX_RESIDENT layers). Fills per-mesh chunk id
 * (mchunk) + atlas rect (mrect). Returns 0 on success. */
static int sh_stream_load(sh_stream_t *s, const char *lmfile, int nm,
                          lm_atlas_rect_t *mrect, int *mchunk)
{
    memset(s, 0, sizeof *s);
    char mp[600]; snprintf(mp, sizeof mp, "%s_manifest.bin", lmfile);
    FILE *mf = fopen(mp, "rb"); if (!mf) { fprintf(stderr, "no manifest %s\n", mp); return -1; }
    char mg[4]; uint32_t hdr[4];
    if (fread(mg,1,4,mf)!=4 || memcmp(mg,"ZLM1",4) || fread(hdr,sizeof hdr,1,mf)!=1) { fclose(mf); return -1; }
    uint32_t nm_m = hdr[0]; s->n_chunks = (int)hdr[1]; s->aw = hdr[2]; s->ah = hdr[3];
    for (int i = 0; i < nm; ++i) { mchunk[i] = -1; mrect[i] = (lm_atlas_rect_t){0,0,0,0}; }
    for (uint32_t i = 0; i < nm_m; ++i) {
        int32_t L; uint32_t r[4];
        if (fread(&L,4,1,mf)!=1 || fread(r,4,4,mf)!=4) break;
        if ((int)i < nm) { mchunk[i] = (int)L; mrect[i] = (lm_atlas_rect_t){ r[2], r[3], r[0], r[1] }; }
    }
    fclose(mf);

    s->ram = calloc((size_t)s->n_chunks, sizeof *s->ram);
    s->page = malloc((size_t)s->n_chunks * sizeof(int));
    s->vis = calloc((size_t)s->n_chunks, 1);
    if (!s->ram || !s->page || !s->vis) return -1;
    for (int c = 0; c < s->n_chunks; ++c) s->page[c] = -1;
    for (int i = 0; i < SH_MAX_RESIDENT; ++i) { s->slot_chunk[i] = -1; s->slot_used[i] = -1; }

    /* Cache each chunk's 9 coeff images in host RAM. */
    int loaded = 0;
    for (uint32_t cc = 0; cc < 100000u && loaded < s->n_chunks; ++cc) {
        char path[600]; snprintf(path, sizeof path, "%s_c%03u.flm", lmfile, cc);
        FILE *lf = fopen(path, "rb"); if (!lf) continue;
        char m2[4]; uint32_t caw, cah, nc, nmh;
        if (fread(m2,1,4,lf)!=4 || memcmp(m2,"FLM1",4) || fread(&caw,4,1,lf)!=1 ||
            fread(&cah,4,1,lf)!=1 || fread(&nc,4,1,lf)!=1 || fread(&nmh,4,1,lf)!=1) { fclose(lf); continue; }
        sh_chunk_ram_t *r = &s->ram[loaded]; r->w = (int)caw; r->h = (int)cah;
        size_t cpix = (size_t)caw * cah * 3;
        for (int k = 0; k < 9; ++k) {
            r->coeff[k] = malloc(cpix * sizeof(float));
            if (!r->coeff[k] || fread(r->coeff[k], sizeof(float), cpix, lf) != cpix) { fclose(lf); return -1; }
        }
        fclose(lf); ++loaded;
    }
    s->n_chunks = loaded;

    for (int k = 0; k < 9; ++k) {
        glGenTextures(1, &s->tex[k]);
        glActiveTexture(GL_TEXTURE0 + 7u + (GLenum)k);
        glBindTexture(GL_TEXTURE_2D_ARRAY, s->tex[k]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB32F, (GLsizei)s->aw, (GLsizei)s->ah,
                     SH_MAX_RESIDENT, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    fprintf(stderr, "sh_stream: %d chunks cached in RAM, %d resident layers, array %ux%u\n",
            s->n_chunks, SH_MAX_RESIDENT, s->aw, s->ah);
    return 0;
}

static GLuint sh_pp_compile(const char *vs, const char *fs)
{
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v,1,&vs,NULL); glCompileShader(v);
    glShaderSource(f,1,&fs,NULL); glCompileShader(f);
    GLint ok; glGetShaderiv(f,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[1024]; glGetShaderInfoLog(f,sizeof log,NULL,log); fprintf(stderr,"pp fs: %s\n",log); }
    GLuint p = glCreateProgram(); glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f); return p;
}

/* Create the lightmap-index prepass: an R32UI colour target the fragment writes
 * its chunk id (+1) into, so a readback yields the on-screen chunk set. */
static void sh_stream_prepass_init(sh_stream_t *s, int w, int h)
{
    s->pp_w = w; s->pp_h = h;
    s->pp_read = malloc((size_t)w * h * sizeof(uint32_t));
    glGenTextures(1, &s->pp_col);
    glBindTexture(GL_TEXTURE_2D, s->pp_col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &s->pp_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s->pp_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &s->pp_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s->pp_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->pp_col, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s->pp_depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s->pp_prog = sh_pp_compile(
        "#version 330 core\nlayout(location=0) in vec3 p;\nuniform mat4 u_mvp;\n"
        "void main(){ gl_Position = u_mvp * vec4(p,1.0); }\n",
        "#version 330 core\nout uint o;\nuniform uint u_chunk;\n"
        "void main(){ o = u_chunk + 1u; }\n");
}

/* Per frame: render the chunk-id prepass, read back the visible chunk set, page
 * those chunks into the bounded array, and set each mesh's resident layer. */
static void sh_stream_frame(sh_stream_t *s, const render_scene_t *scene,
                            const float view[16], const float proj[16],
                            const int *mchunk, render_renderable_t *items, int nm,
                            int main_w, int main_h)
{
    ++s->frame;
    /* vp = proj * view (column-major). */
    float vp[16];
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
        float sum = 0; for (int k = 0; k < 4; ++k) sum += proj[k*4+r] * view[c*4+k];
        vp[c*4+r] = sum;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, s->pp_fbo);
    glViewport(0, 0, s->pp_w, s->pp_h);
    GLuint zero[4] = {0,0,0,0}; glClearBufferuiv(GL_COLOR, 0, zero);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE);
    glUseProgram(s->pp_prog);
    glUniformMatrix4fv(glGetUniformLocation(s->pp_prog,"u_mvp"), 1, GL_FALSE, vp);
    GLint u_chunk = glGetUniformLocation(s->pp_prog, "u_chunk");
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (!r->mesh || (int)i >= nm || mchunk[i] < 0) continue;
        glUniform1ui(u_chunk, (GLuint)mchunk[i]);
        static_mesh_bind(r->mesh);
        for (uint32_t sm = 0; sm < r->mesh->submesh_count; ++sm)
            static_mesh_draw_submesh(r->mesh, sm);
    }
    /* Read back the chunk-id image and mark visible chunks. */
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, s->pp_w, s->pp_h, GL_RED_INTEGER, GL_UNSIGNED_INT, s->pp_read);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, main_w, main_h);

    memset(s->vis, 0, (size_t)s->n_chunks);
    for (int p = 0; p < s->pp_w * s->pp_h; ++p) {
        uint32_t v = s->pp_read[p];
        if (v > 0 && (int)(v-1) < s->n_chunks) s->vis[v-1] = 1;
    }
    for (int c = 0; c < s->n_chunks; ++c) if (s->vis[c]) sh_stream_touch(s, c);
    /* Set each mesh's resident layer (or -1 if its chunk isn't paged in). */
    for (int i = 0; i < nm; ++i)
        items[i].sh_layer = (mchunk[i] >= 0 && mchunk[i] < s->n_chunks) ? s->page[mchunk[i]] : -1;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 900
#define MAXM 256
#define MAX_LIGHTS 96

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static float frand(uint32_t *s){ *s=*s*1664525u+1013904223u; return (float)(*s>>8)*(1.0f/16777216.0f); }
static int group_of(const char *n){ if(strstr(n,"win")||strstr(n,"door"))return 0; if(strstr(n,"vault"))return 2; return 1; }
static int hld_cmpstr(const void *a,const void *b){ return strcmp((const char *)a,(const char *)b); }

static void save_ppm(const char *path,int w,int h){
    size_t row=(size_t)w*3; uint8_t *rgb=malloc(row*(size_t)h); if(!rgb)return;
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    FILE *f=fopen(path,"wb");
    if(f){ fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*row,1,row,f); fclose(f);
        printf("screenshot: %s\n",path);} free(rgb);
}

int main(int argc,char **argv){
    const char *dir = argc>1?argv[1]:"datasets/hall_lm";
    const char *bake = argc>2?argv[2]:"assets/arch/proc/prefabs/bake";
    const char *lmfile = argc>3?argv[3]:"/tmp/hall_prod.flm";
    const char *shot = argc>4?argv[4]:"/tmp/hall_lit_dynamic.ppm";

    /* Count the .dmesh files up front so every per-mesh array + the GPU command
     * queue / registry are sized to the ACTUAL mesh count -- this scales to
     * thousands of meshes instead of a fixed MAXM (128-slot queues would spin
     * forever once full). */
    int nm_cap=0; { DIR *dc=opendir(dir); struct dirent *ec;
        while(dc&&(ec=readdir(dc))){ if(strstr(ec->d_name,".dmesh")) ++nm_cap; }
        if(dc)closedir(dc); }
    if(nm_cap<1) nm_cap=1;
    obj_mesh_t         *dm     = calloc((size_t)nm_cap,sizeof *dm);
    int                *grp    = calloc((size_t)nm_cap,sizeof *grp);
    char             (*fnames)[128] = calloc((size_t)nm_cap,sizeof *fnames);
    static_mesh_t      *meshes = calloc((size_t)nm_cap,sizeof *meshes);
    render_submesh_t   *subs   = calloc((size_t)nm_cap,sizeof *subs);
    render_renderable_t *rb    = calloc((size_t)nm_cap,sizeof *rb);
    if(!dm||!grp||!fnames||!meshes||!subs||!rb){ fprintf(stderr,"oom (nm_cap=%d)\n",nm_cap); return 1; }
    /* GPU resources = meshes + 9 SH atlases + 8 material textures, all queued
     * before a single drain -> the queue must hold them all at once. */
    int res_cap = nm_cap + 9 + 8 + 16;

    if(SDL_Init(SDL_INIT_VIDEO)!=0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,1);   /* MSAA */
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,8);
    SDL_DisplayMode dmode; SDL_GetDesktopDisplayMode(0,&dmode);
    SDL_Window *win=SDL_CreateWindow("hall lit+dynamic",0,0,dmode.w,dmode.h,
        SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_GLContext gc=SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win,gc);
    SDL_GL_SetSwapInterval(0); /* vsync off: measure real render cost. */
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    int W,H; SDL_GL_GetDrawableSize(win,&W,&H); /* actual pixels (HiDPI-safe) */
    glEnable(GL_MULTISAMPLE);
    printf("fullscreen %dx%d (msaa 8x)\n",W,H);
    printf("GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
    gl_loader_t loader={sdl_get_proc,NULL};

    /* --- Fiber-based resource subsystem: every PBR texture + static mesh is
     * created through the job system -> command queue -> render-thread executor. --- */
    job_system_t jobs;
    if(job_system_create(&jobs,4,256,64*1024,4096,0)!=JOB_CREATE_OK){ fprintf(stderr,"job create failed\n"); return 1; }
    job_system_start(&jobs);
    size_t arena_cap=192u*1024u*1024u; void *arena_mem=malloc(arena_cap);
    arena_t rarena; arena_init(&rarena,arena_mem,arena_cap);
    gpu_registry_t greg; gpu_registry_init(&greg,(uint32_t)res_cap);
    gpu_cmd_t *gslots=calloc((size_t)res_cap,sizeof *gslots);
    atomic_int *gstates=calloc((size_t)res_cap,sizeof *gstates);
    if(!gslots||!gstates){ fprintf(stderr,"oom (res_cap=%d)\n",res_cap); return 1; }
    gpu_cmd_queue_t gqueue; gpu_cmd_queue_init(&gqueue,gslots,gstates,(uint32_t)res_cap);
    gpu_executor_t gexec; if(!gpu_executor_init(&gexec,&loader,&greg)){ fprintf(stderr,"executor init failed\n"); return 1; }
    job_counter_t rcounter; job_counter_init(&rcounter,0);

    /* --- Load dual-UV dmeshes in a DETERMINISTIC (sorted) order so the mesh
     * index -> atlas rect mapping matches the bake regardless of which machine
     * baked it. readdir() order is filesystem-dependent; baking on one box and
     * rendering on another (e.g. a chimera GPU bake) would otherwise shuffle the
     * rects and splotch every surface. --- */
    int nm=0; int nf=0;
    DIR *d=opendir(dir); struct dirent *e;
    while(d&&(e=readdir(d))&&nf<nm_cap){ if(!strstr(e->d_name,".dmesh"))continue;
        snprintf(fnames[nf],sizeof fnames[nf],"%s",e->d_name); ++nf; }
    if(d)closedir(d);
    qsort(fnames,(size_t)nf,sizeof fnames[0],hld_cmpstr);
    for(int fi=0;fi<nf;++fi){ char p[512]; snprintf(p,sizeof p,"%s/%s",dir,fnames[fi]);
        if(dmesh_load(p,&dm[nm])==0){ grp[nm]=(getenv("RENDER_FLOOR_GREEN")&&strstr(fnames[fi],"floor"))?3:group_of(fnames[fi]); ++nm; } }
    printf("loaded %d dmeshes\n",nm);

    /* --- Load the baked SH lightmap(s) into 9 GL_TEXTURE_2D_ARRAY pages. Single
     * atlas (1 layer) or per-chunk (LM_PERCHUNK: <lmfile>_manifest.bin +
     * <lmfile>_c*.flm, one layer per chunk). Each mesh carries its page layer. */
    int perchunk = getenv("LM_PERCHUNK") != NULL;
    /* LM_STREAM (rpg-ojuq): keep all chunks in RAM and page only visible ones into
     * a bounded array via a lightmap-index prepass. Implies per-chunk. */
    int stream = getenv("LM_STREAM") != NULL;
    GLuint sh_tex[9]={0};
    lm_atlas_rect_t *mrect = calloc((size_t)nm, sizeof *mrect);
    int *mlayer = calloc((size_t)nm, sizeof *mlayer);   /* stream: mesh chunk id */
    uint32_t atlas_w=0, atlas_h=0;
    static sh_stream_t sstream;
    if(!mrect||!mlayer){ fprintf(stderr,"oom lightmap tables\n"); return 1; }
    if(stream){
        if(sh_stream_load(&sstream, lmfile, nm, mrect, mlayer)!=0){
            fprintf(stderr,"stream lightmap load failed (%s)\n", lmfile); return 1; }
        for(int c=0;c<9;++c) sh_tex[c]=sstream.tex[c];
        atlas_w=sstream.aw; atlas_h=sstream.ah;
    } else if(load_sh_arrays(lmfile, perchunk, nm, sh_tex, mrect, mlayer, &atlas_w, &atlas_h)!=0){
        fprintf(stderr,"lightmap load failed (%s%s)\n", lmfile, perchunk?" [per-chunk]":"");
        return 1;
    }
    printf("lightmap array %ux%u (%s)\n", atlas_w, atlas_h, stream?"streaming":(perchunk?"per-chunk":"single"));

    /* --- Build static meshes: uv1 remapped into each mesh's atlas rect. --- */
    float amin[3]={1e30f,1e30f,1e30f},amax[3]={-1e30f,-1e30f,-1e30f};
    lm_atlas_t atlas={atlas_w,atlas_h};
    for(int i=0;i<nm;++i){
        for(uint32_t v=0;v<dm[i].vert_count;++v) for(int c=0;c<3;++c){
            float q=dm[i].positions[v*3+c]; if(q<amin[c])amin[c]=q; if(q>amax[c])amax[c]=q; }
        /* uv1 remap + the create descriptor live in the arena so they persist
         * until the executor creates the mesh on the render thread. */
        float *uv1=arena_alloc(&rarena,16u,(size_t)dm[i].vert_count*2*sizeof(float));
        const lm_atlas_rect_t *rc = (mrect[i].w>0)?&mrect[i]:NULL;
        for(uint32_t v=0;v<dm[i].vert_count;++v){
            float au=dm[i].uvs1[v*2], av=dm[i].uvs1[v*2+1];
            if(rc) lm_atlas_remap_uv(rc,&atlas,dm[i].uvs1[v*2],dm[i].uvs1[v*2+1],&au,&av);
            uv1[v*2]=au; uv1[v*2+1]=av;
        }
        subs[i]=(render_submesh_t){0,dm[i].index_count,0};
        mesh_load_t *ml=arena_alloc(&rarena,16u,sizeof *ml); memset(&ml->info,0,sizeof ml->info);
        ml->info.positions=dm[i].positions; ml->info.normals=dm[i].normals; ml->info.tangents=dm[i].tangents;
        ml->info.uv0=dm[i].uvs; ml->info.uv1=uv1;
        ml->info.indices=dm[i].indices; ml->info.vertex_count=dm[i].vert_count; ml->info.index_count=dm[i].index_count;
        ml->info.submeshes=&subs[i]; ml->info.submesh_count=1; ml->out=&meshes[i];
        gpu_cmd_t mc; memset(&mc,0,sizeof mc); mc.type=GPU_CMD_CUSTOM; mc.execute=finalize_mesh; mc.ctx=ml;
        while(!gpu_cmd_push(&gqueue,&mc)) sched_yield();
    }
    float span[3]={amax[0]-amin[0],amax[1]-amin[1],amax[2]-amin[2]};
    float cx=(amin[0]+amax[0])*0.5f,cy=(amin[1]+amax[1])*0.5f,cz=(amin[2]+amax[2])*0.5f;
    printf("scene AABB: min(%.2f,%.2f,%.2f) max(%.2f,%.2f,%.2f) span(%.2f,%.2f,%.2f)\n",
           amin[0],amin[1],amin[2],amax[0],amax[1],amax[2],span[0],span[1],span[2]);

    /* --- Materials: every PBR texture loaded on a fiber -> queue -> executor. --- */
    char q[512]; texture_t tb_a,tb_n,tb_o,tb_r,ts_a,ts_r,tv_a,tv_r;
    #define LOADT(outp,relpath,format) do{ \
        snprintf(q,sizeof q,"%s/" relpath,bake); \
        size_t pl_=strlen(q)+1; char *pc_=arena_alloc(&rarena,1u,pl_); memcpy(pc_,q,pl_); \
        tex_load_t *tl_=arena_alloc(&rarena,16u,sizeof *tl_); \
        tl_->queue=&gqueue; tl_->arena=&rarena; tl_->path=pc_; tl_->fmt=(format); tl_->out=(outp); tl_->px=NULL; \
        job_dispatch(&jobs,tex_load_fiber,tl_,0,&rcounter); }while(0)
    LOADT(&tb_a,"albedo.png",TEXTURE_FORMAT_SRGB8);
    LOADT(&tb_n,"normal.png",TEXTURE_FORMAT_RGB8);
    LOADT(&tb_o,"ao.png",TEXTURE_FORMAT_RGB8);
    LOADT(&tb_r,"roughness.png",TEXTURE_FORMAT_RGB8);
    LOADT(&ts_a,"ashlar_albedo.png",TEXTURE_FORMAT_SRGB8);
    LOADT(&ts_r,"ashlar_roughness.png",TEXTURE_FORMAT_RGB8);
    LOADT(&tv_a,"vault_albedo.png",TEXTURE_FORMAT_SRGB8);
    LOADT(&tv_r,"vault_roughness.png",TEXTURE_FORMAT_RGB8);
    #undef LOADT
    /* Barrier: fibers decode + enqueue, then the render thread realises every
     * mesh + texture in one drain. */
    job_wait_counter(&rcounter,0);
    uint32_t created=gpu_executor_drain(&gexec,&gqueue);
    printf("resource executor created %u GPU resources (meshes + textures)\n",created);
    render_material_t mats[4];
    /* Brick/stone contrast (u_contrast): punch up the brick-vs-mortar tonal range
     * so the masonry reads with more depth. */
    float brick_contrast = getenv("BRICK_CONTRAST") ? (float)atof(getenv("BRICK_CONTRAST")) : 1.6f;
    material_init(&mats[0]); mats[0].maps[MATERIAL_TEX_ALBEDO]=&tb_a; mats[0].maps[MATERIAL_TEX_NORMAL]=&tb_n;
    mats[0].maps[MATERIAL_TEX_AO]=&tb_o; mats[0].maps[MATERIAL_TEX_ROUGHNESS]=&tb_r; mats[0].normal_scale=1.6f;
    mats[0].roughness_min=0.25f; mats[0].roughness_max=1.0f; mats[0].contrast=brick_contrast;
    material_init(&mats[1]); mats[1].maps[MATERIAL_TEX_ALBEDO]=&ts_a; mats[1].maps[MATERIAL_TEX_ROUGHNESS]=&ts_r;
    mats[1].roughness_min=0.2f; mats[1].roughness_max=1.0f; mats[1].contrast=brick_contrast;
    material_init(&mats[2]); mats[2].maps[MATERIAL_TEX_ALBEDO]=&tv_a; mats[2].maps[MATERIAL_TEX_ROUGHNESS]=&tv_r;
    mats[2].roughness_min=0.2f; mats[2].roughness_max=1.0f; mats[2].contrast=brick_contrast*0.9f;
    /* Floor (group 3): lush-grass green -- ashlar albedo tinted green, matching
     * the bake's green floor reflectance so the render + colour-bleed agree. */
    material_init(&mats[3]); mats[3].maps[MATERIAL_TEX_ALBEDO]=&ts_a; mats[3].maps[MATERIAL_TEX_ROUGHNESS]=&ts_r;
    mats[3].roughness_min=0.5f; mats[3].roughness_max=1.0f;
    mats[3].tint[0]=0.15f; mats[3].tint[1]=0.85f; mats[3].tint[2]=0.12f;

    /* --- Dynamic clustered point lights ("magic particles"). --- */
    render_light_t lb[MAX_LIGHTS];
    render_light_store_t lights; render_light_store_init(&lights,lb,MAX_LIGHTS);
    float pal[6][3]={{1,0.3f,0.3f},{0.3f,1,0.4f},{0.35f,0.5f,1},{1,0.85f,0.3f},{1,0.4f,0.9f},{0.3f,1,1}};
    int lax=(span[0]>span[2])?0:2; int cax=(lax==0)?2:0;
    float hall_len=(span[0]>span[2])?span[0]:span[2];
    int shadow_only = getenv("SHADOW_ONLY") && atoi(getenv("SHADOW_ONLY"));
    int one_light = getenv("HALL_ONE") && atoi(getenv("HALL_ONE")); /* 1 light + lightmap */
    int spot_only = getenv("SPOT_ONLY") && atoi(getenv("SPOT_ONLY")); /* just the sconce */
    int csm_demo = getenv("HALL_CSM") && atoi(getenv("HALL_CSM")); /* sun CSM + moving box */
    int lm_only = getenv("LM_ONLY") && atoi(getenv("LM_ONLY")); /* pure baked lightmap, no dynamic lights */
    /* Light index 0: a bright SHADOW-CASTING point light. Placed near the camera
     * end and off to one side at mid height so the central column rakes a long
     * shadow across the floor and far columns. */
    { render_light_t s; memset(&s,0,sizeof s); s.kind=RENDER_LIGHT_POINT;
      s.position[0]=cx; s.position[1]=amin[1]+0.45f*span[1]; s.position[2]=cz;
      s.position[lax]=amin[lax]+0.16f*span[lax];
      s.position[cax]=amin[cax]+0.80f*span[cax];
      s.color[0]=s.color[1]=s.color[2]=1.0f; s.intensity=(spot_only||csm_demo||lm_only)?0.0f:(shadow_only?14.0f:8.0f); s.range=hall_len*1.6f;
      s.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&s); }
    /* Light index 1: a warm-orange UPWARD sconce SPOTLIGHT (candle brightness),
     * on a side wall pointing at the vault -- casts a 2D spot shadow of the ribs
     * and column onto the ceiling. */
    { render_light_t sp; memset(&sp,0,sizeof sp); sp.kind=RENDER_LIGHT_SPOT;
      sp.position[cax]=amin[cax]+0.5f*span[cax]+0.55f; /* just to the side of the central column */
      sp.position[1]=amin[1]+0.20f*span[1];
      sp.position[lax]=amin[lax]+0.34f*span[lax];      /* at the near central column */
      sp.direction[0]=0; sp.direction[1]=1; sp.direction[2]=0;
      sp.color[0]=1.0f; sp.color[1]=0.55f; sp.color[2]=0.22f; /* warm orange */
      sp.intensity=(csm_demo||lm_only)?0.0f:(spot_only?22.0f:6.0f); sp.range=hall_len;
      sp.cos_inner=cosf(0.80f); sp.cos_outer=cosf(1.05f); /* ~120-deg cone */
      sp.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&sp); }
    uint32_t rng=4242;
    for(int i=0;i<((shadow_only||one_light||spot_only||csm_demo||lm_only)?0:64);++i){ render_light_t l; memset(&l,0,sizeof l); l.kind=RENDER_LIGHT_POINT;
        l.position[0]=amin[0]+frand(&rng)*span[0]; l.position[1]=amin[1]+0.12f*span[1]+frand(&rng)*0.7f*span[1];
        l.position[2]=amin[2]+frand(&rng)*span[2]; const float *pc=pal[i%6];
        l.color[0]=pc[0]; l.color[1]=pc[1]; l.color[2]=pc[2];
        l.intensity=6.0f; l.range=2.2f; l.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&l); }
    printf("point lights: %u\n",lights.count);

    /* --- Camera + scene. --- */
    int lenax=(span[0]>span[2])?0:2; int crossax=(lenax==0)?2:0;
    /* The hall is a compact colonnade; the 400 m open zone dwarfs it. Detect the
     * hall so we can frame a specific interior shot for it. */
    int is_hall=(hall_len<100.0f);
    float center[3]={cx,cy,cz};   /* per-axis centre — index by axis, NOT cx/cz. */
    render_camera_t cam; float eye[3]={cx,cy,cz},tgt[3]={cx,cy,cz},up[3]={0,1,0};
    if(is_hall){
        /* Start behind the middle pillar (pillar row is centred on lenax), in the
         * aisle, at standing eye height, looking down the colonnade. */
        eye[lenax]=amin[lenax]+span[lenax]*0.42f; /* just behind the central pillar */
        eye[crossax]=center[crossax];             /* centred in the aisle */
        eye[1]=amin[1]+span[1]*0.30f;             /* ~eye height above the floor */
        tgt[lenax]=amax[lenax];                   /* look down the hall */
        tgt[crossax]=center[crossax]; tgt[1]=eye[1];
    } else {
        eye[lenax]=amin[lenax]+span[lenax]*0.08f; tgt[lenax]=amax[lenax]-span[lenax]*0.08f;
        eye[1]=amin[1]+span[1]*0.35f; tgt[1]=amin[1]+span[1]*0.35f;
    }
    /* Far plane must clear the whole scene (a 400 m zone dwarfs the hall's 60 m). */
    float cam_far=fmaxf(120.0f,hall_len*1.8f);
    render_camera_look_at(&cam,eye,tgt,up,60.0f*(float)M_PI/180.0f,(float)W/(float)H,0.2f,cam_far);
    render_scene_t scene; render_scene_init(&scene,rb,nm_cap);
    float model[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(int i=0;i<nm;++i){ render_scene_add(&scene,&meshes[i],&mats[grp[i]],model);
        scene.items[i].sh_layer = stream ? -1 : mlayer[i]; }  /* stream: set per frame */
    /* A dynamic caster: everything above is static (baked once); the box below is
     * re-shadowed every frame, so the CSM co-samples static wall + moving box. */
    static_mesh_t box; int box_idx=-1;
    if(csm_demo){
      if(getenv("CSM_NOBOX")==NULL){
        static_mesh_create_box(&loader,0.7f,0.7f,0.7f,&box);
        render_scene_mark_dynamic(&scene);
        box_idx=(int)scene.count;
        render_scene_add(&scene,&box,&mats[grp[0]],model); /* model updated per frame */
      }
    }
    scene.camera=cam; scene.lights=&lights;

    /* --- Driver: forward+ with the baked SH lightmap enabled. --- */
    render_forward_config_t fcfg; memset(&fcfg,0,sizeof fcfg);
    fcfg.loader=&loader; fcfg.cluster=(cluster_config_t){16,16,24,0.2f,60.0f};
    fcfg.max_lights=MAX_LIGHTS; fcfg.index_capacity=16u*16u*24u*16u;
    fcfg.screen_w=(float)W; fcfg.screen_h=(float)H;
    /* u_sun_dir points TOWARD the sun; the CSM negates it for the travel dir.
     * Must equal the lightmap bake's sun (hall_bake.c: travel (0.42,-0.50,0.76)),
     * so to-sun = (-0.42,0.50,-0.76) -- the direct sun + its CSM shadow then line
     * up with the baked indirect bounce. */
    fcfg.sun_dir[0]=-0.42f; fcfg.sun_dir[1]=0.50f; fcfg.sun_dir[2]=-0.76f;
    fcfg.sun_color[0]=fcfg.sun_color[1]=fcfg.sun_color[2]=0.0f; /* sun already baked into SH */
    fcfg.ambient[0]=fcfg.ambient[1]=fcfg.ambient[2]=0.0f;
    /* CSM demo combines the baked indirect lightmap (reduced strength) with the
     * direct sun + its CSM shadows. */
    fcfg.sh_enabled=(shadow_only||spot_only)?0:1; fcfg.sh_scale=lm_only?1.0f:(csm_demo?0.5f:0.4f); for(int c=0;c<9;c++) fcfg.sh_tex[c]=sh_tex[c];
    fcfg.shadow_light=0; fcfg.shadow_res=1024; fcfg.shadow_near=0.1f;
    fcfg.shadow_far=hall_len*1.8f; fcfg.shadow_bias=0.08f;
    fcfg.spot_light=1; fcfg.spot_res=1024; fcfg.spot_near=0.05f;
    fcfg.spot_far=hall_len*1.5f; fcfg.spot_bias=0.05f;
    if((csm_demo||lm_only) && getenv("LM_NOCSM")==NULL){
        /* Warm directional sun; 3 cascades split logarithmically, static baked
         * once + a low-res dynamic map. Sun is NOT baked into the SH here. */
        /* Direct sun -- brighter than the bake radiance so the shafts read
         * strongly over the (reduced) baked indirect fill. */
        fcfg.sun_color[0]=9.0f; fcfg.sun_color[1]=8.4f; fcfg.sun_color[2]=7.2f;
        /* Small flat ambient so dynamic objects (no baked lightmap) aren't black. */
        fcfg.ambient[0]=fcfg.ambient[1]=fcfg.ambient[2]=0.15f;
        fcfg.dir_cascades=6; fcfg.dir_static_res=2048; fcfg.dir_dynamic_res=2048;
        fcfg.dir_lambda=0.6f;
        /* PCSS depth-compare bias in metres (DIR_BIAS env, default 5cm). */
        fcfg.dir_bias=getenv("DIR_BIAS")?(float)atof(getenv("DIR_BIAS")):0.05f;
        /* Sun light-source size in METRES: the PCSS penumbra grows with the
         * occluder->receiver gap times this, mapped per-cascade to a mip LOD via
         * the cascade texel size (world-aligned). CSM_SOFT env, default 0.7m. */
        fcfg.dir_softness=getenv("CSM_SOFT")?(float)atof(getenv("CSM_SOFT")):0.7f;
        /* Slice the WHOLE view frustum (to the far clip) into the fixed cascade
         * count; 0 = use the camera far plane rather than a fixed cap. */
        fcfg.dir_max_distance=0.0f;
        /* Fit the CSM cascades to the whole scene AABB (+pad) so tall casters
         * (vaults) and geometry outside the view are never clipped. */
        for(int k=0;k<3;++k){ fcfg.shadow_scene_min[k]=amin[k]-1.0f; fcfg.shadow_scene_max[k]=amax[k]+1.0f; }
    }
    struct timespec t0_,t1_; clock_gettime(CLOCK_MONOTONIC,&t0_);
    render_forward_t fwd;
    if(!render_forward_init(&fwd,&fcfg)){ fprintf(stderr,"render_forward_init failed\n"); return 1; }
    clock_gettime(CLOCK_MONOTONIC,&t1_);
    fprintf(stderr,"[perf] render_forward_init: %.1f ms\n",
        (t1_.tv_sec-t0_.tv_sec)*1e3+(t1_.tv_nsec-t0_.tv_nsec)*1e-6);

    glViewport(0,0,W,H);
    /* lm_only: run an interactive flythrough so a large baked zone can actually
     * be looked at (ESC / window-close quits); others render a few frames. */
    int nframes=csm_demo?600:(lm_only?1000000:3);
    int save_frame=csm_demo?30:(lm_only?140:(nframes-2));
    struct timespec win_t0; clock_gettime(CLOCK_MONOTONIC,&win_t0);
    int win_frames=0;                    /* frames since the last per-second report. */
    if(stream) sh_stream_prepass_init(&sstream, W/4>0?W/4:1, H/4>0?H/4:1);
    for(int frame=0;frame<nframes;++frame){
        if(lm_only||csm_demo){
            /* Smooth ping-pong dolly down the colonnade, looking in the travel
             * direction with a gentle side sway; poll for quit. Used for both the
             * lightmap-only flythrough and the CSM demo so the camera stays
             * inside the building either way. */
            SDL_Event ev; while(SDL_PollEvent(&ev)){
                if(ev.type==SDL_QUIT || (ev.type==SDL_KEYDOWN && ev.key.keysym.sym==SDLK_ESCAPE)) frame=nframes-1;
            }
            int sax=(lenax==0)?2:0;
            float ph=(float)frame*0.0012f;
            float t=0.5f-0.5f*cosf(ph);
            float e[3]={cx,cy,cz}, tg[3]={cx,cy,cz};
            if(csm_demo && !is_hall){
                /* Open colonnade: stand on the SHADOW side and look toward the sun
                 * so the columns' floor shadows lead straight toward the camera and
                 * the sun-lit column faces are visible. u_sun_dir points to the sun;
                 * -h is the shadow side. */
                float h[3]={fcfg.sun_dir[0],0.0f,fcfg.sun_dir[2]};
                float hl=sqrtf(h[0]*h[0]+h[2]*h[2]); if(hl<1e-4f)hl=1.0f; h[0]/=hl; h[2]/=hl;
                float pan=0.18f*sinf(ph);          /* gentle side drift. */
                e[0]=cx - h[0]*0.42f*span[0] + h[2]*pan*span[0];
                e[2]=cz - h[2]*0.42f*span[2] - h[0]*pan*span[2];
                e[1]=amin[1]+0.24f*span[1];         /* low, so shadows rake across. */
                tg[0]=cx + h[0]*0.30f*span[0];      /* look toward the sun. */
                tg[2]=cz + h[2]*0.30f*span[2];
                tg[1]=amin[1]+0.06f*span[1];        /* down onto the lit floor. */
            } else if(is_hall){
                /* Begin behind the central pillar and dolly forward down the aisle
                 * (ping-pong around the middle), staying centred and looking down
                 * the hall so the camera never leaves the interior. */
                e[lenax]=amin[lenax]+span[lenax]*(0.42f-0.30f*sinf(ph));
                e[1]=amin[1]+span[1]*0.30f;
                e[sax]=center[sax]+0.08f*span[sax]*sinf(ph*1.7f);
                tg[lenax]=amax[lenax];            /* always look down the colonnade */
                tg[1]=e[1]; tg[sax]=center[sax];
            } else {
                e[lenax]=amin[lenax]+span[lenax]*(0.03f+0.94f*t);
                e[1]=amin[1]+span[1]*0.42f;
                e[sax]=center[sax]+0.26f*span[sax]*sinf(ph*1.7f);
                float dir=(sinf(ph)>=0.0f)?1.0f:-1.0f;
                tg[lenax]=e[lenax]+dir*span[lenax];
                tg[1]=e[1]-0.05f*span[1];
                tg[sax]=center[sax];
            }
            render_camera_look_at(&cam,e,tg,up,62.0f*(float)M_PI/180.0f,(float)W/(float)H,0.2f,cam_far);
            scene.camera=cam;
        }
        if(csm_demo && box_idx>=0){
            /* Fly the box back and forth right in front of the sun-side windows at
             * window height, so it intercepts the incoming beams and its dynamic
             * shadow masks the window shafts on the floor. */
            float ph=(float)frame/50.0f;
            float t=0.5f+0.42f*sinf(ph);
            float *m=scene.items[box_idx].model;
            for(int k=0;k<16;++k) m[k]=(k%5==0)?1.0f:0.0f;
            m[13]=amin[1]+0.44f*span[1];                       /* window height. */
            m[12+cax]=amin[cax]+0.20f*span[cax];               /* just inside the sun-side wall. */
            m[12+lenax]=amin[lenax]+span[lenax]*t;             /* sweep past the windows. */
        }
        /* Stream: page in the chunks visible this frame (lightmap-index prepass)
         * and set each mesh's resident layer before the main pass. */
        if(stream) sh_stream_frame(&sstream,&scene,scene.camera.view,scene.camera.proj,mlayer,scene.items,nm,W,H);
        glClearColor(0.02f,0.02f,0.03f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        render_forward_render(&fwd,&scene);
        if(frame==save_frame) save_ppm(shot,W,H);
        SDL_GL_SwapWindow(win);
        /* Report the average render FPS roughly once per second. */
        ++win_frames;
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        double el=(now.tv_sec-win_t0.tv_sec)+(now.tv_nsec-win_t0.tv_nsec)*1e-9;
        if(el>=1.0){ fprintf(stderr,"[perf] %.1f fps (%.2f ms/frame)\n",
            win_frames/el,1e3*el/win_frames); win_frames=0; win_t0=now; }
    }
    render_forward_destroy(&fwd);
    if(csm_demo) static_mesh_destroy(&box);
    for(int i=0;i<nm;++i){ static_mesh_destroy(&meshes[i]); obj_mesh_free(&dm[i]); }
    free(dm); free(grp); free(fnames); free(meshes); free(subs); free(rb);
    free(gslots); free(gstates);
    free(mrect); free(mlayer);
    gpu_executor_destroy(&gexec); gpu_registry_destroy(&greg);
    job_system_shutdown(&jobs); free(arena_mem);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
