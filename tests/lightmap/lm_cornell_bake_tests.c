/**
 * @file lm_cornell_bake_tests.c
 * @brief Regression test: bake the Cornell box through the triangle-mesh baker
 *        (material albedo/emissive from textures) and assert the GI -- the light
 *        reaches the floor and colour bleeds from the red/green side walls.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/memory/arena.h"

#define ASSERT_TRUE(c)                                                        \
    do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* Quad (2 triangles) storage: 4 positions/normals/uvs + 6 indices. */
typedef struct quad { float pos[12], nrm[12], uv[8]; uint32_t idx[6]; } quad_t;

static void make_quad(quad_t *q, vec3_t o, vec3_t eu, vec3_t ev, vec3_t n) {
    vec3_t c[4] = { o, vec3_add(o, eu), vec3_add(vec3_add(o, eu), ev), vec3_add(o, ev) };
    float uv[4][2] = { {0,0},{1,0},{1,1},{0,1} };
    for (int i = 0; i < 4; ++i) {
        q->pos[i*3]=c[i].x; q->pos[i*3+1]=c[i].y; q->pos[i*3+2]=c[i].z;
        q->nrm[i*3]=n.x; q->nrm[i*3+1]=n.y; q->nrm[i*3+2]=n.z;
        q->uv[i*2]=uv[i][0]; q->uv[i*2+1]=uv[i][1];
    }
    uint32_t idx[6] = { 0,1,2, 0,2,3 };
    memcpy(q->idx, idx, sizeof idx);
}

/* 2x2 solid-colour image. */
static void solid(uint8_t *buf, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 4; ++i) { buf[i*3]=r; buf[i*3+1]=g; buf[i*3+2]=b; }
}

static void set_mesh(lm_mesh_t *m, const quad_t *q, const lm_image_t *alb,
                     const lm_image_t *emi, vec3_t alb_tint, vec3_t emi_tint) {
    memset(m, 0, sizeof(*m));
    m->positions=q->pos; m->normals=q->nrm; m->uv0=q->uv; m->uv1=q->uv; m->indices=q->idx;
    m->vert_count=4; m->index_count=6;
    m->albedo_image=alb; m->emissive_image=emi;
    m->albedo=alb_tint; m->emissive=emi_tint;
    m->material=0; m->lightmap_resolution=16;
}

static float chan(const lm_luxel_t *lx, int c) { return lm_sh9_irradiance(&lx->sh[c], lx->normal); }

/* Find the floor luxel (normal +y) nearest world (x,z). */
static const lm_luxel_t *floor_near(const lm_mesh_bake_result_t *r, float x, float z) {
    float best=1e30f; const lm_luxel_t *bl=&r->combined.luxels[0];
    for (uint32_t i=0;i<r->n_luxels;++i){
        const lm_luxel_t *l=&r->combined.luxels[i];
        if (l->normal.y < 0.9f) continue; /* floor only */
        float d=(l->pos.x-x)*(l->pos.x-x)+(l->pos.z-z)*(l->pos.z-z);
        if (d<best){best=d;bl=l;}
    }
    return bl;
}

static int test_cornell(void) {
    static char buf[64 * 1024 * 1024];
    arena_t arena; arena_init(&arena, buf, sizeof(buf));
    const float S=5.0f;
    quad_t qf,qc,qb,ql,qr,qp;
    make_quad(&qf, (vec3_t){0,0,0}, (vec3_t){0,0,S}, (vec3_t){S,0,0}, (vec3_t){0,1,0});   /* floor */
    make_quad(&qc, (vec3_t){0,S,0}, (vec3_t){S,0,0}, (vec3_t){0,0,S}, (vec3_t){0,-1,0});   /* ceiling */
    make_quad(&qb, (vec3_t){0,0,0}, (vec3_t){S,0,0}, (vec3_t){0,S,0}, (vec3_t){0,0,1});    /* back */
    make_quad(&ql, (vec3_t){0,0,0}, (vec3_t){0,S,0}, (vec3_t){0,0,S}, (vec3_t){1,0,0});    /* left (red) */
    make_quad(&qr, (vec3_t){S,0,0}, (vec3_t){0,0,S}, (vec3_t){0,S,0}, (vec3_t){-1,0,0});   /* right (green) */
    make_quad(&qp, (vec3_t){1.7f,S-0.02f,1.7f}, (vec3_t){1.6f,0,0}, (vec3_t){0,0,1.6f}, (vec3_t){0,-1,0}); /* light */

    static uint8_t white[12], red[12], green[12], lit[12];
    solid(white,200,200,200); solid(red,200,30,25); solid(green,40,190,45); solid(lit,255,255,255);
    lm_image_t img_w={white,2,2,3,true}, img_r={red,2,2,3,true}, img_g={green,2,2,3,true}, img_l={lit,2,2,3,true};
    vec3_t one={1,1,1}, zero={0,0,0}, emit={14,14,12};

    lm_mesh_t meshes[6];
    set_mesh(&meshes[0], &qf, &img_w, NULL, one, zero);
    set_mesh(&meshes[1], &qc, &img_w, NULL, one, zero);
    set_mesh(&meshes[2], &qb, &img_w, NULL, one, zero);
    set_mesh(&meshes[3], &ql, &img_r, NULL, one, zero);
    set_mesh(&meshes[4], &qr, &img_g, NULL, one, zero);
    set_mesh(&meshes[5], &qp, NULL, &img_l, zero, emit);

    lm_material_t fb={{0,0,0},{0,0,0}};
    lm_mesh_scene_t scene={ meshes, 6, NULL, 0, { NULL, 0, fb } };
    lm_bake_config_t cfg={0};
    cfg.svo_bounds=(phys_aabb_t){{-0.5f,-0.5f,-0.5f},{5.5f,5.5f,5.5f}};
    cfg.svo_depth=6; cfg.atlas_width=512; cfg.atlas_padding=2; cfg.direct_samples=16;
    cfg.farfield_samples=0; cfg.solve.near_radius=10.0f; cfg.solve.max_shots=2000;
    cfg.solve.residual_epsilon=1e-3f; cfg.seed=7u;

    lm_mesh_bake_result_t res;
    ASSERT_TRUE(lm_mesh_bake(&scene,&cfg,&res,&arena));
    ASSERT_TRUE(res.n_luxels > 1000);

    /* Floor centre is lit by the panel. */
    const lm_luxel_t *fc = floor_near(&res, 2.5f, 2.5f);
    ASSERT_TRUE(chan(fc,0) > 0.2f);

    /* Colour bleed: floor near the red wall is redder than greener; vice versa. */
    const lm_luxel_t *fr = floor_near(&res, 0.4f, 2.5f); /* by the red wall */
    const lm_luxel_t *fg = floor_near(&res, 4.6f, 2.5f); /* by the green wall */
    printf("  floor@red  R=%.3f G=%.3f | floor@green R=%.3f G=%.3f\n",
           chan(fr,0),chan(fr,1),chan(fg,0),chan(fg,1));
    ASSERT_TRUE(chan(fr,0) > chan(fr,1));  /* red bleed */
    ASSERT_TRUE(chan(fg,1) > chan(fg,0));  /* green bleed */
    return 0;
}

int main(void) {
    printf("RUN  cornell\n");
    int r = test_cornell();
    printf(r == 0 ? "OK   cornell\nPASSED (0 failed)\n" : "FAIL cornell\nFAILED (1 failed)\n");
    return r ? 1 : 0;
}
