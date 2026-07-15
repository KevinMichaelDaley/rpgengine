/**
 * @file lm_jfa_sdf.c
 * @brief GPU Jump-Flood SDF, validated against a known composition of ANALYTIC
 *        SDFs (rpg-bpyr). Occupancy is rasterised from analytic_sdf < 0, the JFA
 *        compute kernel builds a signed distance field on the GPU (ping-pong
 *        SSBOs), and the readback is compared to the analytic SDF within a
 *        discretisation tolerance. No host-side JFA reference needed.
 *
 * This also stands up the offline GL 4.3 compute context the baker will reuse.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The project's glad is GL 3.3; compute is 4.3. Shim the missing constants +
 * entry points (loaded via SDL_GL_GetProcAddress after context creation). */
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_DYNAMIC_COPY
#define GL_DYNAMIC_COPY 0x88EA
#endif
static void (*p_glDispatchCompute)(GLuint, GLuint, GLuint);
static void (*p_glMemoryBarrier)(GLbitfield);
static void (*p_glBindBufferBase)(GLenum, GLuint, GLuint);
#define glDispatchCompute p_glDispatchCompute
#define glMemoryBarrier p_glMemoryBarrier
#define glBindBufferBase p_glBindBufferBase

/* ── Analytic SDF composition (ground truth) ── */
static float sdf_sphere(float x, float y, float z, float cx, float cy, float cz, float r) {
    float dx = x - cx, dy = y - cy, dz = z - cz;
    return sqrtf(dx*dx + dy*dy + dz*dz) - r;
}
static float sdf_box(float x, float y, float z, float cx, float cy, float cz,
                     float hx, float hy, float hz) {
    float qx = fabsf(x - cx) - hx, qy = fabsf(y - cy) - hy, qz = fabsf(z - cz) - hz;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, oz = qz > 0 ? qz : 0;
    float outside = sqrtf(ox*ox + oy*oy + oz*oz);
    float inside = fmaxf(qx, fmaxf(qy, qz)); if (inside > 0) inside = 0;
    return outside + inside;
}
/* Union of a sphere and a box. */
static float scene_sdf(float x, float y, float z) {
    float s = sdf_sphere(x, y, z, 20, 32, 32, 14);
    float b = sdf_box(x, y, z, 44, 32, 32, 10, 16, 10);
    return s < b ? s : b;
}

static GLuint make_compute(const char *src) {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &src, NULL); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "compute compile failed:\n%s\n", log); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, sh); glLinkProgram(p);
    glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "compute link failed:\n%s\n", log); return 0; }
    return p;
}

/* Shared GLSL helpers for coord<->index over an ivec3 `dims`. */
#define GLSL_COMMON \
    "layout(local_size_x=4,local_size_y=4,local_size_z=4) in;\n" \
    "uniform ivec3 dims;\n" \
    "int lidx(ivec3 c){ return (c.z*dims.y + c.y)*dims.x + c.x; }\n" \
    "ivec3 lcoord(int i){ int x=i%dims.x; int y=(i/dims.x)%dims.y; int z=i/(dims.x*dims.y); return ivec3(x,y,z); }\n"

/* Init: seed = own index for surface cells (solid/empty boundary), else -1. */
static const char *CS_INIT =
    "#version 430\n" GLSL_COMMON
    "layout(std430,binding=0) readonly buffer Occ { uint occ[]; };\n"
    "layout(std430,binding=1) writeonly buffer Seed { int seed[]; };\n"
    "bool solid(ivec3 c){ if(any(lessThan(c,ivec3(0)))||any(greaterThanEqual(c,dims))) return false; return occ[lidx(c)]!=0u; }\n"
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int i=lidx(c); bool s=occ[i]!=0u; bool surf=false;\n"
    "  ivec3 o[6]=ivec3[6](ivec3(1,0,0),ivec3(-1,0,0),ivec3(0,1,0),ivec3(0,-1,0),ivec3(0,0,1),ivec3(0,0,-1));\n"
    "  for(int k=0;k<6;++k){ if(solid(c+o[k])!=s){ surf=true; break; } }\n"
    "  seed[i] = surf ? i : -1; }\n";

/* One JFA pass at `step`: adopt the nearest seed among the 26 neighbours. */
static const char *CS_JFA =
    "#version 430\n" GLSL_COMMON
    "uniform int step;\n"
    "layout(std430,binding=0) readonly buffer SeedIn { int sin[]; };\n"
    "layout(std430,binding=1) writeonly buffer SeedOut { int sout[]; };\n"
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int i=lidx(c); int best=sin[i]; float bd = best>=0 ? distance(vec3(c),vec3(lcoord(best))) : 1e30;\n"
    "  for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){\n"
    "    ivec3 n=c+ivec3(dx,dy,dz)*step; if(any(lessThan(n,ivec3(0)))||any(greaterThanEqual(n,dims))) continue;\n"
    "    int s=sin[lidx(n)]; if(s<0) continue; float d=distance(vec3(c),vec3(lcoord(s)));\n"
    "    if(d<bd){ bd=d; best=s; } }\n"
    "  sout[i]=best; }\n";

/* Finalise: signed distance = |cell - seed| * voxel, negative inside solid. */
static const char *CS_FIN =
    "#version 430\n" GLSL_COMMON
    "uniform float voxel;\n"
    "layout(std430,binding=0) readonly buffer SeedIn { int sin[]; };\n"
    "layout(std430,binding=1) readonly buffer Occ { uint occ[]; };\n"
    "layout(std430,binding=2) writeonly buffer Dist { float dist[]; };\n"
    "void main(){ ivec3 c=ivec3(gl_GlobalInvocationID); if(any(greaterThanEqual(c,dims))) return;\n"
    "  int i=lidx(c); int se=sin[i]; float d = se>=0 ? distance(vec3(c),vec3(lcoord(se)))*voxel : 1e30;\n"
    "  dist[i] = occ[i]!=0u ? -d : d; }\n";

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL init failed\n"); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("jfa", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext gc = SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { fprintf(stderr, "glad failed\n"); return 1; }
    p_glDispatchCompute = (void (*)(GLuint, GLuint, GLuint))SDL_GL_GetProcAddress("glDispatchCompute");
    p_glMemoryBarrier = (void (*)(GLbitfield))SDL_GL_GetProcAddress("glMemoryBarrier");
    p_glBindBufferBase = (void (*)(GLenum, GLuint, GLuint))SDL_GL_GetProcAddress("glBindBufferBase");
    if (!p_glDispatchCompute || !p_glMemoryBarrier || !p_glBindBufferBase) {
        fprintf(stderr, "GL 4.3 compute entry points unavailable\n"); return 1; }
    printf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));

    const int N = 64;                    /* 64^3 grid, voxel = 1 (voxel units). */
    const int dims[3] = { N, N, N };
    const float voxel = 1.0f;
    const size_t cells = (size_t)N * N * N;

    uint32_t *occ = malloc(cells * sizeof(uint32_t));
    for (int z = 0; z < N; ++z) for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x)
        occ[(size_t)(z*N + y)*N + x] = (scene_sdf((float)x, (float)y, (float)z) < 0.0f) ? 1u : 0u;

    GLuint bo_occ, bo_a, bo_b, bo_dist;
    glGenBuffers(1, &bo_occ); glBindBuffer(GL_SHADER_STORAGE_BUFFER, bo_occ);
    glBufferData(GL_SHADER_STORAGE_BUFFER, cells * sizeof(uint32_t), occ, GL_STATIC_DRAW);
    glGenBuffers(1, &bo_a); glBindBuffer(GL_SHADER_STORAGE_BUFFER, bo_a);
    glBufferData(GL_SHADER_STORAGE_BUFFER, cells * sizeof(int32_t), NULL, GL_DYNAMIC_COPY);
    glGenBuffers(1, &bo_b); glBindBuffer(GL_SHADER_STORAGE_BUFFER, bo_b);
    glBufferData(GL_SHADER_STORAGE_BUFFER, cells * sizeof(int32_t), NULL, GL_DYNAMIC_COPY);
    glGenBuffers(1, &bo_dist); glBindBuffer(GL_SHADER_STORAGE_BUFFER, bo_dist);
    glBufferData(GL_SHADER_STORAGE_BUFFER, cells * sizeof(float), NULL, GL_DYNAMIC_COPY);

    GLuint p_init = make_compute(CS_INIT), p_jfa = make_compute(CS_JFA), p_fin = make_compute(CS_FIN);
    if (!p_init || !p_jfa || !p_fin) return 1;
    GLuint gx = (GLuint)((N + 3) / 4);

    /* Init seeds into bo_a. */
    glUseProgram(p_init);
    glUniform3iv(glGetUniformLocation(p_init, "dims"), 1, dims);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bo_occ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bo_a);
    glDispatchCompute(gx, gx, gx); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    /* JFA passes step = N/2 .. 1, ping-ponging a<->b. */
    GLuint src = bo_a, dst = bo_b; int passes = 0;
    glUseProgram(p_jfa);
    glUniform3iv(glGetUniformLocation(p_jfa, "dims"), 1, dims);
    for (int step = N / 2; step >= 1; step /= 2) {
        glUniform1i(glGetUniformLocation(p_jfa, "step"), step);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, dst);
        glDispatchCompute(gx, gx, gx); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        GLuint t = src; src = dst; dst = t; ++passes;
    }

    /* Finalise src -> signed distance. */
    glUseProgram(p_fin);
    glUniform3iv(glGetUniformLocation(p_fin, "dims"), 1, dims);
    glUniform1f(glGetUniformLocation(p_fin, "voxel"), voxel);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, src);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bo_occ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bo_dist);
    glDispatchCompute(gx, gx, gx); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    float *dist = malloc(cells * sizeof(float));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bo_dist);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, cells * sizeof(float), dist);

    /* Compare to analytic SDF. JFA distance is to the nearest surface VOXEL, so
     * expect ~1 voxel discretisation error; sign must match exactly. */
    double sum_err = 0.0; float max_err = 0.0f; int sign_bad = 0, checked = 0;
    for (int z = 0; z < N; ++z) for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        size_t i = (size_t)(z*N + y)*N + x;
        float ana = scene_sdf((float)x, (float)y, (float)z);
        float jfa = dist[i];
        /* Sign check away from the surface (|ana| > 1 voxel). */
        if (fabsf(ana) > 1.0f && ((ana < 0) != (jfa < 0))) sign_bad++;
        float e = fabsf(fabsf(jfa) - fabsf(ana));
        sum_err += e; if (e > max_err) max_err = e; ++checked;
    }
    double mean_err = sum_err / (double)checked;
    printf("JFA passes=%d  cells=%zu  mean_err=%.3f  max_err=%.3f  sign_mismatch=%d (voxels)\n",
           passes, cells, mean_err, max_err, sign_bad);

    int fail = (mean_err > 1.0) || (max_err > 2.5) || (sign_bad != 0);
    printf("%s\n", fail ? "FAILED" : "PASSED");

    free(occ); free(dist);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return fail ? 1 : 0;
}
