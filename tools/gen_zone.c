/**
 * @file gen_zone.c
 * @brief Procedural "massive open zone" generator for the chunked lightmap bake
 *        (rpg-fzht). Emits a tiled hypostyle arcade -- a grid of stone columns
 *        carrying faceted barrel VAULTS that run in X, with open CLERESTORY gaps
 *        between adjacent vault ribbons so sky light floods down onto the floor
 *        and columns. Written as flat .dmesh triangle soup (see dmesh_loader.h):
 *        uint32 corner_count, then 10 LE floats/corner
 *        (pos3, nrm3, uv0(material)2, uv1(lightmap)2), Y-up.
 *
 * The scene is split into a grid of SUPER-TILES, one .dmesh per tile, each with
 * its own packed [0,1] lightmap unwrap (so the atlas holds one rect per tile and
 * texel density stays roughly uniform). Every column / vault / floor is emitted
 * into the tile that contains its centre.
 *
 * Standalone (libc + libm only):
 *   cc -O2 -o build/gen_zone tools/gen_zone.c -lm
 *   build/gen_zone datasets/zone [bays_x] [bays_z] [bay_m] [tiles]
 * Prints the world-space AABB (min/max) for the baker's svo_bounds on stdout.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Small vector helpers ──────────────────────────────────────────────── */
static void vsub(const float a[3], const float b[3], float o[3]) {
    o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2];
}
static void vcross(const float a[3], const float b[3], float o[3]) {
    o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0];
}
static void vnorm(float v[3]) {
    float m=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(m>1e-9f){ v[0]/=m; v[1]/=m; v[2]/=m; }
}

/* ── A quad: 4 CCW corners, chart-local lightmap uv, material uv. ───────── */
typedef struct { float p[4][3]; float luv[4][2]; float muv[4][2]; } Quad;

/* ── A lightmap chart: a run of quads sharing a sub-rectangle of [0,1]. ─── */
typedef struct {
    Quad  *q;        /* borrowed slice of the tile's quad array */
    int    n;
    float  uw, uh;   /* natural chart size (metres) -> uniform luxel density */
    float  ox, oy;   /* packed origin in [0,1] (filled by pack) */
    float  sx, sy;   /* packed size in [0,1]  (filled by pack) */
} Chart;

/* Per-tile accumulation: quads plus the chart table over them. */
#define MAX_QUADS_PER_TILE  200000
#define MAX_CHARTS_PER_TILE 20000
typedef struct {
    Quad   *quads; int nquads;
    Chart  *charts; int ncharts;
} Tile;

/* Append a quad with a chart-local uv layout (unit square) + planar material uv.
 * @p muv_scale metres-per-tile for the material texture. Normal from winding. */
static void push_quad(Tile *t, const float p0[3], const float p1[3],
                      const float p2[3], const float p3[3], float muv_scale) {
    if (t->nquads >= MAX_QUADS_PER_TILE) return;
    Quad *q = &t->quads[t->nquads++];
    memcpy(q->p[0],p0,12); memcpy(q->p[1],p1,12); memcpy(q->p[2],p2,12); memcpy(q->p[3],p3,12);
    /* chart-local lightmap uv: unit square, remapped when the chart is packed */
    q->luv[0][0]=0; q->luv[0][1]=0; q->luv[1][0]=1; q->luv[1][1]=0;
    q->luv[2][0]=1; q->luv[2][1]=1; q->luv[3][0]=0; q->luv[3][1]=1;
    /* material uv: project onto the plane perpendicular to the dominant normal
     * axis, world-scaled so the stone texture tiles. */
    float e1[3], e2[3], n[3]; vsub(p1,p0,e1); vsub(p3,p0,e2); vcross(e1,e2,n); vnorm(n);
    int ax = (fabsf(n[0])>fabsf(n[1]) && fabsf(n[0])>fabsf(n[2])) ? 0
           : (fabsf(n[1])>fabsf(n[2]) ? 1 : 2);
    const float *pp[4] = { p0,p1,p2,p3 };
    for (int i=0;i<4;++i) {
        float u = (ax==0)?pp[i][2]:pp[i][0];
        float v = (ax==1)?pp[i][2]:pp[i][1];
        q->muv[i][0]=u/muv_scale; q->muv[i][1]=v/muv_scale;
    }
}

/* Begin a chart over the last @p nq pushed quads, with natural size (uw,uh) m. */
static void push_chart(Tile *t, int nq, float uw, float uh) {
    if (t->ncharts >= MAX_CHARTS_PER_TILE || nq<=0) return;
    Chart *c = &t->charts[t->ncharts++];
    c->q = &t->quads[t->nquads - nq]; c->n = nq;
    c->uw = uw>1e-4f?uw:1e-4f; c->uh = uh>1e-4f?uh:1e-4f;
}

/* ── Shelf-pack a tile's charts into [0,1] preserving relative (metre) size, so
 *    luxel density is uniform across the tile. Sorts by height, rows L->R. ─── */
static int chart_cmp(const void *a, const void *b) {
    float ha=((const Chart*)a)->uh, hb=((const Chart*)b)->uh;
    return (ha<hb)-(ha>hb); /* descending */
}
static void pack_charts(Tile *t) {
    if (t->ncharts==0) return;
    qsort(t->charts, t->ncharts, sizeof(Chart), chart_cmp);
    float total=0; for(int i=0;i<t->ncharts;++i) total += t->charts[i].uw*t->charts[i].uh;
    float roww = sqrtf(total)*1.05f; if(roww<1e-4f) roww=1e-4f;
    const float gut = 0.004f; /* gutter between charts (uv units after scale) */
    /* First lay out at metre scale to get the bounding extent. */
    float x=0,y=0,rowh=0,maxx=0;
    for (int i=0;i<t->ncharts;++i) {
        Chart *c=&t->charts[i];
        if (x>0 && x+c->uw>roww) { y+=rowh; x=0; rowh=0; }
        c->ox=x; c->oy=y; c->sx=c->uw; c->sy=c->uh;
        x+=c->uw; if(c->uw>0 && x>maxx) maxx=x; if(c->uh>rowh) rowh=c->uh;
    }
    float exty=y+rowh, extx=maxx;
    float ext=extx>exty?extx:exty; if(ext<1e-4f) ext=1e-4f;
    float scale=(1.0f-2*gut)/ext;
    for (int i=0;i<t->ncharts;++i) {
        Chart *c=&t->charts[i];
        c->ox=gut+c->ox*scale; c->oy=gut+c->oy*scale;
        c->sx*=scale; c->sy*=scale;
    }
}

/* Resolve each quad's final lightmap uv from its chart placement, then write the
 * tile as .dmesh triangle soup (two tris per quad, 10 floats per corner). */
static int write_tile(const char *path, Tile *t) {
    /* Map each quad to its chart (charts borrow contiguous quad slices). */
    FILE *f=fopen(path,"wb"); if(!f) return -1;
    uint32_t corners=(uint32_t)t->nquads*6; fwrite(&corners,4,1,f);
    for (int ci=0; ci<t->ncharts; ++ci) {
        Chart *c=&t->charts[ci];
        for (int qi=0; qi<c->n; ++qi) {
            Quad *q=&c->q[qi];
            float e1[3],e2[3],n[3]; vsub(q->p[1],q->p[0],e1); vsub(q->p[3],q->p[0],e2);
            vcross(e1,e2,n); vnorm(n);
            const int tri[6]={0,1,2, 0,2,3};
            for (int k=0;k<6;++k) {
                int i=tri[k];
                float u1 = c->ox + q->luv[i][0]*c->sx;
                float v1 = c->oy + q->luv[i][1]*c->sy;
                float rec[10]={ q->p[i][0],q->p[i][1],q->p[i][2], n[0],n[1],n[2],
                                q->muv[i][0],q->muv[i][1], u1,v1 };
                fwrite(rec,4,10,f);
            }
        }
    }
    fclose(f); return 0;
}

/* ── Geometry emitters (all quads, Y-up) ───────────────────────────────── */

/* Axis-aligned box column [x-hw,x+hw]x[0,h]x[z-hw,z+hw]: 4 sides + top, laid
 * out as one horizontal strip chart. */
static void emit_column(Tile *t, float x, float z, float hw, float h) {
    float x0=x-hw,x1=x+hw,z0=z-hw,z1=z+hw,y0=0,y1=h;
    int start=t->nquads;
    /* +Z, +X, -Z, -X sides (CCW seen from outside), then top */
    float a[3],b[3],c[3],d[3];
    #define QQ(ax,ay,az,bx,by,bz,cx,cy,cz,dx,dy,dz) \
        a[0]=ax;a[1]=ay;a[2]=az; b[0]=bx;b[1]=by;b[2]=bz; \
        c[0]=cx;c[1]=cy;c[2]=cz; d[0]=dx;d[1]=dy;d[2]=dz; push_quad(t,a,b,c,d,2.0f)
    QQ(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1);   /* +Z */
    QQ(x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1);   /* +X */
    QQ(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0);   /* -Z */
    QQ(x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0);   /* -X */
    QQ(x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0);   /* top */
    #undef QQ
    push_chart(t, t->nquads-start, 4*(2*hw)+ (2*hw), h); /* strip: sides width + top, height h */
}

/* Faceted barrel-vault soffit over [x0,x1] in X, centred on z=zc, springing from
 * y=h0 with radius r, nseg facets across the semicircle (axis along X). One
 * chart (the arc unrolled along v). */
static void emit_vault(Tile *t, float x0, float x1, float zc, float h0, float r, int nseg) {
    int start=t->nquads;
    float arclen = 3.14159265f*r;
    for (int s=0;s<nseg;++s) {
        float a0=3.14159265f*(float)s/(float)nseg;      /* 0..pi */
        float a1=3.14159265f*(float)(s+1)/(float)nseg;
        float z0=zc - r*cosf(a0), y0=h0 + r*sinf(a0);
        float z1c=zc - r*cosf(a1), y1=h0 + r*sinf(a1);
        /* quad spanning x0..x1 at this facet; wound so the normal faces DOWN
         * (soffit, lit from below/inside). */
        float A[3]={x0,y0,z0}, B[3]={x1,y0,z0}, C[3]={x1,y1,z1c}, D[3]={x0,y1,z1c};
        push_quad(t,A,D,C,B,2.0f); /* reversed winding -> inward/down normal */
    }
    push_chart(t, t->nquads-start, (x1-x0), arclen);
}

int main(int argc, char **argv) {
    const char *outdir = argc>1?argv[1]:"datasets/zone";
    int   bays_x = argc>2?atoi(argv[2]):8;
    int   bays_z = argc>3?atoi(argv[3]):8;
    float bay    = argc>4?(float)atof(argv[4]):6.0f;
    int   tiles  = argc>5?atoi(argv[5]):4;   /* super-tiles per axis */

    const float col_hw = 0.35f, col_h = 4.0f;
    const float vault_r = bay*0.42f, vault_seg = 8;
    const float world_x = bays_x*bay, world_z = bays_z*bay;

    /* One Tile accumulator reused per super-tile. */
    Tile T; T.quads=malloc(sizeof(Quad)*MAX_QUADS_PER_TILE);
    T.charts=malloc(sizeof(Chart)*MAX_CHARTS_PER_TILE);
    if(!T.quads||!T.charts){ fprintf(stderr,"oom\n"); return 1; }

    float wmin[3]={1e30f,1e30f,1e30f}, wmax[3]={-1e30f,-1e30f,-1e30f};
    #define ACC(px,py,pz) do{ if(px<wmin[0])wmin[0]=px; if(py<wmin[1])wmin[1]=py; if(pz<wmin[2])wmin[2]=pz; \
        if(px>wmax[0])wmax[0]=px; if(py>wmax[1])wmax[1]=py; if(pz>wmax[2])wmax[2]=pz; }while(0)

    int nfiles=0;
    for (int tz=0; tz<tiles; ++tz)
    for (int tx=0; tx<tiles; ++tx) {
        T.nquads=0; T.ncharts=0;
        float tx0=world_x*(float)tx/(float)tiles, tx1=world_x*(float)(tx+1)/(float)tiles;
        float tz0=world_z*(float)tz/(float)tiles, tz1=world_z*(float)(tz+1)/(float)tiles;

        /* Floor: one quad over the tile footprint (single chart). */
        {   int s=T.nquads;
            float A[3]={tx0,0,tz0},B[3]={tx1,0,tz0},C[3]={tx1,0,tz1},D[3]={tx0,0,tz1};
            push_quad(&T,A,B,C,D,2.0f); push_chart(&T,T.nquads-s,(tx1-tx0),(tz1-tz0));
            ACC(tx0,0,tz0); ACC(tx1,0,tz1);
        }
        /* Columns at every grid node whose centre lands in this tile. */
        for (int j=0;j<=bays_z;++j)
        for (int i=0;i<=bays_x;++i) {
            float x=i*bay, z=j*bay;
            if (x<tx0-1e-3f||x>=tx1+1e-3f||z<tz0-1e-3f||z>=tz1+1e-3f) continue;
            /* keep shared border nodes in exactly one tile */
            if ((x<tx0||x>=tx1||z<tz0||z>=tz1)) continue;
            emit_column(&T,x,z,col_hw,col_h);
            ACC(x-col_hw,0,z-col_hw); ACC(x+col_hw,col_h,z+col_hw);
        }
        /* Barrel vaults: a ribbon over each column ROW (constant z=j*bay) that
         * runs in X, clipped to this tile's X range. Rows are spaced by 2*bay so
         * a clerestory gap opens to the sky between adjacent vaults. */
        for (int j=0;j<=bays_z;j+=2) {
            float zc=j*bay; if(zc<tz0||zc>=tz1) continue;
            emit_vault(&T, tx0, tx1, zc, col_h, vault_r, (int)vault_seg);
            ACC(tx0,col_h,zc-vault_r); ACC(tx1,col_h+vault_r,zc+vault_r);
        }

        if (T.nquads==0) continue;
        pack_charts(&T);
        char path[512]; snprintf(path,sizeof path,"%s/zone_r%02d_c%02d.dmesh",outdir,tz,tx);
        if (write_tile(path,&T)!=0){ fprintf(stderr,"write fail %s\n",path); return 1; }
        ++nfiles;
    }

    fprintf(stderr,"gen_zone: %d tiles, %.0fx%.0f m\n", nfiles, world_x, world_z);
    /* Print svo_bounds (Y-up) for the baker: min/max with a small pad. */
    printf("BOUNDS %.3f %.3f %.3f  %.3f %.3f %.3f\n",
           wmin[0]-1.0f,wmin[1]-1.0f,wmin[2]-1.0f, wmax[0]+1.0f,wmax[1]+1.0f,wmax[2]+1.0f);
    free(T.quads); free(T.charts);
    return 0;
}
