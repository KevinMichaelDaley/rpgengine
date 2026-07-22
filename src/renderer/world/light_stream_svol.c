/**
 * @file light_stream_svol.c
 * @brief STREAMED static-irradiance seed for the probe GI (rpg-zygg follow-up).
 *
 * The probes' static term samples a 3D irradiance field built from the baked
 * lightmap. The single-atlas path builds it once at load, but a streamed level
 * can be arbitrarily large -- so here the seed follows the STREAM: when a
 * lightmap chunk pages in, its splat POINTS (world position + cosine
 * irradiance from SH0..3 at each lightmapped vertex) are extracted on the
 * load fiber and kept with the chunk's residency slot; a fixed-size window
 * volume centred on the camera re-splats the resident point lists whenever a
 * chunk arrives or the camera walks far enough to recentre. Bounded memory
 * regardless of world size, exactly like every other streamed light asset.
 *
 * Ownership: the window state + per-chunk point lists are owned here (freed
 * by evict/destroy); the scene descriptor is BORROWED (must outlive the
 * streamer). The gi_static_volume_t passed to tick is the caller's; on a
 * rebuild it is destroyed + re-uploaded and the caller re-points gi_runtime.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferrum/renderer/light_stream.h"
#include "light_stream_internal.h"
#include "ferrum/renderer/gi/gi_static_volume.h"
#include "ferrum/scene/scene_desc.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/job/system.h"
#include "ferrum/job/counter.h"

/* Window defaults: 96 x 40 x 96 cells at 1 m -- covers a city block around the
 * camera; ~5 MB RGB float upload per rebuild. */
#define SVOL_DX 96
#define SVOL_DY 40
#define SVOL_DZ 96
#define SVOL_VOX_DEFAULT 1.0f
#define SVOL_MAX_PTS_PER_CHUNK 16384u


/* WORKER JOB: splat the snapshot into the window, average + one dilation
 * pass -> sv->rgb. Touches only job-owned scratch + the snapshot lists. */
static void svol_rebuild_job(void *user)
{
    ls_svol_t *sv = user;
    size_t cells = (size_t)sv->dims[0] * sv->dims[1] * sv->dims[2];
    memset(sv->sum, 0, cells * 3u * sizeof(float));
    memset(sv->cnt, 0, cells * sizeof(uint32_t));
    memset(sv->rgb, 0, cells * 3u * sizeof(float));
    for (uint32_t c = 0; c < sv->n_snap; ++c) {
        const ls_svol_snap_t *sn = &sv->snap[c];
        for (uint32_t p = 0; p < sn->np; ++p) {
            const float *pt = &sn->pts[p*6];
            int cx = (int)((pt[0]-sv->job_org[0])/sv->vox);
            int cy = (int)((pt[1]-sv->job_org[1])/sv->vox);
            int cz = (int)((pt[2]-sv->job_org[2])/sv->vox);
            if (cx<0||cy<0||cz<0||cx>=sv->dims[0]||cy>=sv->dims[1]||cz>=sv->dims[2]) continue;
            size_t ci = ((size_t)cz*sv->dims[1] + cy)*sv->dims[0] + cx;
            sv->sum[ci*3+0]+=pt[3]; sv->sum[ci*3+1]+=pt[4]; sv->sum[ci*3+2]+=pt[5];
            sv->cnt[ci]++;
        }
    }
    for (size_t c = 0; c < cells; ++c)
        if (sv->cnt[c]) { float inv = 1.0f/(float)sv->cnt[c];
            sv->rgb[c*3+0]=sv->sum[c*3+0]*inv; sv->rgb[c*3+1]=sv->sum[c*3+1]*inv;
            sv->rgb[c*3+2]=sv->sum[c*3+2]*inv; }
    for (int z = 0; z < sv->dims[2]; ++z) for (int y = 0; y < sv->dims[1]; ++y)
    for (int x = 0; x < sv->dims[0]; ++x) {
        size_t ci = ((size_t)z*sv->dims[1]+y)*sv->dims[0]+x;
        if (sv->cnt[ci]) continue;
        float acc[3] = {0,0,0}; int nn = 0;
        static const int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int oo = 0; oo < 6; ++oo) {
            int ax=x+off[oo][0], ay=y+off[oo][1], az=z+off[oo][2];
            if (ax<0||ay<0||az<0||ax>=sv->dims[0]||ay>=sv->dims[1]||az>=sv->dims[2]) continue;
            size_t nci = ((size_t)az*sv->dims[1]+ay)*sv->dims[0]+ax;
            if (!sv->cnt[nci]) continue;
            acc[0]+=sv->rgb[nci*3+0]; acc[1]+=sv->rgb[nci*3+1]; acc[2]+=sv->rgb[nci*3+2]; ++nn;
        }
        if (nn) { sv->rgb[ci*3+0]=acc[0]/nn; sv->rgb[ci*3+1]=acc[1]/nn; sv->rgb[ci*3+2]=acc[2]/nn; }
    }
}

bool client_ls_svol_init(client_light_stream_t *ls,
                         const struct scene_desc *desc, const char *base_dir,
                         struct job_system *jobs)
{
    if (ls == NULL || desc == NULL || base_dir == NULL) return false;
    ls_svol_t *sv = calloc(1, sizeof *sv);
    if (sv == NULL) return false;
    sv->desc = desc;
    sv->jobs = jobs;
    snprintf(sv->base_dir, sizeof sv->base_dir, "%s", base_dir);
    sv->dims[0] = SVOL_DX; sv->dims[1] = SVOL_DY; sv->dims[2] = SVOL_DZ;
    sv->vox = getenv("GI_SVOX") ? (float)atof(getenv("GI_SVOX")) : SVOL_VOX_DEFAULT;
    if (sv->vox < 0.1f) sv->vox = 0.1f;
    size_t cells = (size_t)sv->dims[0] * sv->dims[1] * sv->dims[2];
    sv->sum = calloc(cells * 3u, sizeof(float));
    sv->cnt = calloc(cells, sizeof(uint32_t));
    sv->rgb = calloc(cells * 3u, sizeof(float));
    sv->snap = calloc(ls->n_chunks ? ls->n_chunks : 1u, sizeof *sv->snap);
    if (sv->sum == NULL || sv->cnt == NULL || sv->rgb == NULL || sv->snap == NULL) {
        free(sv->sum); free(sv->cnt); free(sv->rgb); free(sv->snap); free(sv);
        return false;
    }
    job_counter_init(&sv->jc, 0);
    sv->jc_ready = 1;
    ls->svol = sv;
    return true;
}

void client_ls_svol_destroy(client_light_stream_t *ls)
{
    if (ls == NULL || ls->svol == NULL) return;
    ls_svol_t *sv = ls->svol;
    if (sv->job_running && sv->jc_ready) {
        /* Drain the in-flight rebuild, BOUNDED: if the job system was already
         * shut down the counter never decrements -- after ~2 s give up and
         * LEAK the svol block instead of hanging shutdown forever. */
        int spins = 0;
        while (job_counter_value(&sv->jc) != 0 && spins < 2000) {
            struct timespec ts = { 0, 1000000 };   /* 1 ms */
            nanosleep(&ts, NULL);
            ++spins;
        }
        if (job_counter_value(&sv->jc) != 0) {
            fprintf(stderr, "light_stream_svol: rebuild job never completed "
                    "(job system stopped first?) -- leaking the window\n");
            ls->svol = NULL;
            return;
        }
    }
    if (sv->jc_ready) job_counter_destroy(&sv->jc);
    for (uint32_t i = 0; i < sv->n_grave; ++i) free(sv->grave[i]);
    free(sv->grave);
    free(sv->snap);
    free(sv->sum); free(sv->cnt); free(sv->rgb);
    free(sv);
    ls->svol = NULL;
}

/* JOB FIBER (from client_ls_load, after the SH decode): extract this chunk's
 * splat points -- for every mesh assigned to the chunk, read its fvma and
 * evaluate cosine irradiance from SH0..3 at each lightmapped vertex. Pure CPU
 * + file IO, no GL; the point list rides the slot to the render thread. */
void client_ls_svol_extract(client_light_stream_t *ls, lm_chunk_slot_t *s)
{
    if (ls == NULL || ls->svol == NULL || s == NULL || s->coeff[0] == NULL)
        return;
    const ls_svol_t *sv = ls->svol;
    const scene_desc_t *desc = sv->desc;
    uint32_t aw = (uint32_t)s->w, ah = (uint32_t)s->h;
    if (aw == 0 || ah == 0) return;

    /* Count lightmapped verts of this chunk's meshes first for one alloc. */
    float *pts = NULL;
    uint32_t np = 0, cap = 0;

    for (uint32_t i = 0; i < desc->object_count && i < ls->n_meshes; ++i) {
        if (ls->mchunk[i] != (int)s->chunk_id || ls->mrect[i].w == 0) continue;
        const scene_desc_object_t *o = &desc->objects[i];
        char path[600]; snprintf(path, sizeof path, "%s/%s", sv->base_dir, o->mesh);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *bytes = (fl > 0) ? malloc((size_t)fl) : NULL;
        if (bytes == NULL || fread(bytes, 1, (size_t)fl, f) != (size_t)fl) {
            free(bytes); fclose(f); continue;
        }
        fclose(f);
        mesh_slot_t slot; memset(&slot, 0, sizeof slot);
        bool ok = mesh_vao_deserialize(bytes, (size_t)fl, &slot);
        free(bytes);
        if (!ok) continue;
        if (slot.uvs[1] == NULL || slot.normals == NULL) { mesh_slot_destroy(&slot); continue; }

        /* TRS -> column-major model. */
        float mm[16]; memset(mm, 0, sizeof mm); mm[15] = 1.0f;
        {
            const float *p = o->position, *q = o->rotation, *sc = o->scale;
            float x = q[0], y = q[1], z = q[2], w = q[3];
            mm[0] = (1-2*(y*y+z*z))*sc[0]; mm[1] = (2*(x*y+z*w))*sc[0]; mm[2] = (2*(x*z-y*w))*sc[0];
            mm[4] = (2*(x*y-z*w))*sc[1]; mm[5] = (1-2*(x*x+z*z))*sc[1]; mm[6] = (2*(y*z+x*w))*sc[1];
            mm[8] = (2*(x*z+y*w))*sc[2]; mm[9] = (2*(y*z-x*w))*sc[2]; mm[10] = (1-2*(x*x+y*y))*sc[2];
            mm[12] = p[0]; mm[13] = p[1]; mm[14] = p[2];
        }
        const lm_atlas_rect_t *rc = &ls->mrect[i];
        uint32_t stride = slot.vertex_count > SVOL_MAX_PTS_PER_CHUNK
                              ? slot.vertex_count / SVOL_MAX_PTS_PER_CHUNK + 1u : 1u;
        for (uint32_t v = 0; v < slot.vertex_count; v += stride) {
            float u0 = slot.uvs[1][v*2], v0 = slot.uvs[1][v*2+1];
            if (u0 == 0.0f && v0 == 0.0f) continue;
            float au = ((float)rc->x + u0 * (float)rc->w) / (float)aw;
            float av = ((float)rc->y + v0 * (float)rc->h) / (float)ah;
            int px = (int)(au * (float)aw), py = (int)(av * (float)ah);
            if (px < 0) px = 0; else if (px >= (int)aw) px = (int)aw - 1;
            if (py < 0) py = 0; else if (py >= (int)ah) py = (int)ah - 1;
            size_t sp = ((size_t)py * aw + px) * 3;
            const float *ln = &slot.normals[v*3];
            float nx = mm[0]*ln[0]+mm[4]*ln[1]+mm[8]*ln[2];
            float ny = mm[1]*ln[0]+mm[5]*ln[1]+mm[9]*ln[2];
            float nz = mm[2]*ln[0]+mm[6]*ln[1]+mm[10]*ln[2];
            float il = sqrtf(nx*nx+ny*ny+nz*nz);
            if (il > 1e-8f) { nx/=il; ny/=il; nz/=il; }
            float b0 = 0.282094792f, b1 = 0.488602512f*ny,
                  b2 = 0.488602512f*nz, b3 = 0.488602512f*nx;
            float E[3];
            for (int c = 0; c < 3; ++c) {
                float e = 3.14159265f * s->coeff[0][sp+c] * b0
                        + 2.09439510f * (s->coeff[1][sp+c]*b1 +
                                         s->coeff[2][sp+c]*b2 +
                                         s->coeff[3][sp+c]*b3);
                E[c] = e > 0.0f ? e : 0.0f;
            }
            const float *lp = &slot.positions[v*3];
            if (np == cap) {
                cap = cap ? cap * 2u : 4096u;
                float *nb = realloc(pts, (size_t)cap * 6u * sizeof(float));
                if (nb == NULL) { free(pts); pts = NULL; np = 0; mesh_slot_destroy(&slot); goto done; }
                pts = nb;
            }
            pts[np*6+0] = mm[0]*lp[0]+mm[4]*lp[1]+mm[8]*lp[2]+mm[12];
            pts[np*6+1] = mm[1]*lp[0]+mm[5]*lp[1]+mm[9]*lp[2]+mm[13];
            pts[np*6+2] = mm[2]*lp[0]+mm[6]*lp[1]+mm[10]*lp[2]+mm[14];
            pts[np*6+3] = E[0]; pts[np*6+4] = E[1]; pts[np*6+5] = E[2];
            ++np;
        }
        mesh_slot_destroy(&slot);
    }
done:
    /* Handoff to the render thread: count BEFORE pointer (the reader gates on
     * the pointer), and never free a potentially-live list here -- on a
     * re-page-in the previous list was already dropped by the RAM evict. */
    if (s->svol_pts == NULL) {
        s->svol_np = np;
        s->svol_pts = pts;
    } else {
        free(pts);
    }
    if (np > 0 && ls->svol != NULL)
        ((ls_svol_t *)ls->svol)->dirty = 1;   /* benign cross-thread flag. */
}

/* RENDER THREAD: recentre the window on the camera when it strays past a
 * quarter extent, and rebuild (re-splat every resident chunk's point list,
 * average, one dilation pass, re-upload) when dirty. Returns 1 if @p vol was
 * re-uploaded (the caller must re-point gi_runtime at the new texture). */
int client_ls_svol_tick(client_light_stream_t *ls, const float cam_pos[3],
                        gi_static_volume_t *vol)
{
    if (ls == NULL || ls->svol == NULL || cam_pos == NULL || vol == NULL)
        return 0;
    ls_svol_t *sv = ls->svol;
    if (sv->cooldown > 0)
        --sv->cooldown;

    /* Harvest a finished rebuild: the only GL work on the render thread is one
     * subimage upload of the fixed-size window. */
    if (sv->job_running && sv->jc_ready && job_counter_value(&sv->jc) == 0) {
        sv->job_running = 0;
        sv->job_uploaded = 1;
    }
    int changed = 0;
    if (sv->job_uploaded) {
        sv->job_uploaded = 0;
        if (gi_static_volume_refresh(vol, sv->rgb, sv->dims, sv->job_org, sv->vox))
            changed = 1;
    }

    /* Frees happen ONLY here and only with no job in flight, which is what
     * makes the job's pointer snapshot safe. */
    if (!sv->job_running) {
        for (uint32_t i = 0; i < sv->n_grave; ++i) free(sv->grave[i]);
        sv->n_grave = 0;
    }

    /* Recentre when the camera strays past a quarter extent. Deferred while a
     * job runs (its snapshot splats against job_org). */
    float ext[3] = { sv->dims[0] * sv->vox, sv->dims[1] * sv->vox,
                     sv->dims[2] * sv->vox };
    if (!sv->job_running &&
        (!sv->have_center ||
         fabsf(cam_pos[0] - sv->center[0]) > ext[0] * 0.25f ||
         fabsf(cam_pos[1] - sv->center[1]) > ext[1] * 0.25f ||
         fabsf(cam_pos[2] - sv->center[2]) > ext[2] * 0.25f)) {
        for (int a = 0; a < 3; ++a) {
            /* Snap to the voxel grid so recentres don't shimmer the field. */
            sv->center[a] = floorf(cam_pos[a] / sv->vox) * sv->vox;
            sv->org[a] = sv->center[a] - ext[a] * 0.5f;
        }
        if (!sv->have_center) sv->have_center = 1;
        sv->dirty = 1;
        sv->cooldown = 0;            /* a recentre must not wait out the cooldown. */
    }

    /* Kick a rebuild: dirty + no job in flight + cooldown expired (page-ins
     * mark dirty constantly during warm-up; one rebuild per ~2 s amortises). */
    if (!sv->dirty || sv->job_running || sv->cooldown > 0)
        return changed;
    sv->dirty = 0;
    sv->cooldown = 120;

    /* Snapshot the resident point lists (pointers only -- frees are gated on
     * job completion above) + the window origin. */
    lm_chunk_slot_t *slots = ls->slots;
    sv->n_snap = 0;
    for (uint32_t c = 0; c < ls->n_chunks; ++c) {
        const float *pts = slots[c].svol_pts;
        if (pts == NULL) continue;
        sv->snap[sv->n_snap].pts = pts;
        sv->snap[sv->n_snap].np = slots[c].svol_np;
        ++sv->n_snap;
    }
    memcpy(sv->job_org, sv->org, sizeof sv->job_org);
    if (sv->jobs != NULL) {
        job_counter_init(&sv->jc, 0);
        if (job_dispatch(sv->jobs, svol_rebuild_job, sv, 0, &sv->jc) != JOB_ID_INVALID) {
            sv->job_running = 1;
        } else {
            svol_rebuild_job(sv);    /* queue full: degrade to inline. */
            sv->job_uploaded = 1;
        }
    } else {
        svol_rebuild_job(sv);        /* no job system (headless/tests). */
        sv->job_uploaded = 1;
    }
    return changed;
}
