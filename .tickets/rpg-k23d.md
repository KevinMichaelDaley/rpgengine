---
id: rpg-k23d
status: open
deps: []
links: []
created: 2026-07-21T01:35:15Z
type: epic
priority: 1
assignee: KMD
---
# Renderer performance on low-end hardware

Umbrella for the renderer performance review (ref/renderer_performance_issues_and_suggested_fixes.md, 2026-07-20). Goal: recover frame time on Intel Iris/Xe, Steam Deck (RDNA2 APU, 800p, shared memory), and older NVIDIA (Pascal/Maxwell) / AMD (GCN) WITHOUT changing the design or featureset -- (a) optimize the current pipeline, (b) add config toggles that gracefully degrade visuals. Children follow the review's four-phase order: config+defaults, cheap high-impact code, formats, structural. See section 9 for the phasing rationale and section 0 for the ranked top-10.

## Acceptance Criteria

All child tickets closed; low/med/high presets exist and measurably recover frame time on the low-end targets, verified with Tracy Render.* zones (using timer queries, not the PROF glFinish path).

