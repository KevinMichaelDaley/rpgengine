/**
 * @file lm_cornell_visual.c
 * @brief Visual regression: bake the Cornell box through the TRIANGLE-MESH baker
 *        (material albedo/emissive textures) and render its SH lightmap through
 *        the PBR shader. Should match the quad-baker Cornell -- GI + colour bleed.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_mesh_bake.h"
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

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

static void save_ppm(const char *path, int w, int h) {
    size_t row=(size_t)w*3; uint8_t *rgb=malloc(row*(size_t)h); if(!rgb)return;
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    FILE *f=fopen(path,"wb");
    if(f){fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*row,1,row,f); fclose(f);
        printf("screenshot: %s\n",path);} free(rgb);
}

typedef struct quad { float pos[12], nrm[12], uv[8]; uint32_t idx[6]; } quad_t;
static void make_quad(quad_t *q, vec3_t o, vec3_t eu, vec3_t ev, vec3_t n) {
    vec3_t c[4]={o,vec3_add(o,eu),vec3_add(vec3_add(o,eu),ev),vec3_add(o,ev)};
    float uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;i++){ q->pos[i*3]=c[i].x;q->pos[i*3+1]=c[i].y;q->pos[i*3+2]=c[i].z;
        q->nrm[i*3]=n.x;q->nrm[i*3+1]=n.y;q->nrm[i*3+2]=n.z; q->uv[i*2]=uv[i][0];q->uv[i*2+1]=uv[i][1];}
    uint32_t idx[6]={0,1,2,0,2,3}; memcpy(q->idx,idx,sizeof idx);
}
static void solid(uint8_t *b, uint8_t r, uint8_t g, uint8_t bl){ for(int i=0;i<4;i++){b[i*3]=r;b[i*3+1]=g;b[i*3+2]=bl;} }
static void set_mesh(lm_mesh_t *m,const quad_t *q,const lm_image_t *a,const lm_image_t *e,vec3_t at,vec3_t et){
    memset(m,0,sizeof *m); m->positions=q->pos;m->normals=q->nrm;m->uv0=q->uv;m->uv1=q->uv;m->indices=q->idx;
    m->vert_count=4;m->index_count=6;m->albedo_image=a;m->emissive_image=e;m->albedo=at;m->emissive=et;
    m->material=0;m->lightmap_resolution=32;
}

int main(int argc, char **argv) {
    const char *shot = argc>1?argv[1]:"/tmp/lm_cornell.ppm";

    /* --- Cornell meshes --- */
    static char buf[128*1024*1024]; arena_t arena; arena_init(&arena,buf,sizeof buf);
    const float S=5.0f; quad_t qf,qc,qb,ql,qr,qp;
    make_quad(&qf,v3(0,0,0),v3(0,0,S),v3(S,0,0),v3(0,1,0));
    make_quad(&qc,v3(0,S,0),v3(S,0,0),v3(0,0,S),v3(0,-1,0));
    make_quad(&qb,v3(0,0,0),v3(S,0,0),v3(0,S,0),v3(0,0,1));
    make_quad(&ql,v3(0,0,0),v3(0,S,0),v3(0,0,S),v3(1,0,0));
    make_quad(&qr,v3(S,0,0),v3(0,0,S),v3(0,S,0),v3(-1,0,0));
    make_quad(&qp,v3(1.7f,S-0.02f,1.7f),v3(1.6f,0,0),v3(0,0,1.6f),v3(0,-1,0));
    static uint8_t white[12],red[12],green[12],lit[12];
    solid(white,190,190,180); solid(red,200,25,20); solid(green,35,185,45); solid(lit,255,255,255);
    lm_image_t iw={white,2,2,3,true},ir={red,2,2,3,true},ig={green,2,2,3,true},il={lit,2,2,3,true};
    vec3_t one=v3(1,1,1),zero=v3(0,0,0),emit=v3(14,13,11);
    lm_mesh_t meshes[6];
    set_mesh(&meshes[0],&qf,&iw,NULL,one,zero); set_mesh(&meshes[1],&qc,&iw,NULL,one,zero);
    set_mesh(&meshes[2],&qb,&iw,NULL,one,zero); set_mesh(&meshes[3],&ql,&ir,NULL,one,zero);
    set_mesh(&meshes[4],&qr,&ig,NULL,one,zero); set_mesh(&meshes[5],&qp,NULL,&il,zero,emit);
    lm_material_t fb={{0,0,0},{0,0,0}};
    lm_mesh_scene_t scene={meshes,6,NULL,0,{NULL,0,fb}};
    lm_bake_config_t cfg={0};
    cfg.svo_bounds=(phys_aabb_t){{-0.5f,-0.5f,-0.5f},{5.5f,5.5f,5.5f}};
    cfg.svo_depth=6;cfg.atlas_width=512;cfg.atlas_padding=2;cfg.direct_samples=32;
    cfg.farfield_samples=0;cfg.solve.near_radius=10.0f;cfg.solve.max_shots=4000;
    cfg.solve.residual_epsilon=1e-3f;cfg.seed=7u;
    lm_mesh_bake_result_t res;
    if(!lm_mesh_bake(&scene,&cfg,&res,&arena)){fprintf(stderr,"bake failed\n");return 1;}
    printf("mesh-baked %u luxels into %ux%u atlas\n",res.n_luxels,res.atlas.width,res.atlas.height);

    /* --- GL --- */
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window *win=SDL_CreateWindow("lm_cornell",0,0,WIN,WIN,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    SDL_GLContext gc=SDL_GL_CreateContext(win);SDL_GL_MakeCurrent(win,gc);
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))return 1;
    gl_loader_t loader={sdl_get_proc,NULL};

    /* Upload 9 SH coefficient atlases (units 7..15). */
    uint32_t aw=res.atlas.width,ah=res.atlas.height,apx=aw*ah;
    float *img=malloc((size_t)apx*3*sizeof(float)); GLuint sh_tex[9]; glGenTextures(9,sh_tex);
    for(int c=0;c<9;c++){ lm_mesh_bake_readback_sh(&res,(uint32_t)c,img);
        glActiveTexture(GL_TEXTURE7+c); glBindTexture(GL_TEXTURE_2D,sh_tex[c]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB32F,(GLsizei)aw,(GLsizei)ah,0,GL_RGB,GL_FLOAT,img);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    free(img);

    /* Dummy buffer textures for the shader's forward+ cluster samplers (unused
     * here, but they must be complete or the draw is skipped). Units 16..19. */
    GLuint dbuf[4],dtex[4]; glGenBuffers(4,dbuf); glGenTextures(4,dtex);
    int32_t zi[4]={0,0,0,0}; float zf[4]={0,0,0,0};
    uint32_t dfmt[4]={GL_RGBA32F,GL_R32I,GL_R32I,GL_R32I};
    const void *ddata[4]={zf,zi,zi,zi};
    for(int k=0;k<4;k++){ glBindBuffer(GL_TEXTURE_BUFFER,dbuf[k]);
        glBufferData(GL_TEXTURE_BUFFER,16,ddata[k],GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE16+k); glBindTexture(GL_TEXTURE_BUFFER,dtex[k]);
        glTexBuffer(GL_TEXTURE_BUFFER,dfmt[k],dbuf[k]); }

    shader_program_t prog; char log[2048]={0};
    if(pbr_shader_create(&prog,&loader,log,sizeof log)!=SHADER_PROGRAM_OK){fprintf(stderr,"shader:%s\n",log);return 1;}
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache,&prog);

    /* Render geometry: per mesh, its 6 triangle verts (pos,nrm,tan,uv0,uv1-remapped). */
    static float vtx[6*6*14]; int vi=0;
    for(int m=0;m<6;++m){ const lm_mesh_t *me=&meshes[m]; const lm_atlas_rect_t *rc=&res.rects[m];
        for(int k=0;k<6;++k){ uint32_t id=me->indices[k]; float *p=&vtx[vi*14];
            p[0]=me->positions[id*3];p[1]=me->positions[id*3+1];p[2]=me->positions[id*3+2];
            p[3]=me->normals[id*3];p[4]=me->normals[id*3+1];p[5]=me->normals[id*3+2];
            p[6]=1;p[7]=0;p[8]=0;p[9]=1;
            p[10]=me->uv0[id*2];p[11]=me->uv0[id*2+1];
            float au,av; lm_atlas_remap_uv(rc,&res.atlas,me->uv1[id*2],me->uv1[id*2+1],&au,&av);
            p[12]=au;p[13]=av; ++vi; }
    }
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vi*14*sizeof(float)),vtx,GL_STATIC_DRAW);
    int comps[5]={3,3,4,2,2}; size_t off[5]={0,3,6,10,12};
    for(int a=0;a<5;a++){glEnableVertexAttribArray((GLuint)a);
        glVertexAttribPointer((GLuint)a,comps[a],GL_FLOAT,GL_FALSE,14*sizeof(float),(void*)(off[a]*sizeof(float)));}

    /* Per-mesh render tint (wall colour) + emissive. */
    vec3_t tints[6]={v3(0.75f,0.75f,0.72f),v3(0.75f,0.75f,0.72f),v3(0.75f,0.75f,0.72f),
                     v3(0.63f,0.06f,0.05f),v3(0.14f,0.45f,0.09f),v3(0,0,0)};
    mat4_t proj,view;
    mat4_perspective(38.0f*(float)M_PI/180.0f,1.0f,0.1f,100.0f,&proj);
    mat4_look_at(v3(2.5f,2.5f,12.8f),v3(2.5f,2.5f,2.5f),v3(0,1,0),&view);
    mat4_t model=mat4_identity();

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    static const char *shn[9]={"u_sh0","u_sh1","u_sh2","u_sh3","u_sh4","u_sh5","u_sh6","u_sh7","u_sh8"};
    for(int frame=0;frame<3;++frame){
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",view.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",proj.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_model",model.m,0);
        float eye[3]={2.5f,2.5f,12.8f},z3[3]={0,0,0};
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",eye);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_color",z3);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",eye);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",1);
        shader_uniform_set_int(&cache,&prog,"u_clustered",0);
        shader_uniform_set_int(&cache,&prog,"u_light_count",0);
        shader_uniform_set_int(&cache,&prog,"u_light_data",16);
        shader_uniform_set_int(&cache,&prog,"u_cluster_offset",17);
        shader_uniform_set_int(&cache,&prog,"u_cluster_count",18);
        shader_uniform_set_int(&cache,&prog,"u_light_index",19);
        for(int c=0;c<9;c++) shader_uniform_set_int(&cache,&prog,shn[c],7+c);
        glBindVertexArray(vao);
        for(int m=0;m<6;++m){ render_material_t mt; material_init(&mt);
            mt.tint[0]=tints[m].x;mt.tint[1]=tints[m].y;mt.tint[2]=tints[m].z;
            if(m==5){mt.emissive_color[0]=1.0f;mt.emissive_color[1]=0.95f;mt.emissive_color[2]=0.82f;mt.emissive_strength=1.2f;}
            material_bind(&mt,0u,&cache,&prog);
            glDrawArrays(GL_TRIANGLES,m*6,6);
        }
        if(frame==1) save_ppm(shot,WIN,WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc);SDL_DestroyWindow(win);SDL_Quit();
    return 0;
}
