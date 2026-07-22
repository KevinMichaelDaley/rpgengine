/**
 * @file client_scene_svol.c
 * @brief Glue: drive the STREAMED static-irradiance probe seed each frame and
 *        re-point the GI runtime when the window volume was rebuilt (see
 *        light_stream_svol.c). Kept out of client_scene_load.c so targets
 *        that don't link the light streamer stay streamer-free.
 */
#include "ferrum/renderer/client_scene.h"
#include "ferrum/renderer/light_stream.h"
#include "light_stream_internal.h"

void client_scene_svol_stream_tick(client_scene_t *cs,
                                   struct client_light_stream *ls,
                                   const float cam_pos[3])
{
    if (cs == NULL || ls == NULL || cam_pos == NULL)
        return;
    if (client_ls_svol_tick(ls, cam_pos, &cs->static_vol))
        gi_runtime_set_static_volume(&cs->world.gi, cs->static_vol.tex,
                                     cs->static_vol.origin,
                                     (const float[3]){
                                         (float)cs->static_vol.dims[0],
                                         (float)cs->static_vol.dims[1],
                                         (float)cs->static_vol.dims[2] },
                                     cs->static_vol.voxel, cs->static_k);
}
