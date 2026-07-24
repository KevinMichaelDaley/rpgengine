/**
 * @file client_scene_load.c
 * @brief Descriptor-driven client scene load + render + teardown (rpg-8302):
 *        the reusable counterpart of hall_lit_dynamic.c's inline assembly.
 */
#include <glad/glad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_scene.h"
#include "ferrum/scene/scene_desc.h"
#include "ferrum/scene/render_config.h"
#include "ferrum/renderer/gi/gi_sdf_stream.h"
#include "ferrum/probe/probe_place.h"
#include "ferrum/probe/probe_sh_file.h"
#include "ferrum/memory/arena.h"
#include "ferrum/lightmap/lm_atlas.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"

/* ── Small helpers ─────────────────────────────────────────────── */

/* Column-major model matrix from translation/quaternion/scale. */
static void model_from_trs(const float p[3], const float q[4], const float s[3],
                           float m[16])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0]  = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0];
    m[2]  = (2 * (x * z - y * w)) * s[0];     m[3] = 0;
    m[4]  = (2 * (x * y - z * w)) * s[1];     m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6]  = (2 * (y * z + x * w)) * s[1];     m[7] = 0;
    m[8]  = (2 * (x * z + y * w)) * s[2];     m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2]; m[11] = 0;
    m[12] = p[0]; m[13] = p[1]; m[14] = p[2]; m[15] = 1;
}

/* Read a whole file into a malloc'd buffer (caller frees). NULL on error. */
static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz > 0 && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)sz);
            if (buf != NULL && fread(buf, 1, (size_t)sz, f) == (size_t)sz)
                *out_size = (size_t)sz;
            else { free(buf); buf = NULL; }
        }
    }
    fclose(f);
    return buf;
}

/* Load a mesh (.fvma) into a GPU static mesh, remapping its local lightmap uv1
 * into the mesh's atlas rect so the shared SH atlas samples correctly (the
 * forward pass has no per-mesh rect uniform -- uv1 must already be atlas-space,
 * exactly as hall_lit_dynamic remaps its .dmesh uv1). @p rect may be NULL / zero
 * (mesh has no lightmap rect) in which case uv1 is left untouched. Returns 0 on
 * success. */
/* Group the mesh's triangles by polygroup id into contiguous submeshes, each
 * carrying a GLOBAL material index (via @p mat_map: polygroup -> global, from
 * the object's material_idx). Reorders @p src_idx into @p out_idx so each
 * group is contiguous, fills @p out_subs, returns the submesh count (0 -> use
 * a single submesh). @p out_idx must hold index_count uints; @p out_subs must
 * hold (max_polygroup+1) entries -- both sized generously by the caller. */
static uint32_t build_submeshes(const uint16_t *polygroup_ids, uint32_t face_count,
                                const uint32_t *src_idx, uint32_t index_count,
                                const int32_t *mat_map, uint32_t n_map,
                                uint32_t *out_idx, render_submesh_t *out_subs,
                                uint32_t max_subs)
{
    if (polygroup_ids == NULL || face_count == 0 || mat_map == NULL || n_map == 0)
        return 0;
    uint16_t maxpg = 0;
    for (uint32_t f = 0; f < face_count; ++f)
        if (polygroup_ids[f] > maxpg) maxpg = polygroup_ids[f];
    uint32_t ngroups = (uint32_t)maxpg + 1u;
    if (ngroups > max_subs) return 0;   /* caller's buckets too small: fall back. */
    /* Per-group face counts, then face offsets (prefix sum). */
    uint32_t gcount[256] = { 0 };
    if (ngroups > 256) return 0;
    for (uint32_t f = 0; f < face_count; ++f) gcount[polygroup_ids[f]]++;
    uint32_t goff[256]; uint32_t acc = 0;
    for (uint32_t g = 0; g < ngroups; ++g) { goff[g] = acc; acc += gcount[g]; }
    /* Scatter faces into their group's contiguous run. */
    uint32_t cursor[256];
    for (uint32_t g = 0; g < ngroups; ++g) cursor[g] = goff[g];
    for (uint32_t f = 0; f < face_count; ++f) {
        uint16_t g = polygroup_ids[f];
        uint32_t d = cursor[g]++;
        out_idx[d*3+0] = src_idx[f*3+0];
        out_idx[d*3+1] = src_idx[f*3+1];
        out_idx[d*3+2] = src_idx[f*3+2];
    }
    (void)index_count;
    /* One submesh per non-empty group, material_slot = global material index. */
    uint32_t si = 0;
    for (uint32_t g = 0; g < ngroups; ++g) {
        if (gcount[g] == 0) continue;
        int32_t global = (g < n_map) ? mat_map[g] : mat_map[0];
        if (global < 0) global = (mat_map[0] >= 0) ? mat_map[0] : 0;
        out_subs[si].index_offset = goff[g] * 3u;
        out_subs[si].index_count = gcount[g] * 3u;
        out_subs[si].material_slot = (uint16_t)global;
        ++si;
    }
    return si;
}

static int load_mesh(const gl_loader_t *loader, const char *path,
                     const lm_atlas_rect_t *rect, const lm_atlas_t *atlas,
                     const int32_t *mat_map, uint32_t n_map, static_mesh_t *out)
{
    size_t sz = 0;
    uint8_t *bytes = read_file(path, &sz);
    if (bytes == NULL) return -1;

    mesh_slot_t slot;
    memset(&slot, 0, sizeof slot);
    if (!mesh_vao_deserialize(bytes, sz, &slot)) { free(bytes); return -1; }
    free(bytes);

    /* Remap uv1 (surface-local [0,1]) into the atlas rect, in place. */
    if (rect != NULL && rect->w > 0 && atlas != NULL && atlas->width > 0 &&
        slot.uvs[1] != NULL) {
        for (uint32_t v = 0; v < slot.vertex_count; ++v) {
            float au, av;
            lm_atlas_remap_uv(rect, atlas, slot.uvs[1][v * 2], slot.uvs[1][v * 2 + 1],
                              &au, &av);
            slot.uvs[1][v * 2] = au;
            slot.uvs[1][v * 2 + 1] = av;
        }
    }

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof info);
    info.positions = slot.positions; info.normals = slot.normals;
    info.tangents = slot.tangents;   info.uv0 = slot.uvs[0];
    info.uv1 = slot.uvs[1];          info.colors = slot.colors;
    info.indices = slot.indices;     info.vertex_count = slot.vertex_count;
    info.index_count = slot.index_count;

    /* Split the mesh into per-polygroup submeshes so its walls/glass/signs
     * each shade with their OWN material (the FVMA stores a polygroup id per
     * face; without this every multi-material building rendered as one flat
     * material). Falls back to a single submesh when there are no polygroups. */
    uint32_t face_count = slot.index_count / 3u;
    uint32_t *reidx = NULL;
    render_submesh_t *subs = NULL;
    if (slot.polygroup_ids != NULL && face_count > 0 && mat_map != NULL && n_map > 0) {
        reidx = malloc((size_t)slot.index_count * sizeof(uint32_t));
        subs = malloc(256u * sizeof(render_submesh_t));
        if (reidx != NULL && subs != NULL) {
            uint32_t nsub = build_submeshes(slot.polygroup_ids, face_count,
                                            slot.indices, slot.index_count,
                                            mat_map, n_map, reidx, subs, 256u);
            if (nsub > 0) {
                info.indices = reidx;
                info.submeshes = subs;
                info.submesh_count = nsub;
            }
        }
    }

    int rc = static_mesh_create(loader, &info, out);
    free(reidx);
    free(subs);
    mesh_slot_destroy(&slot);
    return rc;
}

/* Decode + upload one material texture into cs->textures[slot_idx]; point the
 * material map at it. Missing files are skipped (map stays NULL). */
static void load_material_tex(client_scene_t *cs, render_material_t *mat,
                              int map_slot, const char *base_dir, const char *rel,
                              texture_format_t fmt, client_image_load_fn image_load)
{
    if (rel == NULL || rel[0] == '\0' || image_load == NULL) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", base_dir, rel);
    int w = 0, h = 0; unsigned char *px = NULL;
    if (!image_load(path, &w, &h, &px) || px == NULL) return;
    texture_t *tex = &cs->textures[cs->texture_count];
    if (texture_create(tex, cs->loader) == TEXTURE_OK &&
        texture_upload_2d(tex, fmt, (uint32_t)w, (uint32_t)h, px, true) == TEXTURE_OK) {
        texture_set_sampler(tex, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT);
        /* ANISOTROPIC filtering. Trilinear alone selects the mip from the WORST-axis
         * footprint, so any surface seen at a grazing angle -- roof rafters, a floor
         * receding down the hall -- collapses to a far too blurry mip even though the
         * texture and its tiling are correct. Anisotropy samples along the elongated
         * axis instead and is the single biggest sharpness win here. Clamp to the
         * driver's max; FR_ANISO=<n> overrides (1 = off). */
        {
            GLfloat maxa = 1.0f;
            glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */, &maxa);
            float want = cs->aniso > 0.0f ? cs->aniso : 16.0f;
            const char *e = getenv("FR_ANISO");
            if (e != NULL) { float v = (float)atof(e); if (v >= 1.0f) want = v; }
            if (want > (float)maxa) want = (float)maxa;
            if (want > 1.0f) {
                glBindTexture(GL_TEXTURE_2D, tex->handle);
                glTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */,
                                (GLfloat)want);
            }
        }
        mat->maps[map_slot] = tex;
        cs->texture_count++;
    }
    free(px);
}

/* Build a render material from a descriptor material definition. */
static void build_material(client_scene_t *cs, render_material_t *mat,
                           const scene_desc_material_t *md, const char *base_dir,
                           client_image_load_fn image_load)
{
    material_init(mat);
    for (int a = 0; a < 3; ++a) { mat->tint[a] = md->tint[a]; mat->emissive_color[a] = md->emissive_color[a]; }
    mat->metalness = md->metalness;
    mat->roughness_min = md->roughness_min;
    mat->roughness_max = md->roughness_max;
    mat->normal_scale = md->normal_scale;
    mat->uv_scale[0] = md->uv_scale[0]; mat->uv_scale[1] = md->uv_scale[1];
    mat->contrast = md->contrast;
    mat->ao_strength = md->ao_strength;
    mat->emissive_strength = md->emissive_strength;
    mat->orm_packed = md->orm_packed;
    mat->opacity = md->opacity;
    static const struct { int map; int fmt_srgb; } slot[SCENE_DESC_MAT_TEX_COUNT] = {
        { MATERIAL_TEX_ALBEDO, 1 }, { MATERIAL_TEX_NORMAL, 0 },
        { MATERIAL_TEX_METALLIC, 0 }, { MATERIAL_TEX_ROUGHNESS, 0 },
        { MATERIAL_TEX_AO, 0 }, { MATERIAL_TEX_EMISSIVE, 1 },
    };
    for (int t = 0; t < SCENE_DESC_MAT_TEX_COUNT; ++t) {
        texture_format_t fmt = slot[t].fmt_srgb ? TEXTURE_FORMAT_SRGB8 : TEXTURE_FORMAT_RGB8;
        load_material_tex(cs, mat, slot[t].map, base_dir, md->tex[t], fmt, image_load);
    }
}

/* The Blender directional-light irradiance (energy, ~W/m^2) -> engine realtime sun
 * radiance scale now lives in render_config (sun_energy_scale, default 0.45): an
 * energy-20 sun lands near the reference hall's tuned (~9) direct-sun colour. */

/* Find the descriptor's sun (first DIRECTIONAL light) and derive the forward
 * pass's realtime sun. render_forward's u_sun_dir points TOWARD the sun, so it is
 * the negated travel direction. sun_color = light colour * intensity * scale.
 * Returns true if a sun was found (fills to_sun[3] + color[3]). */
static bool find_sun(const scene_desc_t *desc, float to_sun[3], float color[3],
                     float energy_scale)
{
    for (uint32_t i = 0; i < desc->light_count; ++i) {
        const scene_desc_light_t *l = &desc->lights[i];
        if (l->kind != SCENE_DESC_LIGHT_DIRECTIONAL) continue;
        float len = 0.0f;
        for (int a = 0; a < 3; ++a) len += l->direction[a] * l->direction[a];
        len = (len > 1e-8f) ? 1.0f / sqrtf(len) : 0.0f;
        for (int a = 0; a < 3; ++a) {
            to_sun[a] = -l->direction[a] * len;                 /* toward the sun */
            color[a] = l->color[a] * l->intensity * energy_scale;
        }
        return true;
    }
    return false;
}

/* Translate the descriptor's lights into the render light store. scene_desc_light
 * mirrors render_light field-for-field (kind values + flag bit-values match by
 * contract), so this is a straight copy. The sun (directional, BAKED +
 * DYNAMIC_INDIRECT) is added too: the realtime packer skips baked-only lights,
 * but the SDF-probe GI gathers DYNAMIC_INDIRECT lights from scene->lights. */
static void add_descriptor_lights(client_scene_t *cs, const scene_desc_t *desc)
{
    for (uint32_t i = 0; i < desc->light_count; ++i) {
        const scene_desc_light_t *sl = &desc->lights[i];
        /* The store feeds TWO consumers: the forward+ cluster (punctual realtime
         * lights only -- it skips anything without FLAG_REALTIME, so a directional
         * there is harmless) and the PROBE GI trace (gathers FLAG_PROBE_GI lights).
         * So we must include the SUN (directional, probe_gi) here or the probe GI
         * -- and the offline probe bake -- gather no light at all (black indirect).
         * Skip only area lights and lights that are neither realtime-punctual nor
         * a probe light. */
        int realtime_punctual =
            (sl->flags & SCENE_DESC_LIGHT_FLAG_REALTIME) &&
            (sl->kind == SCENE_DESC_LIGHT_POINT || sl->kind == SCENE_DESC_LIGHT_SPOT);
        int probe_light = (sl->flags & (SCENE_DESC_LIGHT_FLAG_PROBE_GI |
                                        SCENE_DESC_LIGHT_FLAG_DYNAMIC_INDIRECT)) != 0;
        if (sl->kind == SCENE_DESC_LIGHT_AREA ||
            (!realtime_punctual && !probe_light))
            continue;
        render_light_t rl;
        memset(&rl, 0, sizeof rl);
        rl.kind = (render_light_kind_t)sl->kind;
        for (int a = 0; a < 3; ++a) {
            rl.position[a] = sl->position[a];
            rl.direction[a] = sl->direction[a];
            rl.color[a] = sl->color[a];
        }
        rl.intensity = sl->intensity;
        rl.range = sl->range;
        rl.radius = sl->radius;
        rl.cos_inner = sl->cos_inner;
        rl.cos_outer = sl->cos_outer;
        rl.flags = sl->flags;   /* SCENE_DESC_LIGHT_FLAG_* == RENDER_LIGHT_FLAG_*. */
        if (!render_light_add(&cs->lights, &rl)) break;   /* store full */
    }
}

/* ── Public API ────────────────────────────────────────────────── */

bool client_scene_load(client_scene_t *cs, const gl_loader_t *loader,
                       const struct scene_desc *descp, const char *base_dir,
                       client_image_load_fn image_load, int screen_w, int screen_h,
                       const unsigned int *ext_sh_tex,
                       const lm_atlas_rect_t *ext_mrect,
                       const lm_atlas_t *ext_atlas,
                       gi_sdf_stream_t *ext_sdf,
                       uint32_t lm_chunk_count,
                       const struct render_config *render_cfg)
{
    if (cs == NULL || loader == NULL || descp == NULL || base_dir == NULL) return false;
    const scene_desc_t *desc = descp;
    memset(cs, 0, sizeof *cs);
    /* Resolve the render config FIRST (NULL => engine defaults): it drives material
     * texture filtering, probe density and all forward/GI tuning below, and the
     * material load happens before any of those. */
    render_config_t rc;
    if (render_cfg != NULL) rc = *(const render_config_t *)render_cfg;
    else render_config_defaults(&rc);

    cs->loader = loader;
    cs->aniso = rc.aniso;   /* material texture anisotropy (config-driven). */

    uint32_t nobj = desc->object_count, nmat = desc->material_count;
    cs->rb_cap = nobj + 64u;   /* slack for networked dynamic bodies. */
    cs->meshes = calloc(nobj ? nobj : 1, sizeof(static_mesh_t));
    cs->materials = calloc(nmat ? nmat : 1, sizeof(render_material_t));
    cs->textures = calloc((nmat ? nmat : 1) * SCENE_DESC_MAT_TEX_COUNT, sizeof(texture_t));
    cs->rb = calloc(cs->rb_cap, sizeof(render_renderable_t));
    cs->light_buf = calloc(64, sizeof(render_light_t));
    /* DYNAMIC object tracking (voxelised into the GI dynamic albedo volume). */
    cs->dyn_idx = calloc(nobj ? nobj : 1, sizeof *cs->dyn_idx);
    cs->dyn_albedo = calloc((nobj ? nobj : 1) * 3u, sizeof *cs->dyn_albedo);
    cs->dyn_col = calloc(nobj ? nobj : 1, sizeof *cs->dyn_col);
    cs->dyn_count = 0;
    if (!cs->meshes || !cs->materials || !cs->textures || !cs->rb || !cs->light_buf) {
        client_scene_destroy(cs); return false;
    }

    for (uint32_t m = 0; m < nmat; ++m)
        build_material(cs, &cs->materials[m], &desc->materials[m], base_dir, image_load);
    cs->material_count = nmat;   /* so the per-submesh material table is valid. */

    /* Baked lightmap atlas: EITHER supplied by the external light-data streamer
     * (ext_sh_tex: borrowed SH pages + per-mesh rects; the streamer pages layers
     * and the caller sets each item's sh_layer per frame) OR loaded synchronously
     * here (legacy). Either way the per-mesh atlas rects let us remap each mesh's
     * uv1 into the shared atlas as the mesh is built (single-atlas == 1 chunk). */
    lm_atlas_rect_t *mrect_own = NULL;   /* freed here iff we loaded internally. */
    const lm_atlas_rect_t *mrect = NULL;
    lm_atlas_t atlas = { 0, 0 };
    bool have_lm = false, streamed = false;
    if (ext_sh_tex != NULL) {
        for (int c = 0; c < 9; ++c) cs->sh_tex[c] = ext_sh_tex[c];
        cs->sh_borrowed = 1;             /* streamer owns the SH pages. */
        mrect = ext_mrect;
        if (ext_atlas != NULL) atlas = *ext_atlas;
        have_lm = (ext_sh_tex[0] != 0u);
        streamed = true;
    } else if (desc->lightdata.lightmap_prefix[0]) {
        mrect_own = calloc(nobj ? nobj : 1, sizeof *mrect_own);
        if (mrect_own != NULL) {
            char lm[512]; snprintf(lm, sizeof lm, "%s/%s", base_dir, desc->lightdata.lightmap_prefix);
            have_lm = client_scene_load_lightmap(loader, lm, nobj, cs->sh_tex, mrect_own, &atlas);
            mrect = mrect_own;
        }
    }

    /* Meshes (uv1 remapped into the atlas) + scene AABB (transformed bounds). */
    float amin[3] = { 1e30f, 1e30f, 1e30f }, amax[3] = { -1e30f, -1e30f, -1e30f };
    render_scene_init(&cs->scene, cs->rb, cs->rb_cap);
    /* Multi-material meshes resolve per-submesh materials from this table by
     * material_slot (a GLOBAL index baked into each submesh at load). */
    cs->scene.materials = cs->materials;
    cs->scene.material_count = cs->material_count;
    for (uint32_t i = 0; i < nobj; ++i) {
        const scene_desc_object_t *o = &desc->objects[i];
        char path[512]; snprintf(path, sizeof path, "%s/%s", base_dir, o->mesh);
        const lm_atlas_rect_t *rc = (have_lm && mrect != NULL) ? &mrect[i] : NULL;
        if (load_mesh(loader, path, rc, &atlas, o->material_idx, o->material_count,
                      &cs->meshes[cs->mesh_count]) != 0) continue;
        static_mesh_t *sm = &cs->meshes[cs->mesh_count];
        float model[16]; model_from_trs(o->position, o->rotation, o->scale, model);
        /* material == NULL => the draw loops resolve per-submesh materials from
         * the scene table by material_slot (built in load_mesh). Only objects
         * with NO polygroup split (single submesh) rely on that too -- their
         * one submesh carries material_idx[0] as its global slot. */
        int mi = (o->material_count > 0) ? o->material_idx[0] : -1;
        const render_material_t *fallback =
            (sm->submesh_count <= 1 && mi >= 0 && (uint32_t)mi < nmat)
                ? &cs->materials[mi] : NULL;
        if (render_scene_add(&cs->scene, sm, fallback, model)) {
            /* Internal single atlas => layer 0. Streamed => -1 until the chunk is
             * resident; the caller sets sh_layer per frame from the streamer. No
             * lightmap => -1 (SH sampler skips it). DYNAMIC objects are outside the
             * bake entirely, so they never carry a lightmap layer. */
            cs->scene.items[cs->scene.count - 1].sh_layer =
                (have_lm && !streamed && !o->dynamic) ? 0 : -1;
            /* Record DYNAMIC objects: absent from the baked voxel albedo, so they
             * are voxelised into the GI's dynamic albedo volume each frame with
             * their real material albedo (else they bleed neutral grey). */
            if (o->dynamic && cs->dyn_idx != NULL && cs->dyn_albedo != NULL) {
                uint32_t k = cs->dyn_count++;
                cs->dyn_idx[k] = cs->scene.count - 1;
                const float *tint = (mi >= 0 && (uint32_t)mi < nmat)
                                    ? desc->materials[mi].tint : NULL;
                for (int a = 0; a < 3; ++a) cs->dyn_albedo[k*3+a] = tint ? tint[a] : 0.5f;
            }
        }
        for (int cx = 0; cx < 8; ++cx) {
            float lp[3] = { (cx & 1) ? sm->aabb_max[0] : sm->aabb_min[0],
                            (cx & 2) ? sm->aabb_max[1] : sm->aabb_min[1],
                            (cx & 4) ? sm->aabb_max[2] : sm->aabb_min[2] };
            float wp[3] = {
                model[0]*lp[0]+model[4]*lp[1]+model[8]*lp[2]+model[12],
                model[1]*lp[0]+model[5]*lp[1]+model[9]*lp[2]+model[13],
                model[2]*lp[0]+model[6]*lp[1]+model[10]*lp[2]+model[14] };
            for (int a = 0; a < 3; ++a) { if (wp[a] < amin[a]) amin[a] = wp[a]; if (wp[a] > amax[a]) amax[a] = wp[a]; }
        }
        cs->mesh_count++;
    }
    cs->static_count = (int)cs->scene.count;
    if (amin[0] > amax[0]) { for (int a = 0; a < 3; ++a) { amin[a] = -10; amax[a] = 10; } }

    /* Static irradiance volume (rpg-zygg): fold the baked lightmap into the probe
     * GI so shadowed interior surfaces read the baked bounce. Built here while the
     * per-mesh atlas rects (mrect) + atlas dims are still valid. */
    bool have_svol = false;
    if (have_lm)
        have_svol = client_static_volume_build(&cs->static_vol, desc, base_dir,
                                               mrect, &atlas, amin, amax);
    free(mrect_own);

    render_light_store_init(&cs->lights, cs->light_buf, 64);
    cs->scene.lights = &cs->lights;
    add_descriptor_lights(cs, desc);   /* realtime punctual lights (sun excluded). */

    /* Probe grid from the descriptor spec, with the config's density scale applied
     * to the authored spacing (<1 = denser/more probes). */
    scene_desc_probes_t probes = desc->probes;
    if (rc.probe_spacing_scale > 0.0f && rc.probe_spacing_scale != 1.0f) {
        if (probes.spacing  > 0.0f) probes.spacing  *= rc.probe_spacing_scale;
        if (probes.vspacing > 0.0f) probes.vspacing *= rc.probe_spacing_scale;
    }
    static uint8_t probe_arena_buf[8 * 1024 * 1024];
    arena_t pa; arena_init(&pa, probe_arena_buf, sizeof probe_arena_buf);
    probe_set_t pset; memset(&pset, 0, sizeof pset);
    /* MANUAL probes first (rpg-pjkb): the offline probe_bake pass ships the
     * placed+fixed-up set as a .probes file (the loader must not re-place --
     * at load time it only ever sees the resident subset of the scene). The
     * descriptor's manual_path wins; else the bake convention <sdf_prefix>.probes
     * is tried. The auto grid below remains the no-bake fallback, and hand-
     * placed extras simply live in the same .probes file. */
    bool probes_manual = false;
    {
        char ppath[1024]; ppath[0] = '\0';
        if (desc->probes.has_manual)
            snprintf(ppath, sizeof ppath, "%s/%s", base_dir, desc->probes.manual_path);
        else if (desc->lightdata.sdf_prefix[0])
            snprintf(ppath, sizeof ppath, "%s/%s.probes", base_dir,
                     desc->lightdata.sdf_prefix);
        if (ppath[0] != '\0' && probe_set_load(ppath, &pa, &pset) && pset.count > 0) {
            probes_manual = true;
            printf("[client] probes: %u manual from %s\n", pset.count, ppath);
        } else {
            memset(&pset, 0, sizeof pset);
        }
    }
    /* Brick sidecar (rpg-pjkb): the O(1) sampling structure over those probes.
     * The voxel index is rebuilt here (milliseconds) from the shipped grid
     * parameters instead of being stored. Static lifetime: render_world keeps
     * pointers only through init (the GL upload copies everything). */
    static probe_brick_data_t brick_data;
    static probe_brick_index_t brick_index;
    bool have_bricks = false;
    if (probes_manual && desc->lightdata.sdf_prefix[0]) {
        char bpath[1024];
        snprintf(bpath, sizeof bpath, "%s/%s.bricks", base_dir,
                 desc->lightdata.sdf_prefix);
        if (probe_brick_data_load(bpath, &pa, &brick_data) &&
            brick_data.n_probes == pset.count) {
            probe_brick_config_t bcfg;
            memset(&bcfg, 0, sizeof bcfg);
            memcpy(bcfg.aabb_min, brick_data.aabb_min, sizeof bcfg.aabb_min);
            memcpy(bcfg.aabb_max, brick_data.aabb_max, sizeof bcfg.aabb_max);
            bcfg.coarse_brick = brick_data.coarse_brick;
            bcfg.levels = brick_data.levels;
            if (probe_brick_index_build(&bcfg, brick_data.bricks,
                                        brick_data.n_bricks, &pa, &brick_index)) {
                have_bricks = true;
                printf("[client] probes: brick sampling on (%u bricks, index %dx%dx%d)\n",
                       brick_data.n_bricks, brick_index.dim[0], brick_index.dim[1],
                       brick_index.dim[2]);
            }
        }
    }
    /* Baked probe irradiance streams PER SDF CHUNK (<sdf_prefix>_cNNN.probesh):
     * gi_sdf_stream detects + loads it alongside each SDF chunk, and gi_runtime
     * uploads + freezes as chunks page in. No whole-file load here (a massive
     * level can't hold it) -- it rides the same streaming pipeline as the SDF. */
    if (!probes_manual) {
        probe_place_grid(&probes, amin, amax, &pa, &pset);
        /* Probe-volume importance boxes densify chosen regions (distance/LOD),
         * realising the generated-with-volume case; only when the scene specifies
         * them (else keep the regular grid for trilinear sampling). rpg-ft0g. */
        if (probes.box_count > 0) {
            probe_set_t refined; memset(&refined, 0, sizeof refined);
            if (probe_place_refine_importance(&pset, &probes, amin, amax, &pa, &refined))
                pset = refined;
        }
    }

    /* render_world config. Scalar tuning comes from the render_config (JSON-loadable
     * per level/zone, rpg-da8c); this function supplies the DATA-driven bits
     * (textures, scene pointer, scene AABB, sun from the descriptor, streamed probes)
     * that a config file cannot know. (rc resolved above.) */
    render_world_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.forward.loader = loader;
    cfg.forward.cluster = (cluster_config_t){ rc.cluster_tiles_x, rc.cluster_tiles_y,
                                              rc.cluster_slices, rc.cluster_near, rc.cluster_far };
    cfg.forward.max_lights = rc.max_lights;
    cfg.forward.index_capacity = rc.cluster_tiles_x * rc.cluster_tiles_y * rc.cluster_slices * 16u;
    cfg.forward.screen_w = (float)screen_w; cfg.forward.screen_h = (float)screen_h;
    for (int a = 0; a < 3; ++a) cfg.forward.ambient[a] = rc.ambient[a];
    /* Baked SH lightmap carries the INDIRECT bounce only (the sun's DIRECT term is
     * the realtime directional light + CSM below), so sh_scale < 1 by default. */
    /* sh_enabled: -1 => auto (on iff a lightmap texture loaded); 0/1 => forced. */
    cfg.forward.sh_enabled = (rc.sh_enabled >= 0) ? rc.sh_enabled : (cs->sh_tex[0] ? 1 : 0);
    cfg.forward.sh_scale = rc.sh_scale; cfg.forward.sh_normal_bias = rc.sh_normal_bias;
    for (int c = 0; c < 9; ++c) cfg.forward.sh_tex[c] = cs->sh_tex[c];
    cfg.scene = &cs->scene;

    /* Realtime sun (direct term) + shadows, from the descriptor's directional
     * light. The lightmap holds the sun's baked indirect; the direct sun and its
     * cascaded shadows come from here so dynamic geometry shadows correctly. */
    float to_sun[3], sun_col[3];
    if (find_sun(desc, to_sun, sun_col, rc.sun_energy_scale)) {
        for (int a = 0; a < 3; ++a) {
            cfg.forward.sun_dir[a] = to_sun[a];
            cfg.forward.sun_color[a] = sun_col[a];
        }
        /* Cascaded directional shadow maps fit to the whole scene AABB (+pad). */
        cfg.forward.dir_cascades = rc.dir_cascades;
        cfg.forward.dir_static_res = rc.dir_static_res;
        cfg.forward.dir_dynamic_res = rc.dir_dynamic_res;
        cfg.forward.dir_lambda = rc.dir_lambda;
        cfg.forward.dir_bias = rc.dir_bias;
        cfg.forward.dir_softness = rc.dir_softness;
        cfg.forward.dir_pcss = rc.dir_pcss;
        cfg.forward.dir_max_distance = rc.dir_max_distance;
        cfg.forward.dir_translucency = rc.dir_translucency != 0;
        cfg.forward.dir_caustics = rc.dir_caustics != 0;
        for (int a = 0; a < 3; ++a) {
            cfg.forward.shadow_scene_min[a] = amin[a] - 1.0f;
            cfg.forward.shadow_scene_max[a] = amax[a] + 1.0f;
        }
    }
    /* Omnidirectional cube shadows for every point/spot light flagged
     * RENDER_LIGHT_FLAG_SHADOW (multi-light path, shadow_light=-1). */
    float diag = 0.0f;
    for (int a = 0; a < 3; ++a) { float e = amax[a] - amin[a]; diag += e * e; }
    diag = sqrtf(diag);
    cfg.forward.shadow_light = -1;
    cfg.forward.shadow_max = rc.shadow_max;
    cfg.forward.shadow_res = rc.shadow_res;
    cfg.forward.shadow_near = rc.shadow_near;
    cfg.forward.shadow_far = (diag > 1.0f) ? diag * rc.shadow_far_scale : 60.0f;
    cfg.forward.shadow_bias = rc.shadow_bias;
    cfg.forward.spot_light = -1;   /* no dedicated spot; cube path covers spots. */

    cfg.gi_enabled = rc.gi_enabled;
    cfg.gi_sdf_prefix = NULL;   /* set below to a persistent string. */
    cfg.gi_ext_sdf = ext_sdf;   /* borrow the streamer's SDF stream if provided. */
    static char sdf_path[512];
    if (ext_sdf == NULL && desc->lightdata.sdf_prefix[0]) {
        snprintf(sdf_path, sizeof sdf_path, "%s/%s", base_dir, desc->lightdata.sdf_prefix);
        cfg.gi_sdf_prefix = sdf_path;
    }
    for (int a = 0; a < 3; ++a) {
        cfg.gi_aabb_min[a] = amin[a] + rc.gi_aabb_pad_lo[a];
        cfg.gi_aabb_max[a] = amax[a] - rc.gi_aabb_pad_hi[a];
    }
    cfg.gi_bricks = have_bricks ? &brick_data : NULL;
    cfg.gi_brick_index = have_bricks ? &brick_index : NULL;
    cfg.gi_probe_pos = pset.positions; cfg.gi_probe_count = pset.count;
    cfg.gi_baked_sh = cs->baked_sh; cfg.gi_baked_sg = cs->baked_sg; cfg.gi_baked_count = cs->baked_count;
    cfg.gi_max_probes = pset.count;   /* allow set_probes to restore the full set. */
    /* Keep a persistent copy of the full generated probe set so it can be streamed
     * down to the resident chunks each frame (client_scene_stream_probes). */
    cs->probe_count_full = pset.count;
    cs->probe_resident = 0xFFFFFFFFu;
    if (pset.count > 0 && pset.positions != NULL) {
        cs->probe_pos_full = malloc((size_t)pset.count * 3 * sizeof(float));
        cs->probe_scratch = malloc((size_t)pset.count * 3 * sizeof(float));
        cs->probe_active = malloc((size_t)pset.count);   /* residency mask (dense grid). */
        if (cs->probe_pos_full != NULL)
            memcpy(cs->probe_pos_full, pset.positions, (size_t)pset.count * 3 * sizeof(float));
    }
    cfg.gi_grid_cell = rc.gi_grid_cell;
    cfg.gi_prepass_w = (screen_w / 8 > 0) ? screen_w / 8 : 1;
    cfg.gi_prepass_h = (screen_h / 8 > 0) ? screen_h / 8 : 1;
    cfg.gi_max_lights = rc.gi_max_lights; cfg.gi_max_boxes = rc.gi_max_boxes; cfg.gi_soft_k = rc.gi_soft_k;
    cfg.gi_probe_min = rc.gi_probe_min; cfg.gi_probe_sphere_margin = rc.gi_probe_sphere_margin;
    cfg.gi_bin_interval = rc.gi_bin_interval;
    cfg.gi_update_interval = rc.gi_update_interval; cfg.gi_n_probe_groups = rc.gi_n_probe_groups;
    cfg.gi_freeze_ticks = rc.gi_freeze_ticks;
    cfg.gi_smooth = rc.gi_smooth;
    /* Probe-GI tuning: config is the source of truth (GI_* env only overrides). */
    gi_probe_tuning_defaults(&cfg.gi_tuning);
    cfg.gi_tuning.field_on        = rc.gi_field;
    cfg.gi_tuning.mis             = rc.gi_mis;
    cfg.gi_tuning.hybrid          = rc.gi_hybrid;
    cfg.gi_tuning.hero            = rc.gi_hero;
    cfg.gi_tuning.samples         = rc.gi_samples;
    cfg.gi_tuning.spec_lobes      = rc.gi_spec_lobes;
    cfg.gi_tuning.update_interval = rc.gi_update_interval;
    cfg.gi_tuning.n_probe_groups  = rc.gi_n_probe_groups;
    cfg.gi_tuning.bounce          = rc.gi_bounce;
    cfg.gi_tuning.ray_clamp       = rc.gi_ray_clamp;
    cfg.gi_tuning.near_dist       = rc.gi_near;
    cfg.gi_tuning.dmax            = rc.gi_dmax;
    cfg.gi_tuning.emin            = rc.gi_emin;
    cfg.gi_tuning.norm_gate       = rc.gi_norm_gate;
    cfg.gi_tuning.stat_scale      = rc.gi_stat_scale;
    cfg.gi_tuning.dyn_gain        = rc.gi_dyn_gain;
    cfg.gi_tuning.smooth          = rc.gi_smooth;
    cfg.gi_tuning.vis_bias        = rc.gi_vis_bias;
    cfg.gi_tuning.vis_varmin      = rc.gi_vis_varmin;
    cfg.gi_tuning.vis_sharp       = rc.gi_vis_sharp;
    if (pset.grid_dim[0] > 0) {
        for (int a = 0; a < 3; ++a) {
            cfg.gi_grid_origin[a] = pset.grid_origin[a];
            cfg.gi_grid_cell3[a] = pset.grid_cell[a];
            cfg.gi_grid_dim[a] = pset.grid_dim[a];
        }
    }
    cfg.has_static_weights = 1; cfg.static_baked_w = rc.static_baked_w; cfg.static_dyn_w = rc.static_dyn_w;
    cfg.forward.dir_pcf_taps = rc.dir_pcf_taps;
    cfg.forward.dir_msaa = rc.dir_msaa;
    cfg.forward.dir_mask_res = rc.dir_mask_res;
    cfg.has_spec_gain = 1; cfg.spec_gain = rc.spec_gain;
    cfg.has_probe_gain = 1; cfg.probe_gain = rc.gi_probe_gain;

    /* Fold the baked lightmap into the probes (built above) + sky-openness AO,
     * matching hall_lit_dynamic so shadowed interior surfaces get baked bounce. */
    if (have_svol) {
        cfg.has_static_volume = 1;
        cfg.static_vol_tex = cs->static_vol.tex;
        for (int a = 0; a < 3; ++a) {
            cfg.static_vol_origin[a] = cs->static_vol.origin[a];
            cfg.static_vol_dim[a] = (float)cs->static_vol.dims[a];
        }
        cfg.static_vol_voxel = cs->static_vol.voxel;
        cfg.static_k = rc.static_k;
        cs->static_k = rc.static_k;
        cfg.has_sky_ao = 1;
        for (int a = 0; a < 3; ++a) cfg.sky_ao_color[a] = rc.sky_ao_color[a];
        cfg.sky_ao_ref = rc.sky_ao_ref;
        cfg.sky_ao_mult = rc.sky_ao_mult;
    }

    /* Debug: CLIENT_NOSUN isolates the dynamic punctual lights (kills the sun +
     * baked lightmap + ambient) so their direct contribution is visible alone. */
    if (getenv("CLIENT_NOSUN")) {
        cfg.forward.sun_color[0] = cfg.forward.sun_color[1] = cfg.forward.sun_color[2] = 0.0f;
        cfg.forward.sh_enabled = 0;
        cfg.forward.dir_cascades = 0;
        cfg.forward.ambient[0] = cfg.forward.ambient[1] = cfg.forward.ambient[2] = 0.0f;
    }

    if (!render_world_init(&cs->world, &cfg)) { client_scene_destroy(cs); return false; }

    /* Shared dual visibility prepass over the SDF chunks + the lightmap chunks:
     * when driven each frame it retires gi_runtime's internal prepass, gates probes
     * by on-screen visibility, and reports the visible lightmap-chunk set for the
     * light streamer (rpg-sazm/gky0). SDF channel sized from the streamed SDF; the
     * lightmap channel from @p lm_chunk_count (1 = single atlas, always resident). */
    if (ext_sdf != NULL && ext_sdf->n_chunks > 0) {
        int pw = (screen_w / 8 > 0) ? screen_w / 8 : 1;
        int ph = (screen_h / 8 > 0) ? screen_h / 8 : 1;
        int n_lm = (lm_chunk_count > 0) ? (int)lm_chunk_count : 1;
        if (gi_vis_prepass_init(&cs->gi_pp, pw, ph, ext_sdf->n_chunks) == 0 &&
            gi_vis_prepass_enable_dual(&cs->gi_pp, n_lm) == 0)
            cs->gi_pp_ready = 1;
    }
    /* Voxeliser for DYNAMIC geometry -> the GI's dynamic albedo volume. Only worth
     * standing up if the level actually has dynamic objects. */
    if (cs->dyn_count > 0 && gi_voxelize_init(&cs->vox, loader))
        cs->vox_ready = 1;
    return true;
}

void client_scene_stream_probes(client_scene_t *cs, const float *box_min,
                                const float *box_max, uint32_t n_boxes)
{
    if (cs == NULL || cs->probe_pos_full == NULL || cs->probe_scratch == NULL ||
        cs->probe_count_full == 0 || box_min == NULL || box_max == NULL || n_boxes == 0)
        return;
    /* Probe streaming POPULATES THE GRID: the forward+ addresses probes
     * POSITIONALLY -- probe = (z*dimY + y)*dimX + x (gi_probe_specular /
     * gi_probe_indirect2) -- so the set must stay a dense lattice. Compacting it to
     * the resident subset shifts every probe after the first dropped one and the
     * shader then reads unrelated probes (garbage SG lobes => no specular/roughness
     * response, lattice-shaped diffuse artifacts). So residency is expressed as a
     * per-probe ACTIVE mask instead: every grid slot keeps its position, and
     * non-resident probes are simply skipped by the update (keeping their last
     * coefficients) -- which still bounds the per-frame trace work. */
    if (cs->probe_active == NULL) return;
    uint32_t live = 0;
    for (uint32_t i = 0; i < cs->probe_count_full; ++i) {
        const float *p = &cs->probe_pos_full[i * 3];
        int inside = 0;
        for (uint32_t b = 0; b < n_boxes && !inside; ++b) {
            const float *mn = &box_min[b * 3], *mx = &box_max[b * 3];
            inside = (p[0] >= mn[0] && p[0] <= mx[0] && p[1] >= mn[1] && p[1] <= mx[1] &&
                      p[2] >= mn[2] && p[2] <= mx[2]);
        }
        cs->probe_active[i] = (unsigned char)(inside ? 1 : 0);
        live += (uint32_t)inside;
    }
    /* The full lattice is uploaded once (at load); only the mask changes here, and
     * only when residency actually moved (avoids a per-frame buffer round-trip). */
    if (live != cs->probe_resident) {
        fprintf(stderr, "[client] probes resident: %u/%u (from %u sdf boxes)\n",
                live, cs->probe_count_full, n_boxes);
        gi_runtime_set_probe_active(&cs->world.gi, cs->probe_active, cs->probe_count_full);
        cs->probe_resident = live;
    }
}

void client_scene_gi_visibility(client_scene_t *cs, const float view[16],
                                const float proj[16], const float *sdf_box_min,
                                const float *sdf_box_max, int n_sdf_boxes,
                                const int *lm_mchunk, int lm_nm,
                                int screen_w, int screen_h)
{
    if (cs == NULL || !cs->gi_pp_ready || sdf_box_min == NULL || sdf_box_max == NULL) return;
    /* One geometry pass writes BOTH chunk id sets: per-fragment SDF chunk (box
     * test, low 16 bits) + per-mesh lightmap chunk (@p lm_mchunk, high 16 bits).
     * gi_pp.visible / visible_lm come back one frame late. */
    gi_vis_prepass_run_dual(&cs->gi_pp, &cs->scene, view, proj, sdf_box_min, sdf_box_max,
                            n_sdf_boxes, lm_mchunk, lm_nm, screen_w, screen_h);
    /* The prepass only prioritizes which SDF chunks to SWAP IN; it must NOT evict
     * on-screen state. Feed the visible mask to gi_runtime as a load/priority hint. */
    render_world_set_visible(&cs->world, cs->gi_pp.visible, n_sdf_boxes);
    /* Gate the probe set to the RESIDENT SDF chunks (the persistent page cache) --
     * NOT the instantaneous on-screen set. Gating by visibility drops a chunk's
     * probes the moment you look away, so the indirect it carried vanishes
     * (view-dependent darkening). Residency persists until the cache is full, so
     * this keeps the GI stable as the camera turns. */
    float vmin[64 * 3], vmax[64 * 3];
    int nv = 0;
    if (cs->world.gi.sdf_ptr != NULL)
        nv = gi_sdf_stream_resident_boxes(cs->world.gi.sdf_ptr, vmin, vmax, 64);
    if (nv > 0) client_scene_stream_probes(cs, vmin, vmax, (uint32_t)nv);
}

void client_scene_render(client_scene_t *cs, const render_camera_t *cam,
                         const gi_collider_t *boxes, uint32_t n_boxes,
                         int screen_w, int screen_h)
{
    if (cs == NULL || cam == NULL) return;
    cs->scene.camera = *cam;
    /* With no caller-supplied proxies, feed the scene's own DYNAMIC objects so they
     * occlude in the probe SDF (their colour comes from the dynamic albedo volume). */
    if (boxes == NULL && n_boxes == 0 && cs->dyn_count > 0 && cs->dyn_col != NULL) {
        boxes = cs->dyn_col;
        n_boxes = cs->dyn_count;
    }
    render_world_update(&cs->world, boxes, n_boxes, screen_w, screen_h);
}

void client_scene_destroy(client_scene_t *cs)
{
    if (cs == NULL) return;
    render_world_destroy(&cs->world);
    for (uint32_t i = 0; i < cs->mesh_count; ++i) static_mesh_destroy(&cs->meshes[i]);
    for (uint32_t i = 0; i < cs->texture_count; ++i) texture_destroy(&cs->textures[i]);
    /* SH pages: delete only if we own them (streamed pages belong to the streamer). */
    if (!cs->sh_borrowed)
        for (int c = 0; c < 9; ++c) if (cs->sh_tex[c]) glDeleteTextures(1, &cs->sh_tex[c]);
    gi_static_volume_destroy(&cs->static_vol);
    free(cs->baked_sh); free(cs->baked_sg);
    if (cs->gi_pp_ready) gi_vis_prepass_destroy(&cs->gi_pp);
    if (cs->vox_ready) gi_voxelize_destroy(&cs->vox);
    free(cs->dyn_idx); free(cs->dyn_albedo); free(cs->dyn_col);
    free(cs->meshes); free(cs->materials); free(cs->textures);
    free(cs->rb); free(cs->light_buf);
    free(cs->probe_pos_full); free(cs->probe_scratch); free(cs->probe_active);
    memset(cs, 0, sizeof *cs);
}
