/**
 * @file obj_mesh_load.c
 * @brief Indexed OBJ loader with positions, normals and UVs (see obj_loader.h).
 *
 * Each unique v/vt/vn triple becomes one vertex (deduplicated via an open-
 * addressing hash), polygons are fan-triangulated, and per-position smooth
 * normals are generated when the file has none.
 */
#include "ferrum/mesh/obj_loader.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBJ_MAX_FACE_VERTS 64

/* Parse "v", "v/t", "v//n", or "v/t/n" into 1-based (or negative) indices. */
static void obj_parse_ref(const char *tok, long *vi, long *ti, long *ni)
{
    char *e = NULL;
    *vi = *ti = *ni = 0;
    *vi = strtol(tok, &e, 10);
    if (*e == '/') {
        ++e;
        if (*e != '/') *ti = strtol(e, &e, 10);
        if (*e == '/') { ++e; *ni = strtol(e, &e, 10); }
    }
}

/* Resolve a possibly-negative 1-based OBJ index to 0-based, or -1 if absent. */
static long obj_resolve(long idx, uint32_t count)
{
    if (idx > 0) return idx - 1;
    if (idx < 0) return (long)count + idx;
    return -1;
}

/* Split the body of an "f " line into tokens; returns the token count. */
static int obj_tokenize(char *body, char *toks[OBJ_MAX_FACE_VERTS])
{
    int n = 0;
    char *save = NULL;
    for (char *t = strtok_r(body, " \t\r\n", &save);
         t && n < OBJ_MAX_FACE_VERTS; t = strtok_r(NULL, " \t\r\n", &save)) {
        toks[n++] = t;
    }
    return n;
}

static uint32_t obj_pow2_ceil(uint32_t x)
{
    uint32_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

static uint32_t obj_hash_slot(uint64_t key, uint32_t mask)
{
    return (uint32_t)((key * 11400714819323198485ull) >> 40) & mask;
}

void obj_mesh_free(obj_mesh_t *mesh)
{
    if (mesh == NULL) return;
    free(mesh->positions); free(mesh->normals); free(mesh->tangents);
    free(mesh->uvs); free(mesh->uvs1); free(mesh->indices);
    memset(mesh, 0, sizeof(*mesh));
}

/* Generate per-vertex tangents (vec4, w = handedness) from positions + uvs +
 * normals, for tangent-space normal mapping. Accumulates per-triangle tangent/
 * bitangent, then Gram-Schmidt orthonormalises against the vertex normal. */
int obj_mesh_gen_tangents(obj_mesh_t *m)
{
    m->tangents = malloc((size_t)m->vert_count * 4 * sizeof(float));
    float *bit = calloc((size_t)m->vert_count * 3, sizeof(float));
    if (!m->tangents || !bit) { free(bit); return -1; }
    for (uint32_t i = 0; i < m->vert_count * 4; ++i) m->tangents[i] = 0.0f;
    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        uint32_t a = m->indices[i], b = m->indices[i+1], c = m->indices[i+2];
        float *p0=&m->positions[a*3], *p1=&m->positions[b*3], *p2=&m->positions[c*3];
        float *u0=&m->uvs[a*2], *u1=&m->uvs[b*2], *u2=&m->uvs[c*2];
        float e1[3]={p1[0]-p0[0],p1[1]-p0[1],p1[2]-p0[2]};
        float e2[3]={p2[0]-p0[0],p2[1]-p0[1],p2[2]-p0[2]};
        float du1=u1[0]-u0[0], dv1=u1[1]-u0[1], du2=u2[0]-u0[0], dv2=u2[1]-u0[1];
        float det = du1*dv2 - du2*dv1;
        float r = (fabsf(det) > 1e-12f) ? 1.0f/det : 0.0f;
        float t[3]={(e1[0]*dv2 - e2[0]*dv1)*r, (e1[1]*dv2 - e2[1]*dv1)*r, (e1[2]*dv2 - e2[2]*dv1)*r};
        float bt[3]={(e2[0]*du1 - e1[0]*du2)*r, (e2[1]*du1 - e1[1]*du2)*r, (e2[2]*du1 - e1[2]*du2)*r};
        uint32_t ix[3]={a,b,c};
        for (int j=0;j<3;++j){ for(int k=0;k<3;++k){ m->tangents[ix[j]*4+k]+=t[k]; bit[ix[j]*3+k]+=bt[k]; } }
    }
    for (uint32_t v=0; v<m->vert_count; ++v) {
        float *n=&m->normals[v*3], *tg=&m->tangents[v*4], *bt=&bit[v*3];
        float ndott = n[0]*tg[0]+n[1]*tg[1]+n[2]*tg[2];
        float o[3]={tg[0]-n[0]*ndott, tg[1]-n[1]*ndott, tg[2]-n[2]*ndott};
        float len = sqrtf(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]);
        if (len>1e-8f){ o[0]/=len;o[1]/=len;o[2]/=len; } else { o[0]=1;o[1]=0;o[2]=0; }
        /* handedness: sign of dot(cross(n,t), bitangent). */
        float cr[3]={n[1]*o[2]-n[2]*o[1], n[2]*o[0]-n[0]*o[2], n[0]*o[1]-n[1]*o[0]};
        float w = (cr[0]*bt[0]+cr[1]*bt[1]+cr[2]*bt[2]) < 0.0f ? -1.0f : 1.0f;
        tg[0]=o[0]; tg[1]=o[1]; tg[2]=o[2]; tg[3]=w;
    }
    free(bit);
    return 0;
}

int obj_mesh_load(const char *path, float scale, obj_mesh_t *out)
{
    if (path == NULL || out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    /* Pass 1: count v / vt / vn / triangles. */
    char line[1024];
    uint32_t nv = 0, nvt = 0, nvn = 0, ntri = 0;
    while (fgets(line, (int)sizeof(line), fp)) {
        if (line[0]=='v' && line[1]==' ') ++nv;
        else if (line[0]=='v' && line[1]=='t') ++nvt;
        else if (line[0]=='v' && line[1]=='n') ++nvn;
        else if (line[0]=='f' && line[1]==' ') {
            char tmp[1024]; char *toks[OBJ_MAX_FACE_VERTS];
            memcpy(tmp, line+2, strlen(line+2)+1);
            int k = obj_tokenize(tmp, toks);
            if (k >= 3) ntri += (uint32_t)(k - 2);
        }
    }
    if (ntri == 0) { fclose(fp); return -1; }

    /* Temp source arrays + output arrays (unique verts <= ntri*3). */
    uint32_t vcap = ntri * 3u, icap = ntri * 3u;
    float *pos = malloc((size_t)nv * 3 * sizeof(float));
    float *uv = nvt ? malloc((size_t)nvt * 2 * sizeof(float)) : NULL;
    float *nrm = nvn ? malloc((size_t)nvn * 3 * sizeof(float)) : NULL;
    out->positions = malloc((size_t)vcap * 3 * sizeof(float));
    out->normals   = malloc((size_t)vcap * 3 * sizeof(float));
    out->uvs       = malloc((size_t)vcap * 2 * sizeof(float));
    out->uvs1      = calloc((size_t)vcap * 2, sizeof(float)); /* OBJ has no 2nd UV */
    out->indices   = malloc((size_t)icap * sizeof(uint32_t));
    uint32_t hcap = obj_pow2_ceil(vcap * 2u + 8u), hmask = hcap - 1u;
    uint64_t *hkeys = calloc(hcap, sizeof(uint64_t));
    uint32_t *hvals = malloc((size_t)hcap * sizeof(uint32_t));
    if (!pos || !out->positions || !out->normals || !out->uvs || !out->uvs1 ||
        !out->indices ||
        !hkeys || !hvals || (nvt && !uv) || (nvn && !nrm)) {
        free(pos); free(uv); free(nrm); free(hkeys); free(hvals);
        obj_mesh_free(out); fclose(fp); return -1;
    }

    /* Pass 2: fill temp arrays + build the indexed mesh. */
    rewind(fp);
    uint32_t vi=0, vti=0, vni=0, uverts=0, uidx=0;
    while (fgets(line, (int)sizeof(line), fp)) {
        if (line[0]=='v' && line[1]==' ') {
            float x=0,y=0,z=0; sscanf(line+2, "%f %f %f", &x,&y,&z);
            pos[vi*3]=x*scale; pos[vi*3+1]=y*scale; pos[vi*3+2]=z*scale; ++vi;
        } else if (line[0]=='v' && line[1]=='t') {
            float u=0,v=0; sscanf(line+2, "%f %f", &u,&v);
            uv[vti*2]=u; uv[vti*2+1]=v; ++vti;
        } else if (line[0]=='v' && line[1]=='n') {
            float x=0,y=0,z=0; sscanf(line+2, "%f %f %f", &x,&y,&z);
            nrm[vni*3]=x; nrm[vni*3+1]=y; nrm[vni*3+2]=z; ++vni;
        } else if (line[0]=='f' && line[1]==' ') {
            char tmp[1024]; char *toks[OBJ_MAX_FACE_VERTS];
            memcpy(tmp, line+2, strlen(line+2)+1);
            int k = obj_tokenize(tmp, toks);
            if (k < 3) continue;
            uint32_t fanidx[OBJ_MAX_FACE_VERTS];
            for (int t = 0; t < k; ++t) {
                long a,b,c; obj_parse_ref(toks[t], &a,&b,&c);
                long pa = obj_resolve(a, nv), ta = obj_resolve(b, nvt), na = obj_resolve(c, nvn);
                if (pa < 0) { fanidx[t] = 0; continue; }
                uint64_t key = (uint64_t)(pa+1) | ((uint64_t)(ta+1) << 21) | ((uint64_t)(na+1) << 42);
                uint32_t h = obj_hash_slot(key, hmask), found = 0xFFFFFFFFu;
                while (hkeys[h]) { if (hkeys[h]==key) { found = hvals[h]; break; } h=(h+1)&hmask; }
                if (found == 0xFFFFFFFFu) {
                    found = uverts++;
                    hkeys[h]=key; hvals[h]=found;
                    memcpy(&out->positions[found*3], &pos[pa*3], 3*sizeof(float));
                    if (ta>=0 && uv) { out->uvs[found*2]=uv[ta*2]; out->uvs[found*2+1]=uv[ta*2+1]; }
                    else { out->uvs[found*2]=0; out->uvs[found*2+1]=0; }
                    if (na>=0 && nrm) memcpy(&out->normals[found*3], &nrm[na*3], 3*sizeof(float));
                    else { out->normals[found*3]=0; out->normals[found*3+1]=0; out->normals[found*3+2]=0; }
                }
                fanidx[t] = found;
            }
            for (int t = 1; t + 1 < k; ++t) {
                out->indices[uidx++] = fanidx[0];
                out->indices[uidx++] = fanidx[t];
                out->indices[uidx++] = fanidx[t+1];
            }
        }
    }
    fclose(fp);
    out->vert_count = uverts; out->index_count = uidx;

    /* Generate smooth normals when the file had none. */
    if (nvn == 0) {
        for (uint32_t i = 0; i < uidx; i += 3) {
            uint32_t i0=out->indices[i], i1=out->indices[i+1], i2=out->indices[i+2];
            float *p0=&out->positions[i0*3], *p1=&out->positions[i1*3], *p2=&out->positions[i2*3];
            float e1[3]={p1[0]-p0[0],p1[1]-p0[1],p1[2]-p0[2]};
            float e2[3]={p2[0]-p0[0],p2[1]-p0[1],p2[2]-p0[2]};
            float fn[3]={e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
            uint32_t ix[3]={i0,i1,i2};
            for (int j=0;j<3;++j) for (int c=0;c<3;++c) out->normals[ix[j]*3+c]+=fn[c];
        }
        for (uint32_t v=0; v<uverts; ++v) {
            float *n=&out->normals[v*3]; float len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            if (len>1e-8f) { n[0]/=len; n[1]/=len; n[2]/=len; } else { n[2]=1.0f; }
        }
    }
    free(pos); free(uv); free(nrm); free(hkeys); free(hvals);
    if (obj_mesh_gen_tangents(out) != 0) { obj_mesh_free(out); return -1; }
    return 0;
}
