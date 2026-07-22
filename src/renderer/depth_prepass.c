#include "ferrum/renderer/depth_prepass.h"

#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/cull/frustum_cull.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* gl_Position MUST be computed identically to the PBR forward shader
 * (u_projection * u_view * (u_model * pos), all on the GPU) so the pre-pass and
 * forward depths are bit-identical -- a CPU-composed MVP differs by ULPs and
 * z-fights under LEQUAL on grazing/curved surfaces. */
static const char *const DEPTH_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  gl_Position = u_projection * u_view * wp;\n"
    "}\n";
static const char *const DEPTH_FS =
    "#version 330 core\n"
    "void main(){}\n";

static void *depth_get_proc(const gl_loader_t *loader, const char *name)
{
    if (loader == NULL || loader->get_proc_address == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

shader_program_status_t depth_prepass_init(depth_prepass_t *pass,
                                           const gl_loader_t *loader)
{
    if (pass == NULL || loader == NULL) {
        return SHADER_PROGRAM_ERR_INVALID;
    }
    memset(pass, 0, sizeof(*pass));
    shader_program_status_t st =
        shader_program_create(&pass->shader, loader, DEPTH_VS, DEPTH_FS, NULL, 0);
    if (st != SHADER_PROGRAM_OK) {
        return st;
    }
    shader_uniform_cache_init(&pass->cache, &pass->shader);

    void *cm = depth_get_proc(loader, "glColorMask");
    void *dm = depth_get_proc(loader, "glDepthMask");
    void *en = depth_get_proc(loader, "glEnable");
    void *df = depth_get_proc(loader, "glDepthFunc");
    if (cm == NULL || dm == NULL || en == NULL || df == NULL) {
        return SHADER_PROGRAM_ERR_MISSING_GL;
    }
    memcpy(&pass->glColorMask, &cm, sizeof(cm));
    memcpy(&pass->glDepthMask, &dm, sizeof(dm));
    memcpy(&pass->glEnable, &en, sizeof(en));
    memcpy(&pass->glDepthFunc, &df, sizeof(df));
    return SHADER_PROGRAM_OK;
}

void depth_prepass_execute(depth_prepass_t *pass, const render_scene_t *scene,
                           float draw_distance)
{
    if (pass == NULL || scene == NULL) {
        return;
    }
    shader_program_bind(&pass->shader);
    pass->glEnable(GL_DEPTH_TEST);
    pass->glDepthFunc(GL_LESS);
    pass->glDepthMask(1);
    pass->glColorMask(0, 0, 0, 0); /* depth only */

    /* Upload view/projection once; the GPU composes with each model matrix
     * exactly as the PBR forward shader does (see DEPTH_VS). */
    shader_uniform_set_mat4(&pass->cache, &pass->shader, "u_view",
                            scene->camera.view, 0);
    shader_uniform_set_mat4(&pass->cache, &pass->shader, "u_projection",
                            scene->camera.proj, 0);
    /* Cull the pre-pass against the same camera frustum as the forward pass
     * (rpg-0rs4) so the two passes draw the identical visible set. */
    float planes[6][4];
    frustum_extract_planes_vp(scene->camera.proj, scene->camera.view, planes);
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL) {
            continue;
        }
        /* Translucent surfaces (rpg-rxf8) draw in the sorted blend pass with
         * depth writes OFF -- pre-writing their depth here would early-Z kill
         * everything visible through them. */
        if (r->material != NULL && r->material->opacity < 0.999f) {
            continue;
        }
        if (frustum_cull_aabb_ex(planes, r->model, r->mesh->aabb_min,
                                 r->mesh->aabb_max, scene->camera.eye,
                                 draw_distance)) {
            continue;
        }
        shader_uniform_set_mat4(&pass->cache, &pass->shader, "u_model",
                                r->model, 0);
        static_mesh_bind(r->mesh);
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s) {
            static_mesh_draw_submesh(r->mesh, s);
        }
    }
    pass->glColorMask(1, 1, 1, 1); /* restore colour writes */
}

void depth_prepass_destroy(depth_prepass_t *pass)
{
    if (pass != NULL) {
        shader_program_destroy(&pass->shader);
    }
}
