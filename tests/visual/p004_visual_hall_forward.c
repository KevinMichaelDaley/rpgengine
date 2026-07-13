/**
 * @file p004_visual_hall_forward.c
 * @brief Forward+ payoff scene: the exported romanesque hall (25 objects loaded
 *        from OBJ) with the real brick material, a static directional sun for
 *        fill, and many small coloured point lights ("magic particles") culled
 *        per-cluster and shaded via the forward+ path. PPM screenshot.
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

#include "ferrum/mesh/obj_loader.h"
#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/forward_plus.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 900
#define MAX_MESH 64
#define MAX_LIGHTS 96

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static float frand(uint32_t *s){ *s=*s*1664525u+1013904223u; return (float)(*s>>8)*(1.0f/16777216.0f); }

static void save_ppm(const char *path, int w, int h) {
    size_t row=(size_t)w*3; uint8_t *rgb=malloc(row*(size_t)h); if(!rgb) return;
    glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,rgb);
    FILE *f=fopen(path,"wb");
    if(f){ fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*row,1,row,f); fclose(f);
        printf("screenshot: %s\n",path);} free(rgb);
}
static int load_tex(texture_t *t,const gl_loader_t *l,const char *p,texture_format_t f){
    int w=0,h=0,n=0; unsigned char *px=stbi_load(p,&w,&h,&n,3); if(!px) return 0;
    texture_create(t,l); texture_upload_2d(t,f,(uint32_t)w,(uint32_t)h,px,true);
    texture_set_sampler(t,GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR,GL_REPEAT,GL_REPEAT);
    stbi_image_free(px); return 1;
}
static int is_brick(const char *n){ return strstr(n,"win")||strstr(n,"door"); }
static int is_vault(const char *n){ return strstr(n,"vault")!=NULL; }

int main(int argc, char **argv) {
    const char *dir = argc>1?argv[1]:"datasets/hall";
    const char *bake = argc>2?argv[2]:"assets/arch/proc/prefabs/bake";
    const char *shot = argc>3?argv[3]:"/tmp/p004_hall.ppm";

    if (SDL_Init(SDL_INIT_VIDEO)!=0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window *win=SDL_CreateWindow("hall",0,0,WIN,WIN,SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    SDL_GLContext gc=SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win,gc);
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader={sdl_get_proc,NULL};

    /* --- Load every OBJ in the hall directory into a static mesh. --- */
    static_mesh_t meshes[MAX_MESH]; int mgroup[MAX_MESH]; int nmesh=0;
    render_submesh_t subs[MAX_MESH];
    float amin[3]={1e30f,1e30f,1e30f}, amax[3]={-1e30f,-1e30f,-1e30f};
    DIR *d=opendir(dir); struct dirent *e;
    while(d && (e=readdir(d)) && nmesh<MAX_MESH){
        if(!strstr(e->d_name,".obj")) continue;
        char path[512]; snprintf(path,sizeof(path),"%s/%s",dir,e->d_name);
        obj_mesh_t m; if(obj_mesh_load(path,1.0f,&m)) continue;
        for(uint32_t v=0;v<m.vert_count;++v) for(int c=0;c<3;++c){
            float p=m.positions[v*3+c]; if(p<amin[c])amin[c]=p; if(p>amax[c])amax[c]=p; }
        subs[nmesh]=(render_submesh_t){0,m.index_count,0};
        static_mesh_create_info_t info; memset(&info,0,sizeof info);
        info.positions=m.positions; info.normals=m.normals; info.tangents=m.tangents;
        info.uv0=m.uvs; info.uv1=m.uvs;
        info.indices=m.indices; info.vertex_count=m.vert_count; info.index_count=m.index_count;
        info.submeshes=&subs[nmesh]; info.submesh_count=1;
        if(static_mesh_create(&loader,&info,&meshes[nmesh])==0){
            mgroup[nmesh]= is_brick(e->d_name)?0 : (is_vault(e->d_name)?2:1);
            ++nmesh;
        }
        obj_mesh_free(&m);
    }
    if(d) closedir(d);
    printf("loaded %d meshes; AABB (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",nmesh,
           amin[0],amin[1],amin[2],amax[0],amax[1],amax[2]);
    float cx=(amin[0]+amax[0])*0.5f, cy=(amin[1]+amax[1])*0.5f, cz=(amin[2]+amax[2])*0.5f;

    /* --- Brick material maps (baked from the stone_wall material) + set. --- */
    char pa[512],pn[512],po[512],pr[512];
    snprintf(pa,sizeof pa,"%s/albedo.png",bake); snprintf(pn,sizeof pn,"%s/normal.png",bake);
    snprintf(po,sizeof po,"%s/ao.png",bake);    snprintf(pr,sizeof pr,"%s/roughness.png",bake);
    texture_t t_alb,t_nrm,t_ao,t_rough;
    load_tex(&t_alb,&loader,pa,TEXTURE_FORMAT_SRGB8);
    load_tex(&t_nrm,&loader,pn,TEXTURE_FORMAT_RGB8);
    load_tex(&t_ao,&loader,po,TEXTURE_FORMAT_RGB8);
    load_tex(&t_rough,&loader,pr,TEXTURE_FORMAT_RGB8);
    render_material_t brick,stone,vault;
    material_init(&brick);
    brick.maps[MATERIAL_TEX_ALBEDO]=&t_alb; brick.maps[MATERIAL_TEX_NORMAL]=&t_nrm;
    brick.maps[MATERIAL_TEX_AO]=&t_ao;       brick.maps[MATERIAL_TEX_ROUGHNESS]=&t_rough;
    brick.tint[0]=1.0f; brick.tint[1]=0.98f; brick.tint[2]=0.94f; /* albedo carries colour */
    brick.roughness_min=0.25f; brick.roughness_max=1.0f; brick.normal_scale=1.3f;
    char sa[512],sr[512],va[512],vr[512];
    snprintf(sa,sizeof sa,"%s/ashlar_albedo.png",bake); snprintf(sr,sizeof sr,"%s/ashlar_roughness.png",bake);
    snprintf(va,sizeof va,"%s/vault_albedo.png",bake);  snprintf(vr,sizeof vr,"%s/vault_roughness.png",bake);
    texture_t t_sa,t_sr,t_va,t_vr;
    load_tex(&t_sa,&loader,sa,TEXTURE_FORMAT_SRGB8); load_tex(&t_sr,&loader,sr,TEXTURE_FORMAT_RGB8);
    load_tex(&t_va,&loader,va,TEXTURE_FORMAT_SRGB8); load_tex(&t_vr,&loader,vr,TEXTURE_FORMAT_RGB8);
    material_init(&stone); stone.maps[MATERIAL_TEX_ALBEDO]=&t_sa; stone.maps[MATERIAL_TEX_ROUGHNESS]=&t_sr;
    stone.tint[0]=stone.tint[1]=stone.tint[2]=1.0f; stone.roughness_min=0.2f; stone.roughness_max=1.0f;
    material_init(&vault); vault.maps[MATERIAL_TEX_ALBEDO]=&t_va; vault.maps[MATERIAL_TEX_ROUGHNESS]=&t_vr;
    vault.tint[0]=vault.tint[1]=vault.tint[2]=1.0f; vault.roughness_min=0.2f; vault.roughness_max=1.0f;
    render_material_t *mat_for[3]={&brick,&stone,&vault};

    /* --- Lights: a static directional sun + many coloured particle lights. --- */
    render_light_t lights[MAX_LIGHTS]; int nl=0;
    float palette[6][3]={{1,0.3f,0.3f},{0.3f,1,0.4f},{0.35f,0.5f,1},{1,0.85f,0.3f},{1,0.4f,0.9f},{0.3f,1,1}};
    uint32_t rng=1337;
    for(int i=0;i<64 && nl<MAX_LIGHTS;++i){
        render_light_t l; memset(&l,0,sizeof l); l.kind=RENDER_LIGHT_POINT;
        l.position[0]=amin[0]+frand(&rng)*(amax[0]-amin[0]);
        l.position[1]=amin[1]+0.15f*(amax[1]-amin[1])+frand(&rng)*0.6f*(amax[1]-amin[1]);
        l.position[2]=amin[2]+frand(&rng)*(amax[2]-amin[2]);
        const float *pc=palette[i%6];
        l.color[0]=pc[0]; l.color[1]=pc[1]; l.color[2]=pc[2];
        l.intensity=6.0f; l.range=2.2f; l.flags=RENDER_LIGHT_FLAG_REALTIME;
        lights[nl++]=l;
    }
    printf("particle lights: %d\n",nl);

    /* --- Camera: stand inside near one end, look down the hall length. --- */
    float span[3]={amax[0]-amin[0],amax[1]-amin[1],amax[2]-amin[2]};
    int lenax = (span[0]>span[2])?0:2; /* longest horizontal axis */
    render_camera_t cam;
    float eye[3]={cx,cy,cz}, tgt[3]={cx,cy,cz}, up[3]={0,1,0};
    eye[lenax]  = amin[lenax] + span[lenax]*0.08f;   /* just inside the near end */
    tgt[lenax]  = amax[lenax] - span[lenax]*0.08f;   /* toward the far end */
    eye[1] = amin[1] + span[1]*0.35f; tgt[1] = amin[1] + span[1]*0.35f;
    render_camera_look_at(&cam,eye,tgt,up,60.0f*(float)M_PI/180.0f,1.0f,0.2f,60.0f);

    /* --- Cluster grid + forward+ upload. --- */
    cluster_config_t ccfg={16,16,24, 0.2f, 60.0f};
    uint32_t ctot=ccfg.tiles_x*ccfg.tiles_y*ccfg.slices, icap=ctot*16;
    uint32_t *coff=malloc(ctot*4),*ccnt=malloc(ctot*4),*cidx=malloc(icap*4);
    cluster_grid_t grid; cluster_grid_init(&grid,ccfg,coff,ccnt,cidx,icap);
    cluster_grid_build(&grid,&cam,lights,(uint32_t)nl);
    float *ldata=malloc((size_t)nl*16*sizeof(float));
    for(int i=0;i<nl;++i) forward_plus_pack_light(&lights[i],&ldata[i*16]);
    forward_plus_t fp; forward_plus_init(&fp,&loader);
    forward_plus_upload(&fp,&grid,ldata,(uint32_t)nl);

    shader_program_t prog; char log[2048]={0};
    if(pbr_shader_create(&prog,&loader,log,sizeof log)!=SHADER_PROGRAM_OK){fprintf(stderr,"shader:%s\n",log);return 1;}
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache,&prog);

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    for(int frame=0;frame<3;++frame){
        glClearColor(0.02f,0.02f,0.03f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",cam.view,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",cam.proj,0);
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",cam.eye);
        float sun_dir[3]={0.3f,0.8f,0.4f}, sun_col[3]={0.5f,0.5f,0.55f}, amb[3]={0.05f,0.05f,0.07f};
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",sun_dir);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_color",sun_col);
        shader_uniform_set_vec3(&cache,&prog,"u_ambient",amb);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",0);
        shader_uniform_set_int(&cache,&prog,"u_light_count",0);
        forward_plus_bind(&fp,&cache,&prog,&ccfg,(float)WIN,(float)WIN);
        float model[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        shader_uniform_set_mat4(&cache,&prog,"u_model",model,0);
        for(int i=0;i<nmesh;++i){
            material_bind(mat_for[mgroup[i]],0u,&cache,&prog);
            static_mesh_bind(&meshes[i]);
            for(uint32_t s=0;s<meshes[i].submesh_count;++s) static_mesh_draw_submesh(&meshes[i],s);
        }
        if(frame==1) save_ppm(shot,WIN,WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    for(int i=0;i<nmesh;++i) static_mesh_destroy(&meshes[i]);
    forward_plus_destroy(&fp); shader_program_destroy(&prog);
    free(coff);free(ccnt);free(cidx);free(ldata);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
