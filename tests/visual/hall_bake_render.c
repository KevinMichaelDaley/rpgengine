/**
 * @file hall_bake_render.c
 * @brief Bake the romanesque hall's directional sun into an SH lightmap with the
 *        triangle-mesh baker, serialize + reload it, then render the hall lit by
 *        the baked static GI + forward+ coloured particle lights.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_lightmap_file.h"
#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/mesh/dmesh_loader.h"
#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/forward_plus.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 900
#define MAXM 32

static void *sdl_get_proc(const char *n, void *u){ (void)u; return SDL_GL_GetProcAddress(n); }
static vec3_t v3(float x,float y,float z){ return (vec3_t){x,y,z}; }
static float frand(uint32_t *s){ *s=*s*1664525u+1013904223u; return (float)(*s>>8)*(1.0f/16777216.0f); }

static void save_ppm(const char *p,int w,int h){ size_t row=(size_t)w*3; uint8_t *rgb=malloc(row*(size_t)h);
    if(!rgb)return; glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,rgb); FILE *f=fopen(p,"wb");
    if(f){fprintf(f,"P6\n%d %d\n255\n",w,h); for(int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*row,1,row,f); fclose(f);
        printf("screenshot: %s\n",p);} free(rgb); }

/* A loaded dual-UV mesh (de-interleaved). */
/* A dual-UV mesh: `vcount` unique verts + `icount` triangle-corner indices. */
/* Material group by mesh name: 0=brick (win/door panels), 2=vault, 1=ashlar. */
static int group_of(const char *n){ if(strstr(n,"win")||strstr(n,"door"))return 0; if(strstr(n,"vault"))return 2; return 1; }

/* Load a PNG into a CPU lm_image (keeps pixels alive for the bake). */
static uint8_t *load_cpu(const char *path, lm_image_t *img, int srgb){
    int w=0,h=0,n=0; unsigned char *px=stbi_load(path,&w,&h,&n,3); if(!px){img->pixels=NULL;return NULL;}
    img->pixels=px; img->width=(uint32_t)w; img->height=(uint32_t)h; img->channels=3; img->srgb=srgb!=0; return px;
}
static int load_gl(texture_t *t,const gl_loader_t *l,const char *p,texture_format_t f){
    int w=0,h=0,n=0; unsigned char *px=stbi_load(p,&w,&h,&n,3); if(!px)return 0;
    texture_create(t,l); texture_upload_2d(t,f,(uint32_t)w,(uint32_t)h,px,true);
    texture_set_sampler(t,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,GL_REPEAT,GL_REPEAT); stbi_image_free(px); return 1;
}

int main(int argc,char**argv){
    const char *dir=argc>1?argv[1]:"datasets/hall_lm";
    const char *bake=argc>2?argv[2]:"assets/arch/proc/prefabs/bake";
    const char *shot=argc>3?argv[3]:"/tmp/hall_baked.ppm";
    const char *lmfile="/tmp/hall.flm";

    /* --- Load dual-UV meshes (engine loader: dedupe + smooth tangents). --- */
    obj_mesh_t dm[MAXM]; int grp[MAXM]; int nm=0; char names[MAXM][128];
    DIR *d=opendir(dir); struct dirent *e;
    while(d&&(e=readdir(d))&&nm<MAXM){ if(!strstr(e->d_name,".dmesh"))continue;
        char p[512]; snprintf(p,sizeof p,"%s/%s",dir,e->d_name);
        if(dmesh_load(p,&dm[nm])==0){ grp[nm]=group_of(e->d_name); snprintf(names[nm],128,"%s",e->d_name); ++nm; } }
    if(d)closedir(d);
    printf("loaded %d dual-uv meshes\n",nm);

    /* --- CPU material albedo images for the bake (brick/stone/vault). --- */
    char pab[512],pas[512],pav[512];
    snprintf(pab,sizeof pab,"%s/albedo.png",bake); snprintf(pas,sizeof pas,"%s/ashlar_albedo.png",bake);
    snprintf(pav,sizeof pav,"%s/vault_albedo.png",bake);
    lm_image_t im_b,im_s,im_v; load_cpu(pab,&im_b,1); load_cpu(pas,&im_s,1); load_cpu(pav,&im_v,1);
    const lm_image_t *grp_img[3]={&im_b,&im_s,&im_v};

    /* --- Build lm_mesh array + bake the directional sun. --- */
    static char abuf[512*1024*1024]; arena_t arena; arena_init(&arena,abuf,sizeof abuf);
    lm_mesh_t lms[MAXM];
    for(int i=0;i<nm;++i){ memset(&lms[i],0,sizeof(lm_mesh_t));
        lms[i].positions=dm[i].positions; lms[i].normals=dm[i].normals; lms[i].uv0=dm[i].uvs; lms[i].uv1=dm[i].uvs1;
        lms[i].indices=dm[i].indices; lms[i].vert_count=dm[i].vert_count; lms[i].index_count=dm[i].index_count;
        lms[i].albedo_image=grp_img[grp[i]]; lms[i].emissive_image=NULL;
        lms[i].albedo=v3(1,1,1); lms[i].emissive=v3(0,0,0);
        lms[i].material=0;
        /* Columns are smooth + prominent -> much higher lightmap res; everything
         * else a small bump. */
        lms[i].lightmap_resolution =
            strstr(names[i],"col")   ? 224u :
            strstr(names[i],"resp")  ? 128u :
            strstr(names[i],"floor") ? 160u :
            (grp[i]==0 ? 80u : 72u); }
    /* Low sun angled to enter through the long-wall windows (else the vaulted
     * ceiling blocks all interior light). */
    lm_light_t sun; memset(&sun,0,sizeof sun); sun.kind=LM_LIGHT_DIRECTIONAL;
    sun.direction=v3(0.15f,-0.42f,0.90f); sun.color=v3(3.6f,3.4f,3.0f);
    lm_material_t fb={{0,0,0},{0,0,0}};
    lm_mesh_scene_t scene={lms,(uint32_t)nm,&sun,1,{NULL,0,fb}};
    /* Room AABB -> diagonal; near/far radiosity transition at half the diagonal
     * (this test exercises the SVO far-field path for the distant half). */
    float bmin[3]={1e30f,1e30f,1e30f},bmax[3]={-1e30f,-1e30f,-1e30f};
    for(int i=0;i<nm;++i) for(uint32_t v=0;v<dm[i].vert_count;++v) for(int c=0;c<3;++c){
        float p=dm[i].positions[v*3+c]; if(p<bmin[c])bmin[c]=p; if(p>bmax[c])bmax[c]=p; }
    float diag=sqrtf((bmax[0]-bmin[0])*(bmax[0]-bmin[0])+(bmax[1]-bmin[1])*(bmax[1]-bmin[1])+(bmax[2]-bmin[2])*(bmax[2]-bmin[2]));
    float half_diag=0.5f*diag;
    printf("room diag=%.2f m, near/far transition=%.2f m\n",diag,half_diag);
    lm_bake_config_t cfg={0};
    cfg.svo_bounds=(phys_aabb_t){{-1.5f,-0.5f,-7.5f},{10.5f,5.5f,1.5f}};
    /* Voxel-GI: fine SVO (target voxel edge), path-traced gather. Start coarse
     * (3cm) + few samples to validate the pipeline fast; drop to ~1cm for the
     * final bake. HALL_VOXEL / HALL_SAMPLES / HALL_BOUNCES override. */
    cfg.voxel_size = getenv("HALL_VOXEL")?(float)atof(getenv("HALL_VOXEL")):0.02f;
    cfg.atlas_width=4096; cfg.atlas_padding=2; cfg.direct_samples=0;
    cfg.farfield_samples = getenv("HALL_SAMPLES")?(uint32_t)atoi(getenv("HALL_SAMPLES")):256u;
    cfg.gi_bounces = getenv("HALL_BOUNCES")?(uint32_t)atoi(getenv("HALL_BOUNCES")):2u;
    cfg.farfield_near=half_diag; cfg.farfield_maxdist=1e9f; cfg.seed=11u;
    /* Constant daylight sky sampled by escaping gather rays. */
    cfg.sky.kind=LM_SKY_CONSTANT; cfg.sky.color=v3(0.55f,0.68f,0.95f); cfg.sky.hdri=NULL; cfg.sky.yaw=0.0f;
    lm_mesh_bake_result_t res;
    /* SKIP_BAKE=1 reuses the previously serialized lightmap so debug renders can
     * iterate without re-baking (the bake is minutes; the render is seconds). */
    int skip_bake = getenv("SKIP_BAKE") && atoi(getenv("SKIP_BAKE"));
    if(!skip_bake){
        printf("baking hall (%d meshes)...\n",nm);
        if(!lm_mesh_bake(&scene,&cfg,&res,&arena)){fprintf(stderr,"bake failed\n");return 1;}
        printf("baked %u luxels into %ux%u atlas\n",res.n_luxels,res.atlas.width,res.atlas.height);
        if(!lm_lightmap_save(&res,lmfile)){fprintf(stderr,"save failed\n");return 1;}
    } else { printf("SKIP_BAKE: reusing %s\n",lmfile); }

    /* --- Load the serialized lightmap. --- */
    lm_lightmap_data_t lm; if(!lm_lightmap_load(lmfile,&lm)){fprintf(stderr,"load failed\n");return 1;}
    printf("serialized + reloaded lightmap (%ux%u, %u meshes)\n",lm.atlas_w,lm.atlas_h,lm.n_meshes);

    /* --- GL --- */
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window *win=SDL_CreateWindow("hall_baked",0,0,WIN,WIN,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    SDL_GLContext gc=SDL_GL_CreateContext(win);SDL_GL_MakeCurrent(win,gc);
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))return 1;
    gl_loader_t loader={sdl_get_proc,NULL};

    /* Upload 9 SH atlases from the LOADED lightmap (units 7..15). */
    GLuint sh_tex[9]; glGenTextures(9,sh_tex);
    for(int c=0;c<9;c++){ glActiveTexture(GL_TEXTURE7+c); glBindTexture(GL_TEXTURE_2D,sh_tex[c]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB32F,(GLsizei)lm.atlas_w,(GLsizei)lm.atlas_h,0,GL_RGB,GL_FLOAT,lm.coeffs[c]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}

    /* GL material textures for rendering. */
    texture_t tb_a,tb_n,tb_o,tb_r,ts_a,ts_r,tv_a,tv_r; char q[512];
    load_gl(&tb_a,&loader,pab,TEXTURE_FORMAT_SRGB8);
    snprintf(q,sizeof q,"%s/normal.png",bake); load_gl(&tb_n,&loader,q,TEXTURE_FORMAT_RGB8);
    snprintf(q,sizeof q,"%s/ao.png",bake); load_gl(&tb_o,&loader,q,TEXTURE_FORMAT_RGB8);
    snprintf(q,sizeof q,"%s/roughness.png",bake); load_gl(&tb_r,&loader,q,TEXTURE_FORMAT_RGB8);
    load_gl(&ts_a,&loader,pas,TEXTURE_FORMAT_SRGB8); snprintf(q,sizeof q,"%s/ashlar_roughness.png",bake); load_gl(&ts_r,&loader,q,TEXTURE_FORMAT_RGB8);
    load_gl(&tv_a,&loader,pav,TEXTURE_FORMAT_SRGB8); snprintf(q,sizeof q,"%s/vault_roughness.png",bake); load_gl(&tv_r,&loader,q,TEXTURE_FORMAT_RGB8);
    render_material_t mats[3];
    material_init(&mats[0]); mats[0].maps[MATERIAL_TEX_ALBEDO]=&tb_a; mats[0].maps[MATERIAL_TEX_NORMAL]=&tb_n;
    mats[0].maps[MATERIAL_TEX_AO]=&tb_o; mats[0].maps[MATERIAL_TEX_ROUGHNESS]=&tb_r; mats[0].normal_scale=1.2f;
    mats[0].roughness_min=0.25f; mats[0].roughness_max=1.0f;
    material_init(&mats[1]); mats[1].maps[MATERIAL_TEX_ALBEDO]=&ts_a; mats[1].maps[MATERIAL_TEX_ROUGHNESS]=&ts_r;
    mats[1].roughness_min=0.2f; mats[1].roughness_max=1.0f;
    material_init(&mats[2]); mats[2].maps[MATERIAL_TEX_ALBEDO]=&tv_a; mats[2].maps[MATERIAL_TEX_ROUGHNESS]=&tv_r;
    mats[2].roughness_min=0.2f; mats[2].roughness_max=1.0f;
    /* Flat tangent-space normal (128,128,255) so the ashlar + vault surfaces run
     * through the SAME TBN path as the bricks (rules out the geometric-N path). */
    texture_t tflat; { unsigned char fn[2*2*3]; for(int i=0;i<4;++i){fn[i*3]=128;fn[i*3+1]=128;fn[i*3+2]=255;}
        texture_create(&tflat,&loader); texture_upload_2d(&tflat,TEXTURE_FORMAT_RGB8,2,2,fn,true);
        texture_set_sampler(&tflat,GL_LINEAR,GL_LINEAR,GL_REPEAT,GL_REPEAT); }
    mats[1].maps[MATERIAL_TEX_NORMAL]=&tflat; mats[1].normal_scale=1.0f;
    mats[2].maps[MATERIAL_TEX_NORMAL]=&tflat; mats[2].normal_scale=1.0f;

    /* Build render geometry (pos,nrm,tan,uv0,uv1-remapped) + per-mesh ranges. */
    uint32_t tot=0; for(int i=0;i<nm;++i) tot+=dm[i].index_count;
    float *vtx=malloc((size_t)tot*14*sizeof(float)); uint32_t moff[MAXM]; int vi=0;
    for(int i=0;i<nm;++i){ moff[i]=(uint32_t)vi; const lm_atlas_rect_t *rc=&lm.rects[i];
        /* Expand the indexed mesh to non-indexed corners for glDrawArrays, using
         * the engine loader's smooth per-vertex tangents. */
        for(uint32_t t=0;t+2<dm[i].index_count;t+=3){
            for(int k=0;k<3;++k){ uint32_t id=dm[i].indices[t+k]; float *p=&vtx[vi*14];
                p[0]=dm[i].positions[id*3];p[1]=dm[i].positions[id*3+1];p[2]=dm[i].positions[id*3+2];
                p[3]=dm[i].normals[id*3];p[4]=dm[i].normals[id*3+1];p[5]=dm[i].normals[id*3+2];
                p[6]=dm[i].tangents[id*4];p[7]=dm[i].tangents[id*4+1];p[8]=dm[i].tangents[id*4+2];p[9]=dm[i].tangents[id*4+3];
                p[10]=dm[i].uvs[id*2];p[11]=dm[i].uvs[id*2+1];
                float au,av; lm_atlas_remap_uv(rc,&(lm_atlas_t){lm.atlas_w,lm.atlas_h},dm[i].uvs1[id*2],dm[i].uvs1[id*2+1],&au,&av);
                p[12]=au;p[13]=av; ++vi; } } }
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)vi*14*sizeof(float)),vtx,GL_STATIC_DRAW);
    int comps[5]={3,3,4,2,2}; size_t off[5]={0,3,6,10,12};
    for(int a=0;a<5;a++){glEnableVertexAttribArray((GLuint)a);
        glVertexAttribPointer((GLuint)a,comps[a],GL_FLOAT,GL_FALSE,14*sizeof(float),(void*)(off[a]*sizeof(float)));}

    /* Scene AABB (from geometry) for camera + particle lights. */
    float amin[3]={1e30f,1e30f,1e30f},amax[3]={-1e30f,-1e30f,-1e30f};
    for(int i=0;i<nm;++i) for(uint32_t v=0;v<dm[i].vert_count;++v) for(int c=0;c<3;++c){
        float p=dm[i].positions[v*3+c]; if(p<amin[c])amin[c]=p; if(p>amax[c])amax[c]=p; }
    float span[3]={amax[0]-amin[0],amax[1]-amin[1],amax[2]-amin[2]};
    int lenax=(span[0]>span[2])?0:2;

    /* Coloured particle lights. */
    render_light_t lights[64]; int nl=0; float pal[6][3]={{1,0.3f,0.3f},{0.3f,1,0.4f},{0.35f,0.5f,1},{1,0.85f,0.3f},{1,0.4f,0.9f},{0.3f,1,1}};
    uint32_t rng=99;
    for(int i=0;i<48&&nl<64;++i){ render_light_t l; memset(&l,0,sizeof l); l.kind=RENDER_LIGHT_POINT;
        l.position[0]=amin[0]+frand(&rng)*span[0]; l.position[1]=amin[1]+0.15f*span[1]+frand(&rng)*0.6f*span[1];
        l.position[2]=amin[2]+frand(&rng)*span[2]; const float *pc=pal[i%6];
        l.color[0]=pc[0];l.color[1]=pc[1];l.color[2]=pc[2]; l.intensity=0.0f;l.range=2.0f;l.flags=RENDER_LIGHT_FLAG_REALTIME; lights[nl++]=l; }

    /* Forward+ cluster grid + upload. */
    cluster_config_t ccfg={16,16,24,0.2f,60.0f}; uint32_t ctot=ccfg.tiles_x*ccfg.tiles_y*ccfg.slices,icap=ctot*16;
    uint32_t *coff=malloc(ctot*4),*ccnt=malloc(ctot*4),*cidx=malloc(icap*4);
    cluster_grid_t grid; cluster_grid_init(&grid,ccfg,coff,ccnt,cidx,icap);
    /* camera */
    mat4_t proj,view; float eye[3]={0,0,0},tgt[3]={0,0,0},up[3]={0,1,0};
    eye[lenax]=amin[lenax]+span[lenax]*0.08f; tgt[lenax]=amax[lenax]-span[lenax]*0.08f;
    eye[1]=amin[1]+span[1]*0.35f; tgt[1]=amin[1]+span[1]*0.35f;
    eye[(lenax==0)?2:0]=(amin[(lenax==0)?2:0]+amax[(lenax==0)?2:0])*0.5f; tgt[(lenax==0)?2:0]=eye[(lenax==0)?2:0];
    mat4_perspective(60.0f*(float)M_PI/180.0f,1.0f,0.2f,80.0f,&proj);
    mat4_look_at(v3(eye[0],eye[1],eye[2]),v3(tgt[0],tgt[1],tgt[2]),v3(0,1,0),&view);
    render_camera_t cam; memcpy(cam.view,view.m,64); memcpy(cam.proj,proj.m,64); cam.eye[0]=eye[0];cam.eye[1]=eye[1];cam.eye[2]=eye[2];
    cluster_grid_build(&grid,&cam,lights,(uint32_t)nl);
    float *ldata=malloc((size_t)nl*16*sizeof(float)); for(int i=0;i<nl;++i) forward_plus_pack_light(&lights[i],&ldata[i*16]);
    forward_plus_t fp; forward_plus_init(&fp,&loader); forward_plus_upload(&fp,&grid,ldata,(uint32_t)nl);

    shader_program_t prog; char slog[2048]={0};
    if(pbr_shader_create(&prog,&loader,slog,sizeof slog)!=SHADER_PROGRAM_OK){fprintf(stderr,"shader:%s\n",slog);return 1;}
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache,&prog);
    static const char *shn[9]={"u_sh0","u_sh1","u_sh2","u_sh3","u_sh4","u_sh5","u_sh6","u_sh7","u_sh8"};

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    float model[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(int frame=0;frame<3;++frame){
        glClearColor(0.02f,0.02f,0.03f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",cam.view,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",cam.proj,0);
        shader_uniform_set_mat4(&cache,&prog,"u_model",model,0);
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",cam.eye);
        float z3[3]={0,0,0}; shader_uniform_set_vec3(&cache,&prog,"u_sun_color",z3);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",cam.eye);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",1);
        { const char *dv=getenv("HALL_DEBUG"); int dbg=dv?atoi(dv):0;
          shader_uniform_set_int(&cache,&prog,"u_debug_mode",dbg); }
        for(int c=0;c<9;c++) shader_uniform_set_int(&cache,&prog,shn[c],7+c);
        forward_plus_bind(&fp,&cache,&prog,&ccfg,(float)WIN,(float)WIN);
        glBindVertexArray(vao);
        int nonrm=getenv("HALL_NONORMAL")&&atoi(getenv("HALL_NONORMAL"));
        for(int i=0;i<nm;++i){ material_bind(&mats[grp[i]],0u,&cache,&prog);
            if(nonrm) shader_uniform_set_int(&cache,&prog,"u_has_normal",0);
            glDrawArrays(GL_TRIANGLES,(GLint)moff[i],(GLsizei)dm[i].index_count); }
        if(frame==1) save_ppm(shot,WIN,WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    shader_program_destroy(&prog); forward_plus_destroy(&fp);
    lm_lightmap_data_free(&lm);
    SDL_GL_DeleteContext(gc);SDL_DestroyWindow(win);SDL_Quit();
    return 0;
}
