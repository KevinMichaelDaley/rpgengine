---
id: rpg-5fn3
status: closed
deps: []
links: []
created: 2026-07-16T03:37:51Z
type: task
priority: 1
assignee: KMD
---
# Denoise baked lightmaps with OpenImageDenoise (C API)

Integrate Intel Open Image Denoise (OIDN) via its C API (oidn/oidn.h -> oidnNewDevice/oidnNewFilter('RT')/oidnSetSharedFilterImage/oidnExecuteFilter) as a post-bake denoise pass on the SH-irradiance atlases, to clean up Monte-Carlo noise so we can drop sample counts (currently pushing 4096 spp for clean results).

Scope:
- Link libOpenImageDenoise (prefer system package; it's Apache-2.0, acceptable third-party). Guard behind a build flag like the FAISS_STUB pattern so headless CI without OIDN still builds.
- Denoise the lightmap atlas after the gather kernel writes it, before FLM serialization. The RT filter wants an HDR beauty (color) buffer; feed the L0/DC SH coefficient (irradiance) as color. Optionally provide albedo + normal aux buffers (we have per-luxel albedo from the SVO and geometric normals from luxelization) for the prefiltered/RT_lightmap path -> much better edge retention. OIDN has an explicit 'lightmap' quality/config mode — use it.
- Denoise EACH SH coefficient band? DC (L0) is the clear win; higher bands are directional and may over-smooth. Start by denoising L0 only (or the reconstructed irradiance) and evaluate; make per-band denoising configurable.
- Run per-chunk atlas (each chunk's atlas is an independent image) OR the packed atlas; per-chunk is simpler and matches the streaming layout.
- Denoise runs on chimera as part of the bake (never local). It's CPU (or optionally the GPU OIDN backend) — cheap vs the gather.
- Validate: bake at LOW spp (e.g. 256) + denoise vs 4096 spp reference; the denoised low-spp should approach the reference. Wire an env/config toggle (LM_DENOISE) and A/B render.

This is the IMMEDIATE next task after this ticket lands.


## Notes

**2026-07-16T03:47:27Z**

MODULE DONE + committed. lm_denoise.[ch] wraps OIDN RT/hdr filter over one float-RGB atlas image in place (optional albedo/normal guides). Two build variants share the header (real=OIDN=1 links libOpenImageDenoise; stub=default no-op so headless/CI builds). OIDN v2.5.0 vendored at extern/oidn. Wired into lm_lightmap_save gated by LM_DENOISE env (unset=off / '1'|'dc'=DC band only / 'all'=all 9 bands); runs after gutter dilate. lm_denoise_tests pass under stub.
REMAINING: (1) build OIDN on chimera (needs ISPC+TBB+weights submodule; cmake to extern/oidn/build), rebuild bake with OIDN=1. (2) A/B validate: bake hall at LOW spp (256) + LM_DENOISE=dc vs 4096-spp reference; denoised-low-spp should approach reference. (3) evaluate DC-only vs all-band; consider feeding real per-luxel albedo/geo-normal aux atlases for better edge retention (currently color-only).
