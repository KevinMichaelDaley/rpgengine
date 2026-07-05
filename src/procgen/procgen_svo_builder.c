#include "ferrum/procgen/procgen_svo_builder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void procgen_raster_config_default(procgen_raster_config_t *c) {
    memset(c,0,sizeof(*c)); c->type=PROCGEN_RASTER_VOXEL;
    c->voxel_size=PROCGEN_DEFAULT_VOXEL_SIZE; c->max_depth=PROCGEN_DEFAULT_MAX_DEPTH;
    c->world_extent=PROCGEN_DEFAULT_WORLD_EXTENT;
}

static void mark_block_solid_(npc_svo_grid_t *g,uint32_t vx,uint32_t vy,uint32_t vz,uint32_t bs){
    uint32_t n=0,cells=1u<<g->max_depth,target=bs;
    for(uint32_t d=0;d<g->max_depth;d++){cells>>=1;if(cells<target)break;
        uint32_t ci=((vz/cells)&1)<<2|((vy/cells)&1)<<1|((vx/cells)&1);npc_svo_node_t *nd=&g->nodes[n];
        uint32_t ch=nd->children[ci];if(ch==NPC_SVO_INVALID_NODE){ch=npc_svo_alloc_node(g);if(ch==NPC_SVO_INVALID_NODE)return;
            nd->children[ci]=ch;g->nodes[ch].parent=n;nd->occupancy|=(1u<<ci);}n=ch;}
    g->nodes[n].flags|=NPC_SVO_FLAG_SOLID;
}

static int is_solid_(const npc_svo_grid_t *g,int x,int y,int z){
    uint32_t cells=1u<<g->max_depth;if(x<0||y<0||z<0||(uint32_t)x>=cells||(uint32_t)y>=cells||(uint32_t)z>=cells)return 0;
    uint32_t n=0,c=cells;
    for(uint32_t d=0;d<g->max_depth;d++){c>>=1;uint32_t ci=(((uint32_t)z/c)&1)<<2|(((uint32_t)y/c)&1)<<1|(((uint32_t)x/c)&1);
        uint32_t ch=g->nodes[n].children[ci];if(ch==NPC_SVO_INVALID_NODE)return 0;n=ch;
        if(g->nodes[n].flags&NPC_SVO_FLAG_SOLID)return 1;}
    return 0;
}

static void v2w(const npc_svo_grid_t *g,float wx,float wy,float wz,uint32_t *vx,uint32_t *vy,uint32_t *vz){
    uint32_t c=1u<<g->max_depth;float sx=g->world_bounds.max.x-g->world_bounds.min.x,sy=g->world_bounds.max.y-g->world_bounds.min.y,sz=g->world_bounds.max.z-g->world_bounds.min.z;
    float cx=(wx-g->world_bounds.min.x)/sx*(float)c,cy=(wy-g->world_bounds.min.y)/sy*(float)c,cz=(wz-g->world_bounds.min.z)/sz*(float)c;
    *vx=(uint32_t)(cx<0?0:(cx>=(float)c?c-1:cx));*vy=(uint32_t)(cy<0?0:(cy>=(float)c?c-1:cy));*vz=(uint32_t)(cz<0?0:(cz>=(float)c?c-1:cz));
}

static int pip(const vec3_t *v,uint32_t n,float px,float py){int in=0;
    for(uint32_t i=0,j=n-1;i<n;j=i++){float xi=v[i].x,yi=v[i].y,xj=v[j].x,yj=v[j].y;
        if(((yi>py)!=(yj>py))&&(px<(xj-xi)*(py-yi)/(yj-yi)+xi))in=!in;}return in;}

#define BS 3

static uint32_t shell(npc_svo_grid_t *g,float x1,float x2,float y1,float y2,float fz,float cz,const vec3_t *poly,uint32_t pn){
    uint32_t bs=1u<<BS;float vs=g->voxel_size*bs,vm=(float)(1u<<g->max_depth);uint32_t md=0;
    uint32_t fx1,fy1,fz1,fx2,fy2,fz2;v2w(g,x1,fz,y1,&fx1,&fy1,&fz1);v2w(g,x2,cz,y2,&fx2,&fy2,&fz2);
    if(fx2>=vm)fx2=vm-1;if(fy2>=vm)fy2=vm-1;if(fz2>=vm)fz2=vm-1;
    for(uint32_t vz=fz1;vz<=fz2;vz+=bs)for(uint32_t vx=fx1;vx<=fx2;vx+=bs){
        float cx=g->world_bounds.min.x+((float)vx+vs*0.5f)*g->voxel_size;
        float cz=g->world_bounds.min.z+((float)vz+vs*0.5f)*g->voxel_size;
        if(pip(poly,pn,cx,cz)){for(uint32_t vy=fy1;vy<=fy2;vy+=bs){mark_block_solid_(g,vx,vy,vz,bs);md++;}}}
    for(uint32_t ei=0;ei<pn;ei++){uint32_t ej=(ei+1)%pn;float xa=poly[ei].x,ya=poly[ei].y,xb=poly[ej].x,yb=poly[ej].y;
        float dx=xb-xa,dy=yb-ya,len=sqrtf(dx*dx+dy*dy);int st=(int)(len/vs)+1;
        for(int s=0;s<=st;s++){float t=(float)s/(float)st,px=xa+t*dx,pz=ya+t*dy;
            uint32_t wx,wy,wz,_,wz2;v2w(g,px,fz,pz,&wx,&wy,&wz);v2w(g,px,cz,pz,&_,&_,&wz2);
            for(uint32_t zy=wy;zy<=wz2;zy+=bs){mark_block_solid_(g,wx,zy,wz,bs);md++;}}}
    return md;
}

static uint32_t line2(npc_svo_grid_t *g,float x1,float y1,float x2,float y2,float w,float fz,float cz){
    uint32_t bs=1u<<BS,md=0;float hw=w*0.5f,vm=(float)(1u<<g->max_depth);float vs=g->voxel_size*bs;
    float xn=fminf(x1,x2)-hw,xx=fmaxf(x1,x2)+hw,zn=fminf(y1,y2)-hw,zx=fmaxf(y1,y2)+hw;
    uint32_t mvx,mvy,mvz,Mvx,Mvy,Mvz;v2w(g,xn,fz,zn,&mvx,&mvy,&mvz);v2w(g,xx,cz,zx,&Mvx,&Mvy,&Mvz);
    if(Mvx>=vm)Mvx=vm-1;if(Mvy>=vm)Mvy=vm-1;if(Mvz>=vm)Mvz=vm-1;
    float dx=x2-x1,dz2=y2-y1,len2=dx*dx+dz2*dz2;
    for(uint32_t vy=mvy;vy<=Mvy;vy+=bs)for(uint32_t vz=mvz;vz<=Mvz;vz+=bs)for(uint32_t vx=mvx;vx<=Mvx;vx+=bs){
        float px=g->world_bounds.min.x+((float)vx+vs*0.5f)*g->voxel_size;
        float pz=g->world_bounds.min.z+((float)vz+vs*0.5f)*g->voxel_size;
        float t=((px-x1)*dx+(pz-y1)*dz2)/len2;if(t<0)t=0;if(t>1)t=1;float cx=x1+t*dx,cz2=y1+t*dz2;
        if((px-cx)*(px-cx)+(pz-cz2)*(pz-cz2)<=hw*hw){mark_block_solid_(g,vx,vy,vz,bs);md++;}}
    return md;
}

uint32_t procgen_svo_build_cfg(const procgen_raster_config_t *cfg,const fr_dungeon_layout_t *l,npc_svo_grid_t *g){
    if(!cfg||!l||!g)return 0;phys_aabb_t b={{-cfg->world_extent,-cfg->world_extent,-cfg->world_extent},{cfg->world_extent,cfg->world_extent,cfg->world_extent}};
    if(!npc_svo_grid_init(g,b,cfg->max_depth))return 0;uint32_t t=0;
    for(uint32_t ri=0;ri<l->room_count;ri++){const fr_room_def_t *r=&l->rooms[ri];
        float xn=r->vertices[0].x,xx=xn,yn=r->vertices[0].y,yx=yn;
        for(uint32_t j=1;j<r->vertex_count;j++){if(r->vertices[j].x<xn)xn=r->vertices[j].x;if(r->vertices[j].x>xx)xx=r->vertices[j].x;
            if(r->vertices[j].y<yn)yn=r->vertices[j].y;if(r->vertices[j].y>yx)yx=r->vertices[j].y;}
        t+=shell(g,xn,xx,yn,yx,r->floor_z,r->ceil_z,r->vertices,r->vertex_count);}
    for(uint32_t ci=0;ci<l->corridor_count;ci++){const fr_corridor_def_t *c=&l->corridors[ci];
        t+=line2(g,c->from.x,c->from.y,c->to.x,c->to.y,c->width,c->floor_z,c->ceil_z);}
    for(uint32_t ri=0;ri<l->ramp_count;ri++){const fr_ramp_def_t *r=&l->ramps[ri];float lo=fminf(r->from.z,r->to.z),hi=fmaxf(r->from.z,r->to.z);
        t+=line2(g,r->from.x,r->from.y,r->to.x,r->to.y,r->width,lo,hi);}
    return t;}

uint32_t procgen_svo_build(npc_svo_grid_t *g,const fr_dungeon_layout_t *l){procgen_raster_config_t c;procgen_raster_config_default(&c);return procgen_svo_build_cfg(&c,l,g);}

void procgen_mesh_init(procgen_mesh_t *m){memset(m,0,sizeof(*m));}
void procgen_mesh_destroy(procgen_mesh_t *m){free(m->vertices);memset(m,0,sizeof(*m));}

static void em(procgen_mesh_t *m,float x0,float y0,float z0,float x1,float y1,float z1){
    uint32_t need=m->vertex_count+18;if(need>m->vertex_cap){uint32_t nc=m->vertex_cap?m->vertex_cap*2:65536;if(nc<need)nc=need;
        float *nd=realloc(m->vertices,nc*sizeof(float));if(!nd)return;m->vertices=nd;m->vertex_cap=nc;}
    float *v=m->vertices+m->vertex_count;
    v[0]=x0;v[1]=y0;v[2]=z0;v[3]=x1;v[4]=y0;v[5]=z0;v[6]=x1;v[7]=y1;v[8]=z0;
    v[9]=x0;v[10]=y0;v[11]=z0;v[12]=x1;v[13]=y1;v[14]=z0;v[15]=x0;v[16]=y1;v[17]=z0;
    m->vertex_count+=18;}

uint32_t procgen_mesh_from_svo(const npc_svo_grid_t *g,procgen_mesh_t *mesh){
    uint32_t c2=1u<<g->max_depth,bs=1u<<BS;float vs=g->voxel_size*bs,ox=g->world_bounds.min.x,oy=g->world_bounds.min.y,oz=g->world_bounds.min.z;uint32_t tc=0,vc=c2/bs;
    for(uint32_t vz=0;vz<vc;vz++)for(uint32_t vy=0;vy<vc;vy++)for(uint32_t vx=0;vx<vc;vx++){
        if(!is_solid_(g,(int)(vx*bs),(int)(vy*bs),(int)(vz*bs)))continue;
        float x0=ox+(float)(vx*bs)*g->voxel_size,x1=x0+vs,y0=oy+(float)(vy*bs)*g->voxel_size,y1=y0+vs,z0=oz+(float)(vz*bs)*g->voxel_size,z1=z0+vs;
        if(vx+1>=vc||!is_solid_(g,(int)((vx+1)*bs),(int)(vy*bs),(int)(vz*bs))){em(mesh,x1,z0,y0,x1,z1,y1);tc+=2;}
        if(vx==0||!is_solid_(g,(int)((vx-1)*bs),(int)(vy*bs),(int)(vz*bs))){em(mesh,x0,z0,y0,x0,z1,y1);tc+=2;}
        if(vy+1>=vc||!is_solid_(g,(int)(vx*bs),(int)((vy+1)*bs),(int)(vz*bs))){em(mesh,x0,z0,y1,x1,z1,y1);tc+=2;}
        if(vy==0||!is_solid_(g,(int)(vx*bs),(int)((vy-1)*bs),(int)(vz*bs))){em(mesh,x0,z0,y0,x1,z1,y0);tc+=2;}
        if(vz+1>=vc||!is_solid_(g,(int)(vx*bs),(int)(vy*bs),(int)((vz+1)*bs))){em(mesh,x1,y0,z1,x0,y1,z1);tc+=2;}
        if(vz==0||!is_solid_(g,(int)(vx*bs),(int)(vy*bs),(int)((vz-1)*bs))){em(mesh,x0,y0,z0,x1,y1,z0);tc+=2;}}
    return tc;}
