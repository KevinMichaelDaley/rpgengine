---
id: rpg-hdl5
status: open
deps: []
links: []
created: 2026-07-20T06:01:45Z
type: bug
priority: 3
assignee: KMD
---
# fit_sg under-reads probe specular when fewer than 32 samples


`fit_sg()` (`src/renderer/gi/gi_probe_gpu.c`) computes its lobe amplitude as `num/den` where **both** sums run over all `NR=32` slots.

`pass_gather` now re-fits the SG specular from the gathered radiance, but fills only as many slots as it collected samples for. Unfilled slots contribute 0 to `num` while still adding to `den`, so the amplitude is **diluted** and probe specular reads too dim whenever fewer than 32 samples are gathered.

Not biting at `GI_SAMPLES=1024` (the full deterministic gather fills all 32 immediately), but it will at low sample counts or sparse source sets.

Fix: track the filled count and normalise over that, or skip zero-weight slots.

## Acceptance Criteria

Probe specular amplitude is independent of how many sample slots were filled.
