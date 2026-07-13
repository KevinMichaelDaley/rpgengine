#include "ferrum/renderer/depth_prepass.h"

#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

static const char *const DEPTH_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "uniform mat4 u_mvp;\n"
    "void main(){ gl_Position = u_mvp * vec4(in_position, 1.0); }\n";
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

void depth_prepass_execute(depth_prepass_t *pass, const render_scene_t *scene)
{
    if (pass == NULL || scene == NULL) {
        return;
    }
    shader_program_bind(&pass->shader);
    pass->glEnable(GL_DEPTH_TEST);
    pass->glDepthFunc(GL_LESS);
    pass->glDepthMask(1);
    pass->glColorMask(0, 0, 0, 0); /* depth only */

    mat4_t proj, view;
    memcpy(proj.m, scene->camera.proj, sizeof(proj.m));
    memcpy(view.m, scene->camera.view, sizeof(view.m));
    mat4_t vp = mat4_mul(proj, view);
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL) {
            continue;
        }
        mat4_t model;
        memcpy(model.m, r->model, sizeof(model.m));
        mat4_t mvp = mat4_mul(vp, model);
        shader_uniform_set_mat4(&pass->cache, &pass->shader, "u_mvp", mvp.m, 0);
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
