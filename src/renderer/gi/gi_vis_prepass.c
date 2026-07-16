/**
 * @file gi_vis_prepass.c
 * @brief Low-res visibility prepass (see gi_vis_prepass.h).
 */
#include "ferrum/renderer/gi/gi_vis_prepass.h"

#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/mesh/static_mesh.h"

static GLuint compile2(const char *vs, const char *fs)
{
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
    glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
    GLint ok = 0; glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(f, sizeof log, NULL, log);
               fprintf(stderr, "gi_vis_prepass fs: %s\n", log); }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

int gi_vis_prepass_init(gi_vis_prepass_t *pp, int w, int h, int n_chunks)
{
    if (pp == NULL || w < 1 || h < 1 || n_chunks < 1)
        return -1;
    memset(pp, 0, sizeof *pp);
    pp->w = w; pp->h = h; pp->n_chunks = n_chunks;
    pp->read = malloc((size_t)w * h * sizeof(uint32_t));
    pp->visible = calloc((size_t)n_chunks, 1);
    if (pp->read == NULL || pp->visible == NULL) { gi_vis_prepass_destroy(pp); return -1; }

    glGenTextures(1, &pp->col);
    glBindTexture(GL_TEXTURE_2D, pp->col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &pp->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, pp->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &pp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pp->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pp->col, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pp->depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    pp->prog_mesh = compile2(
        "#version 330 core\nlayout(location=0) in vec3 p;\nuniform mat4 u_mvp;\n"
        "void main(){ gl_Position = u_mvp * vec4(p,1.0); }\n",
        "#version 330 core\nout uint o;\nuniform uint u_chunk;\n"
        "void main(){ o = u_chunk + 1u; }\n");
    /* WORLD mode: pass world pos, pick the first containing SDF chunk box. */
    pp->prog_world = compile2(
        "#version 330 core\nlayout(location=0) in vec3 p;\nuniform mat4 u_mvp;\n"
        "out vec3 v_world;\n"
        "void main(){ v_world = p; gl_Position = u_mvp * vec4(p,1.0); }\n",
        "#version 330 core\nin vec3 v_world;\nout uint o;\n"
        "uniform int u_nboxes;\n"
        "uniform vec3 u_bmin[64];\nuniform vec3 u_bmax[64];\n"
        "void main(){ uint id = 0u;\n"
        "  for(int i=0;i<u_nboxes;++i){\n"
        "    if(all(greaterThanEqual(v_world,u_bmin[i])) && all(lessThanEqual(v_world,u_bmax[i]))){ id = uint(i)+1u; break; }\n"
        "  }\n"
        "  o = id; }\n");
    return 0;
}

/* Shared: bind FBO, clear, set depth state, restore afterwards. */
static void pp_begin(gi_vis_prepass_t *pp)
{
    glBindFramebuffer(GL_FRAMEBUFFER, pp->fbo);
    glViewport(0, 0, pp->w, pp->h);
    GLuint zero[4] = { 0, 0, 0, 0 };
    glClearBufferuiv(GL_COLOR, 0, zero);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE);
}

static void pp_readback(gi_vis_prepass_t *pp, int main_w, int main_h)
{
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, pp->w, pp->h, GL_RED_INTEGER, GL_UNSIGNED_INT, pp->read);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, main_w, main_h);
    memset(pp->visible, 0, (size_t)pp->n_chunks);
    int npx = pp->w * pp->h;
    for (int i = 0; i < npx; ++i) {
        uint32_t v = pp->read[i];
        if (v > 0u && (int)(v - 1u) < pp->n_chunks) pp->visible[v - 1u] = 1;
    }
}

/* Compute vp = proj*view (column-major). */
static void mul_vp(const float view[16], const float proj[16], float vp[16])
{
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) sum += proj[k*4+r] * view[c*4+k];
        vp[c*4+r] = sum;
    }
}

void gi_vis_prepass_run_mesh(gi_vis_prepass_t *pp, const render_scene_t *scene,
                             const float view[16], const float proj[16],
                             const int *mchunk, int nm, int main_w, int main_h)
{
    if (pp == NULL || scene == NULL || mchunk == NULL) return;
    float vp[16]; mul_vp(view, proj, vp);
    pp_begin(pp);
    glUseProgram(pp->prog_mesh);
    glUniformMatrix4fv(glGetUniformLocation(pp->prog_mesh, "u_mvp"), 1, GL_FALSE, vp);
    GLint u_chunk = glGetUniformLocation(pp->prog_mesh, "u_chunk");
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL || (int)i >= nm || mchunk[i] < 0) continue;
        glUniform1ui(u_chunk, (GLuint)mchunk[i]);
        static_mesh_bind(r->mesh);
        for (uint32_t sm = 0; sm < r->mesh->submesh_count; ++sm)
            static_mesh_draw_submesh(r->mesh, sm);
    }
    pp_readback(pp, main_w, main_h);
}

void gi_vis_prepass_run_world(gi_vis_prepass_t *pp, const render_scene_t *scene,
                              const float view[16], const float proj[16],
                              const float *box_min, const float *box_max,
                              int n_boxes, int main_w, int main_h)
{
    if (pp == NULL || scene == NULL || box_min == NULL || box_max == NULL) return;
    if (n_boxes > GI_VIS_MAX_BOXES) n_boxes = GI_VIS_MAX_BOXES;
    float vp[16]; mul_vp(view, proj, vp);
    pp_begin(pp);
    glUseProgram(pp->prog_world);
    glUniformMatrix4fv(glGetUniformLocation(pp->prog_world, "u_mvp"), 1, GL_FALSE, vp);
    glUniform1i(glGetUniformLocation(pp->prog_world, "u_nboxes"), n_boxes);
    glUniform3fv(glGetUniformLocation(pp->prog_world, "u_bmin"), n_boxes, box_min);
    glUniform3fv(glGetUniformLocation(pp->prog_world, "u_bmax"), n_boxes, box_max);
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL) continue;
        static_mesh_bind(r->mesh);
        for (uint32_t sm = 0; sm < r->mesh->submesh_count; ++sm)
            static_mesh_draw_submesh(r->mesh, sm);
    }
    pp_readback(pp, main_w, main_h);
}

void gi_vis_prepass_destroy(gi_vis_prepass_t *pp)
{
    if (pp == NULL) return;
    if (pp->prog_mesh) glDeleteProgram(pp->prog_mesh);
    if (pp->prog_world) glDeleteProgram(pp->prog_world);
    if (pp->fbo) glDeleteFramebuffers(1, &pp->fbo);
    if (pp->col) glDeleteTextures(1, &pp->col);
    if (pp->depth) glDeleteRenderbuffers(1, &pp->depth);
    free(pp->read);
    free(pp->visible);
    memset(pp, 0, sizeof *pp);
}
