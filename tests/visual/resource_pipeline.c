/**
 * @file resource_pipeline.c
 * @brief End-to-end visual test of the fiber-based resource paradigm:
 *        job-system loader FIBER decodes an image -> GPU command queue ->
 *        render-thread executor creates the GL texture -> drawn on a quad.
 *
 * Proves the whole chain with no GL touched off the main thread. PPM screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/resource/gpu_cmd_queue.h"
#include "ferrum/renderer/resource/gpu_executor.h"
#include "ferrum/renderer/resource/gpu_registry.h"
#include "ferrum/renderer/resource/resource_loader.h"

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

static GLuint make_quad_program(void) {
    static const char *VS =
        "#version 330 core\n"
        "out vec2 uv;\n"
        "void main(){ vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));\n"
        "  uv=(p[gl_VertexID]*0.5+0.5); uv.y=1.0-uv.y; gl_Position=vec4(p[gl_VertexID],0,1); }\n";
    static const char *FS =
        "#version 330 core\n"
        "in vec2 uv; out vec4 frag; uniform sampler2D u_tex;\n"
        "void main(){ frag=vec4(texture(u_tex,uv).rgb,1.0); }\n";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER); glShaderSource(vs,1,&VS,NULL); glCompileShader(vs);
    GLuint fs=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(fs,1,&FS,NULL); glCompileShader(fs);
    GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs); return p;
}

static void save_ppm(const char *path,int w,int h){
    uint8_t *rgb=malloc((size_t)w*h*3); if(!rgb)return;
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    FILE *f=fopen(path,"wb");
    if(f){ fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*w*3,1,(size_t)w*3,f); fclose(f);
        printf("screenshot: %s\n",path);} free(rgb);
}

int main(int argc,char **argv){
    const char *tex_path = argc>1?argv[1]:"assets/arch/proc/prefabs/bake/ashlar_albedo.png";
    const char *shot = argc>2?argv[2]:"/tmp/resource_pipeline.ppm";
    if(SDL_Init(SDL_INIT_VIDEO)!=0){ fprintf(stderr,"SDL init failed\n"); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win=SDL_CreateWindow("resource pipeline",0,0,900,900,SDL_WINDOW_OPENGL);
    SDL_GLContext gc=SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win,gc);
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)){ fprintf(stderr,"glad failed\n"); return 1; }
    printf("GL_RENDERER: %s\n",(const char*)glGetString(GL_RENDERER));
    gl_loader_t loader={sdl_get_proc,NULL};

    /* --- Resource subsystem --- */
    job_system_t sys;
    if(job_system_create(&sys,2,64,64*1024,2048,0)!=JOB_CREATE_OK){ fprintf(stderr,"job create failed\n"); return 1; }
    job_system_start(&sys);

    static uint8_t arena_buf[48u*1024u*1024u]; /* room for a few big decodes. */
    arena_t arena; arena_init(&arena,arena_buf,sizeof arena_buf);

    gpu_registry_t reg;
    if(gpu_registry_init(&reg,64)!=0){ fprintf(stderr,"registry init failed\n"); return 1; }
    gpu_cmd_t slots[64]; atomic_int states[64];
    gpu_cmd_queue_t queue; gpu_cmd_queue_init(&queue,slots,states,64);
    gpu_executor_t exec;
    if(!gpu_executor_init(&exec,&loader,&reg)){ fprintf(stderr,"executor init failed\n"); return 1; }
    resource_loader_t rl; resource_loader_init(&rl,&sys,&queue,&reg,&arena);

    /* --- Async load on a fiber, wait, then execute GL on the main thread. --- */
    job_counter_t counter; job_counter_init(&counter,0);
    uint64_t htex = resource_loader_load_texture(&rl,tex_path,&counter);
    if(htex==GPU_HANDLE_INVALID){ fprintf(stderr,"load dispatch failed\n"); return 1; }
    printf("dispatched load (handle=%llu); waiting on fiber...\n",(unsigned long long)htex);
    job_wait_counter(&counter,0);              /* fiber decoded + enqueued. */
    uint32_t executed=gpu_executor_drain(&exec,&queue); /* GL create on main thread. */
    printf("executor ran %u command(s)\n",executed);

    gpu_resource_t *r=gpu_registry_get(&reg,htex);
    if(r==NULL||!atomic_load(&r->ready)||r->gl_name==0){ fprintf(stderr,"texture not ready\n"); return 1; }
    printf("texture ready: gl=%u %ux%u\n",r->gl_name,r->width,r->height);

    /* --- Draw it on a fullscreen quad --- */
    GLuint prog=make_quad_program();
    GLuint vao; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    int W,H; SDL_GL_GetDrawableSize(win,&W,&H); glViewport(0,0,W,H);
    for(int frame=0;frame<3;++frame){
        glClearColor(0.1f,0.0f,0.1f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,r->gl_name);
        glUniform1i(glGetUniformLocation(prog,"u_tex"),0);
        glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES,0,3);
        if(frame==1) save_ppm(shot,W,H);
        SDL_GL_SwapWindow(win); SDL_Delay(40);
    }

    gpu_executor_destroy(&exec); gpu_registry_destroy(&reg);
    job_system_shutdown(&sys);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
