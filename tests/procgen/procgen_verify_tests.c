#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

static int svo_at(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t c=1u<<g->max_depth;
    if(x<0||y<0||z<0||(uint32_t)x>=c||(uint32_t)y>=c||(uint32_t)z>=c)return 0;
    uint32_t n=0,cc=c;
    for(uint32_t d=0;d<g->max_depth;d++){cc>>=1;
        uint32_t ci=(((uint32_t)z/cc)&1)<<2|(((uint32_t)y/cc)&1)<<1|(((uint32_t)x/cc)&1);
        uint32_t ch=g->nodes[n].children[ci];if(ch==NPC_SVO_INVALID_NODE)return 0;n=ch;
        if(g->nodes[n].flags&NPC_SVO_FLAG_SOLID)return 1;}
    return 0;
}

static void export_obj(const char *path, const procgen_mesh_t *m) {
    FILE*f=fopen(path,"w");if(!f)return;
    fprintf(f,"# %u verts\n",m->vertex_count/3);
    for(uint32_t i=0;i<m->vertex_count;i+=3)fprintf(f,"v %.3f %.3f %.3f\n",m->vertices[i],m->vertices[i+1],m->vertices[i+2]);
    for(uint32_t i=0;i<m->vertex_count;i+=9)fprintf(f,"f %u %u %u\n",i/3+1,i/3+2,i/3+3);
    fclose(f);
    printf("%s: %u tris\n",path,m->vertex_count/9);
}

static int point_in_poly(const vec3_t *v, uint32_t n, float px, float py) {
    int in=0;for(uint32_t i=0,j=n-1;i<n;j=i++){float xi=v[i].x,yi=v[i].y,xj=v[j].x,yj=v[j].y;
        if(((yi>py)!=(yj>py))&&(px<(xj-xi)*(py-yi)/(yj-yi)+xi))in=!in;}return in;
}

/* Build dense voxel occupancy from layout geometry directly */
static int *build_dense_grid(const fr_dungeon_layout_t *l, uint32_t res) {
    uint32_t total=res*res*res;
    int *grid=calloc((size_t)total,sizeof(int));
    if(!grid)return NULL;
    /* For each room: fill XY polygon extruded Z */
    for(uint32_t ri=0;ri<l->room_count;ri++){
        const fr_room_def_t*r=&l->rooms[ri];
        float xn=r->vertices[0].x,xx=xn,yn=r->vertices[0].y,yx=yn;
        for(uint32_t j=1;j<r->vertex_count;j++){if(r->vertices[j].x<xn)xn=r->vertices[j].x;if(r->vertices[j].x>xx)xx=r->vertices[j].x;
            if(r->vertices[j].y<yn)yn=r->vertices[j].y;if(r->vertices[j].y>yx)yx=r->vertices[j].y;}
        int vx1=(int)((xn+256)/512*res+0.5f),vx2=(int)((xx+256)/512*res+0.5f);
        int vy1=(int)((yn+256)/512*res+0.5f),vy2=(int)((yx+256)/512*res+0.5f);
        int vz1=(int)((r->floor_z+256)/512*res+0.5f),vz2=(int)((r->ceil_z+256)/512*res+0.5f);
        for(int vz=vz1;vz<=vz2;vz++)for(int vy=vy1;vy<=vy2;vy++)for(int vx=vx1;vx<=vx2;vx++){
            if(vx>=0&&vx<(int)res&&vy>=0&&vy<(int)res&&vz>=0&&vz<(int)res){
                float wx=(float)vx/res*512-256,wy=(float)vy/res*512-256;
                if(point_in_poly(r->vertices,r->vertex_count,wx,wy))
                    grid[vz*res*res+vy*res+vx]=1;
            }}
    }
    /* For corridors: similar approach */
    for(uint32_t ci=0;ci<l->corridor_count;ci++){
        const fr_corridor_def_t*c=&l->corridors[ci];float hw=c->width*0.5f;
        float xn=fminf(c->from.x,c->to.x)-hw,xx=fmaxf(c->from.x,c->to.x)+hw;
        float yn=fminf(c->from.y,c->to.y)-hw,yx=fmaxf(c->from.y,c->to.y)+hw;
        int vx1=(int)((xn+256)/512*res+0.5f),vx2=(int)((xx+256)/512*res+0.5f);
        int vy1=(int)((yn+256)/512*res+0.5f),vy2=(int)((yx+256)/512*res+0.5f);
        int vz1=(int)((c->floor_z+256)/512*res+0.5f),vz2=(int)((c->ceil_z+256)/512*res+0.5f);
        float dx=c->to.x-c->from.x,dy=c->to.y-c->from.y,len2=dx*dx+dy*dy;
        for(int vz=vz1;vz<=vz2;vz++)for(int vy=vy1;vy<=vy2;vy++)for(int vx=vx1;vx<=vx2;vx++){
            float px=(float)vx/res*512-256,py=(float)vy/res*512-256;
            float t=((px-c->from.x)*dx+(py-c->from.y)*dy)/len2;if(t<0)t=0;if(t>1)t=1;
            float cx=c->from.x+t*dx,cy=c->from.y+t*dy;
            if((px-cx)*(px-cx)+(py-cy)*(py-cy)<=hw*hw&&vx>=0&&vx<(int)res&&vy>=0&&vy<(int)res&&vz>=0&&vz<(int)res)
                grid[vz*res*res+vy*res+vx]=1;
        }
    }
    return grid;
}

int main() {
    const char *t = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=main\nSPAWN x=5 y=5 z=1\nDOOR at=(10,5) w=2 h=3\nMARKER x=5 y=5 z=1 name=c\nMARKER x=9 y=5 z=1 name=d\nMARKER x=1 y=1 z=1 name=e\n";
    procgen_token_t tok[4096];char err[256];uint32_t cnt=0;
    procgen_tokenize(t,tok,4096,&cnt,err,sizeof(err));
    procgen_grammar_registry_init();
    procgen_grammar_t g={"blockout",1,procgen_tokenize,grammar_blockout_rasterize,NULL,NULL,0};
    procgen_grammar_register(&g);
    fr_dungeon_layout_t l;procgen_rasterize_with_registry(tok,cnt,&l,err,sizeof(err));

    uint32_t res=64; /* 64³ = 262K cells, 8m per cell */
    int *dense=build_dense_grid(&l,res);
    procgen_raster_config_t cfg;procgen_raster_config_default(&cfg);cfg.max_depth=9;cfg.world_extent=256.0f;
    npc_svo_grid_t svo;procgen_svo_build_cfg(&cfg,&l,&svo);

    /* Verify SVO matches dense grid */
    int match=0,mismatch=0;
    for(uint32_t z=0;z<res;z++)for(uint32_t y=0;y<res;y++)for(uint32_t x=0;x<res;x++){
        int svx=(int)((float)x/res*512+0.5f),svy=(int)((float)y/res*512+0.5f),svz=(int)((float)z/res*512+0.5f);
        int ds=dense[z*res*res+y*res+x];
        int ss=svo_at(&svo,svx,svy,svz);
        if(ds==ss)match++;else mismatch++;
    }
    printf("Dense vs SVO: %d match, %d mismatch (of %u)\n",match,mismatch,res*res*res);

    procgen_mesh_t mesh;procgen_mesh_init(&mesh);procgen_mesh_from_svo(&svo,&mesh);
    export_obj("/tmp/verify_room.obj",&mesh);
    procgen_mesh_destroy(&mesh);npc_svo_grid_destroy(&svo);free(dense);
    free(l.rooms);free(l.corridors);free(l.openings);free(l.ramps);free(l.markers);free(l.nav_nodes);free(l.nav_edges);
    return 0;
}
