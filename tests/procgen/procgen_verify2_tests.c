#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

static int svo_at(const npc_svo_grid_t *g,int x,int y,int z){
    uint32_t c=1u<<g->max_depth;if(x<0||y<0||z<0||(uint32_t)x>=c||(uint32_t)y>=c||(uint32_t)z>=c)return 0;
    uint32_t n=0,cc=c;for(uint32_t d=0;d<g->max_depth;d++){cc>>=1;
        uint32_t ci=(((uint32_t)z/cc)&1)<<2|(((uint32_t)y/cc)&1)<<1|(((uint32_t)x/cc)&1);
        uint32_t ch=g->nodes[n].children[ci];if(ch==NPC_SVO_INVALID_NODE)return 0;n=ch;
        if(g->nodes[n].flags&NPC_SVO_FLAG_SOLID)return 1;}return 0;}

static int pip(const vec3_t*v,uint32_t n,float px,float py){int in=0;
    for(uint32_t i=0,j=n-1;i<n;j=i++){float xi=v[i].x,yi=v[i].y,xj=v[j].x,yj=v[j].y;
        if(((yi>py)!=(yj>py))&&(px<(xj-xi)*(py-yi)/(yj-yi)+xi))in=!in;}return in;}

/* Build surface-only dense grid matching SVO rasterizer behavior */
static int *build_surface_grid(const fr_dungeon_layout_t*l,uint32_t cells,float ox,float oy,float oz,float vs){
    uint32_t total=cells*cells*cells;int *g=calloc((size_t)total,sizeof(int));if(!g)return NULL;
    for(uint32_t ri=0;ri<l->room_count;ri++){const fr_room_def_t*r=&l->rooms[ri];
        float xn=r->vertices[0].x,xx=xn,yn=r->vertices[0].y,yx=yn;
        for(uint32_t j=1;j<r->vertex_count;j++){if(r->vertices[j].x<xn)xn=r->vertices[j].x;if(r->vertices[j].x>xx)xx=r->vertices[j].x;
            if(r->vertices[j].y<yn)yn=r->vertices[j].y;if(r->vertices[j].y>yx)yx=r->vertices[j].y;}
        int vx1=(int)((xn-ox)/vs+0.5f),vx2=(int)((xx-ox)/vs+0.5f);
        int vy1=(int)((yn-oy)/vs+0.5f),vy2=(int)((yx-oy)/vs+0.5f);
        int vz1=(int)((r->floor_z-oz)/vs+0.5f),vz2=(int)((r->ceil_z-oz)/vs+0.5f);
        /* Floor layer */
        for(int vy=vy1;vy<=vy2;vy++)for(int vx=vx1;vx<=vx2;vx++){
            float wx=ox+vx*vs,wy=oy+vy*vs;
            if(vx>=0&&vx<(int)cells&&vy>=0&&vy<(int)cells&&vz1>=0&&vz1<(int)cells&&pip(r->vertices,r->vertex_count,wx,wy))
                g[vz1*cells*cells+vy*cells+vx]=1;}
        /* Ceiling layer */
        int vcz=(int)((r->ceil_z-oz)/vs+0.5f);if(vcz>=0&&vcz<(int)cells)
        for(int vy=vy1;vy<=vy2;vy++)for(int vx=vx1;vx<=vx2;vx++){
            float wx=ox+vx*vs,wy=oy+vy*vs;
            if(vx>=0&&vx<(int)cells&&vy>=0&&vy<(int)cells&&pip(r->vertices,r->vertex_count,wx,wy))
                g[vcz*cells*cells+vy*cells+vx]=1;}
        /* Wall columns */
        for(uint32_t ei=0;ei<r->vertex_count;ei++){uint32_t ej=(ei+1)%r->vertex_count;
            float xa=r->vertices[ei].x,ya=r->vertices[ei].y,xb=r->vertices[ej].x,yb=r->vertices[ej].y;
            float dx=xb-xa,dy=yb-ya,len=sqrtf(dx*dx+dy*dy);int st=(int)(len/vs)+1;
            int wvz1=(int)((r->floor_z-oz)/vs+0.5f),wvz2=(int)((r->ceil_z-oz)/vs+0.5f);
            for(int s=0;s<=st;s++){float t=(float)s/(float)st,px=xa+t*dx,py=ya+t*dy;
                int wx=(int)((px-ox)/vs+0.5f),wy=(int)((py-oy)/vs+0.5f);
                for(int vz=wvz1;vz<=wvz2;vz++)if(wx>=0&&wx<(int)cells&&wy>=0&&wy<(int)cells&&vz>=0&&vz<(int)cells)g[vz*cells*cells+wy*cells+wx]=1;}}}
    return g;
}

int main(){
    const char*t="@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=main\nSPAWN x=5 y=5 z=1\nMARKER x=5 y=5 z=1 name=c\nMARKER x=9 y=5 z=1 name=d\nMARKER x=1 y=1 z=1 name=e\n";
    procgen_token_t tok[4096];char err[256];uint32_t cnt=0;
    procgen_tokenize(t,tok,4096,&cnt,err,sizeof(err));
    procgen_grammar_registry_init();procgen_grammar_t g={"blockout",1,procgen_tokenize,grammar_blockout_rasterize,NULL,NULL,0};procgen_grammar_register(&g);
    fr_dungeon_layout_t l;procgen_rasterize_with_registry(tok,cnt,&l,err,sizeof(err));
    /* Dense surface grid matching SVO parameters */
    uint32_t cells=512;float ox=-256,oy=-256,oz=-256,vs=1.0f;
    int *dense=build_surface_grid(&l,cells,ox,oy,oz,vs);
    /* SVO */
    procgen_raster_config_t cfg;procgen_raster_config_default(&cfg);cfg.max_depth=9;cfg.world_extent=256;
    npc_svo_grid_t svo;procgen_svo_build_cfg(&cfg,&l,&svo);
    /* Compare */
    int match=0,mismatch=0,svom=0,densem=0;
    for(uint32_t z=0;z<cells;z++)for(uint32_t y=0;y<cells;y++)for(uint32_t x=0;x<cells;x++){
        int ds=dense[z*cells*cells+y*cells+x];
        int ss=svo_at(&svo,(int)x,(int)y,(int)z);
        if(ds==ss)match++;else{mismatch++;
            if(mismatch<=10)printf("MISMATCH at (%d,%d,%d): dense=%d svo=%d  world=(%.1f,%.1f,%.1f)\n",x,y,z,ds,ss,ox+x*vs,oy+y*vs,oz+z*vs);}
        if(ss)svom++;if(ds)densem++;}
    printf("dense solid=%d  svo solid=%d  match=%d  mismatch=%d\n",densem,svom,match,mismatch);
    npc_svo_grid_destroy(&svo);free(dense);
    free(l.rooms);free(l.corridors);free(l.openings);free(l.ramps);free(l.markers);free(l.nav_nodes);free(l.nav_edges);
    return mismatch>0?1:0;
}
