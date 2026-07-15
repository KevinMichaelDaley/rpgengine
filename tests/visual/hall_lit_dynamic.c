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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/lightmap/lm_atlas.h"
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
#include "ferrum/renderer/texture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 900
#define MAXM 64
#define MAX_LIGHTS 96

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static float frand(uint32_t *s){ *s=*s*1664525u+1013904223u; return (float)(*s>>8)*(1.0f/16777216.0f); }
static int group_of(const char *n){ if(strstr(n,"win")||strstr(n,"door"))return 0; if(strstr(n,"vault"))return 2; return 1; }

static void save_ppm(const char *path,int w,int h){
    size_t row=(size_t)w*3; uint8_t *rgb=malloc(row*(size_t)h); if(!rgb)return;
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    FILE *f=fopen(path,"wb");
    if(f){ fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*row,1,row,f); fclose(f);
        printf("screenshot: %s\n",path);} free(rgb);
}
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
static int load_tex(texture_t *t,const gl_loader_t *l,const char *p,texture_format_t f){
    int w=0,h=0,n=0; unsigned char *px=stbi_load(p,&w,&h,&n,3); if(!px)return 0;
    texture_create(t,l); texture_upload_2d(t,f,(uint32_t)w,(uint32_t)h,px,true);
    texture_set_sampler(t,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,GL_REPEAT,GL_REPEAT);
    /* Anisotropic filtering: sharpen grazing-angle surfaces (floor/walls) that
     * trilinear mipmapping over-blurs. Clamp to the driver's max (>=16 typical). */
    GLfloat maxa=1.0f; glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY,&maxa);
    if(maxa>16.0f) maxa=16.0f;
    glBindTexture(GL_TEXTURE_2D,texture_handle(t));
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY,maxa);
    stbi_image_free(px); return 1;
}

int main(int argc,char **argv){
    const char *dir = argc>1?argv[1]:"datasets/hall_lm";
    const char *bake = argc>2?argv[2]:"assets/arch/proc/prefabs/bake";
    const char *lmfile = argc>3?argv[3]:"/tmp/hall_prod.flm";
    const char *shot = argc>4?argv[4]:"/tmp/hall_lit_dynamic.ppm";

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
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    int W,H; SDL_GL_GetDrawableSize(win,&W,&H); /* actual pixels (HiDPI-safe) */
    glEnable(GL_MULTISAMPLE);
    printf("fullscreen %dx%d (msaa 8x)\n",W,H);
    gl_loader_t loader={sdl_get_proc,NULL};

    /* --- Load dual-UV dmeshes (readdir order == the bake's mesh order). --- */
    obj_mesh_t dm[MAXM]; int grp[MAXM]; int nm=0;
    DIR *d=opendir(dir); struct dirent *e;
    while(d&&(e=readdir(d))&&nm<MAXM){ if(!strstr(e->d_name,".dmesh"))continue;
        char p[512]; snprintf(p,sizeof p,"%s/%s",dir,e->d_name);
        if(dmesh_load(p,&dm[nm])==0){ grp[nm]=group_of(e->d_name); ++nm; } }
    if(d)closedir(d);
    printf("loaded %d dmeshes\n",nm);

    /* --- Load the serialized SH lightmap (FLM1: magic, atlas_w/h, n_coeffs=9,
     * n_meshes; 9*w*h*3 floats; n_meshes lm_atlas_rect_t) + upload 9 coeff
     * atlases (units 7..15). Parsed inline to avoid the bake pipeline. --- */
    FILE *lf=fopen(lmfile,"rb");
    if(!lf){ fprintf(stderr,"lightmap open failed: %s\n",lmfile); return 1; }
    char magic[4]; uint32_t atlas_w,atlas_h,ncoef,n_meshes;
    if(fread(magic,1,4,lf)!=4||memcmp(magic,"FLM1",4)!=0){ fprintf(stderr,"bad flm\n"); return 1; }
    if(fread(&atlas_w,4,1,lf)!=1||fread(&atlas_h,4,1,lf)!=1||fread(&ncoef,4,1,lf)!=1||fread(&n_meshes,4,1,lf)!=1){ return 1; }
    printf("lightmap %ux%u, %u meshes\n",atlas_w,atlas_h,n_meshes);
    size_t npix=(size_t)atlas_w*atlas_h*3;
    float *coeffs[9]; for(int c=0;c<9;c++){ coeffs[c]=malloc(npix*sizeof(float)); if(fread(coeffs[c],sizeof(float),npix,lf)!=npix){ return 1; } }
    lm_atlas_rect_t *rects=malloc((size_t)n_meshes*sizeof(lm_atlas_rect_t));
    if(fread(rects,sizeof(lm_atlas_rect_t),n_meshes,lf)!=n_meshes){ return 1; }
    fclose(lf);
    GLuint sh_tex[9]; glGenTextures(9,sh_tex);
    for(int c=0;c<9;c++){ glActiveTexture(GL_TEXTURE7+c); glBindTexture(GL_TEXTURE_2D,sh_tex[c]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB32F,(GLsizei)atlas_w,(GLsizei)atlas_h,0,GL_RGB,GL_FLOAT,coeffs[c]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE); }

    /* --- Build static meshes: uv1 remapped into each mesh's atlas rect. --- */
    static_mesh_t meshes[MAXM]; render_submesh_t subs[MAXM];
    float amin[3]={1e30f,1e30f,1e30f},amax[3]={-1e30f,-1e30f,-1e30f};
    lm_atlas_t atlas={atlas_w,atlas_h};
    for(int i=0;i<nm;++i){
        for(uint32_t v=0;v<dm[i].vert_count;++v) for(int c=0;c<3;++c){
            float q=dm[i].positions[v*3+c]; if(q<amin[c])amin[c]=q; if(q>amax[c])amax[c]=q; }
        float *uv1=malloc((size_t)dm[i].vert_count*2*sizeof(float));
        const lm_atlas_rect_t *rc = (i<(int)n_meshes)?&rects[i]:NULL;
        for(uint32_t v=0;v<dm[i].vert_count;++v){
            float au=dm[i].uvs1[v*2], av=dm[i].uvs1[v*2+1];
            if(rc) lm_atlas_remap_uv(rc,&atlas,dm[i].uvs1[v*2],dm[i].uvs1[v*2+1],&au,&av);
            uv1[v*2]=au; uv1[v*2+1]=av;
        }
        subs[i]=(render_submesh_t){0,dm[i].index_count,0};
        static_mesh_create_info_t info; memset(&info,0,sizeof info);
        info.positions=dm[i].positions; info.normals=dm[i].normals; info.tangents=dm[i].tangents;
        info.uv0=dm[i].uvs; info.uv1=uv1;
        info.indices=dm[i].indices; info.vertex_count=dm[i].vert_count; info.index_count=dm[i].index_count;
        info.submeshes=&subs[i]; info.submesh_count=1;
        static_mesh_create(&loader,&info,&meshes[i]);
        free(uv1);
    }
    float span[3]={amax[0]-amin[0],amax[1]-amin[1],amax[2]-amin[2]};
    float cx=(amin[0]+amax[0])*0.5f,cy=(amin[1]+amax[1])*0.5f,cz=(amin[2]+amax[2])*0.5f;

    /* --- Materials. --- */
    char q[512]; texture_t tb_a,tb_n,tb_o,tb_r,ts_a,ts_r,tv_a,tv_r;
    snprintf(q,sizeof q,"%s/albedo.png",bake);    load_tex(&tb_a,&loader,q,TEXTURE_FORMAT_SRGB8);
    snprintf(q,sizeof q,"%s/normal.png",bake);    load_tex(&tb_n,&loader,q,TEXTURE_FORMAT_RGB8);
    snprintf(q,sizeof q,"%s/ao.png",bake);        load_tex(&tb_o,&loader,q,TEXTURE_FORMAT_RGB8);
    snprintf(q,sizeof q,"%s/roughness.png",bake); load_tex(&tb_r,&loader,q,TEXTURE_FORMAT_RGB8);
    snprintf(q,sizeof q,"%s/ashlar_albedo.png",bake);    load_tex(&ts_a,&loader,q,TEXTURE_FORMAT_SRGB8);
    snprintf(q,sizeof q,"%s/ashlar_roughness.png",bake); load_tex(&ts_r,&loader,q,TEXTURE_FORMAT_RGB8);
    snprintf(q,sizeof q,"%s/vault_albedo.png",bake);     load_tex(&tv_a,&loader,q,TEXTURE_FORMAT_SRGB8);
    snprintf(q,sizeof q,"%s/vault_roughness.png",bake);  load_tex(&tv_r,&loader,q,TEXTURE_FORMAT_RGB8);
    render_material_t mats[3];
    material_init(&mats[0]); mats[0].maps[MATERIAL_TEX_ALBEDO]=&tb_a; mats[0].maps[MATERIAL_TEX_NORMAL]=&tb_n;
    mats[0].maps[MATERIAL_TEX_AO]=&tb_o; mats[0].maps[MATERIAL_TEX_ROUGHNESS]=&tb_r; mats[0].normal_scale=1.3f;
    mats[0].roughness_min=0.25f; mats[0].roughness_max=1.0f;
    material_init(&mats[1]); mats[1].maps[MATERIAL_TEX_ALBEDO]=&ts_a; mats[1].maps[MATERIAL_TEX_ROUGHNESS]=&ts_r;
    mats[1].roughness_min=0.2f; mats[1].roughness_max=1.0f;
    material_init(&mats[2]); mats[2].maps[MATERIAL_TEX_ALBEDO]=&tv_a; mats[2].maps[MATERIAL_TEX_ROUGHNESS]=&tv_r;
    mats[2].roughness_min=0.2f; mats[2].roughness_max=1.0f;

    /* --- Dynamic clustered point lights ("magic particles"). --- */
    render_light_t lb[MAX_LIGHTS];
    render_light_store_t lights; render_light_store_init(&lights,lb,MAX_LIGHTS);
    float pal[6][3]={{1,0.3f,0.3f},{0.3f,1,0.4f},{0.35f,0.5f,1},{1,0.85f,0.3f},{1,0.4f,0.9f},{0.3f,1,1}};
    int lax=(span[0]>span[2])?0:2; int cax=(lax==0)?2:0;
    float hall_len=(span[0]>span[2])?span[0]:span[2];
    int shadow_only = getenv("SHADOW_ONLY") && atoi(getenv("SHADOW_ONLY"));
    int one_light = getenv("HALL_ONE") && atoi(getenv("HALL_ONE")); /* 1 light + lightmap */
    /* Light index 0: a bright SHADOW-CASTING point light. Placed near the camera
     * end and off to one side at mid height so the central column rakes a long
     * shadow across the floor and far columns. */
    { render_light_t s; memset(&s,0,sizeof s); s.kind=RENDER_LIGHT_POINT;
      s.position[0]=cx; s.position[1]=amin[1]+0.45f*span[1]; s.position[2]=cz;
      s.position[lax]=amin[lax]+0.16f*span[lax];
      s.position[cax]=amin[cax]+0.80f*span[cax];
      s.color[0]=s.color[1]=s.color[2]=1.0f; s.intensity=shadow_only?14.0f:8.0f; s.range=hall_len*1.6f;
      s.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&s); }
    uint32_t rng=4242;
    for(int i=0;i<((shadow_only||one_light)?0:64);++i){ render_light_t l; memset(&l,0,sizeof l); l.kind=RENDER_LIGHT_POINT;
        l.position[0]=amin[0]+frand(&rng)*span[0]; l.position[1]=amin[1]+0.12f*span[1]+frand(&rng)*0.7f*span[1];
        l.position[2]=amin[2]+frand(&rng)*span[2]; const float *pc=pal[i%6];
        l.color[0]=pc[0]; l.color[1]=pc[1]; l.color[2]=pc[2];
        l.intensity=6.0f; l.range=2.2f; l.flags=RENDER_LIGHT_FLAG_REALTIME; render_light_add(&lights,&l); }
    printf("point lights: %u\n",lights.count);

    /* --- Camera + scene. --- */
    int lenax=(span[0]>span[2])?0:2;
    render_camera_t cam; float eye[3]={cx,cy,cz},tgt[3]={cx,cy,cz},up[3]={0,1,0};
    eye[lenax]=amin[lenax]+span[lenax]*0.08f; tgt[lenax]=amax[lenax]-span[lenax]*0.08f;
    eye[1]=amin[1]+span[1]*0.35f; tgt[1]=amin[1]+span[1]*0.35f;
    render_camera_look_at(&cam,eye,tgt,up,60.0f*(float)M_PI/180.0f,(float)W/(float)H,0.2f,60.0f);
    render_renderable_t rb[MAXM]; render_scene_t scene; render_scene_init(&scene,rb,MAXM);
    float model[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(int i=0;i<nm;++i) render_scene_add(&scene,&meshes[i],&mats[grp[i]],model);
    scene.camera=cam; scene.lights=&lights;

    /* --- Driver: forward+ with the baked SH lightmap enabled. --- */
    render_forward_config_t fcfg; memset(&fcfg,0,sizeof fcfg);
    fcfg.loader=&loader; fcfg.cluster=(cluster_config_t){16,16,24,0.2f,60.0f};
    fcfg.max_lights=MAX_LIGHTS; fcfg.index_capacity=16u*16u*24u*16u;
    fcfg.screen_w=(float)W; fcfg.screen_h=(float)H;
    fcfg.sun_dir[0]=0.15f; fcfg.sun_dir[1]=0.42f; fcfg.sun_dir[2]=-0.90f;
    fcfg.sun_color[0]=fcfg.sun_color[1]=fcfg.sun_color[2]=0.0f; /* sun already baked into SH */
    fcfg.ambient[0]=fcfg.ambient[1]=fcfg.ambient[2]=0.0f;
    fcfg.sh_enabled=shadow_only?0:1; fcfg.sh_scale=0.4f; for(int c=0;c<9;c++) fcfg.sh_tex[c]=sh_tex[c];
    fcfg.shadow_light=0; fcfg.shadow_res=1024; fcfg.shadow_near=0.1f;
    fcfg.shadow_far=hall_len*1.8f; fcfg.shadow_bias=0.08f;
    render_forward_t fwd;
    if(!render_forward_init(&fwd,&fcfg)){ fprintf(stderr,"render_forward_init failed\n"); return 1; }

    glViewport(0,0,W,H);
    for(int frame=0;frame<3;++frame){
        glClearColor(0.02f,0.02f,0.03f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        render_forward_render(&fwd,&scene);
        if(frame==1) save_ppm(shot,W,H);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    render_forward_destroy(&fwd);
    for(int i=0;i<nm;++i){ static_mesh_destroy(&meshes[i]); obj_mesh_free(&dm[i]); }
    for(int c=0;c<9;c++) free(coeffs[c]); free(rects);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
