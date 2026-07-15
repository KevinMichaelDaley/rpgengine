---
id: rpg-9z96
status: open
deps: [rpg-pldk, rpg-nvw0]
links: []
created: 2026-07-13T06:41:35Z
type: task
priority: 2
assignee: KMD
parent: rpg-dweu
---
# Sorted transparent render pass (back-to-front alpha blend)

A transparent pass after the opaque forward+ (and deferred) passes: gather transparent renderables, sort back-to-front by view depth, alpha-blend over the opaque image (depth-test read, no/sorted depth write).

## Design

Core renderer, new pipeline pass. Depends on the material transparency attrs and the forward+ pipeline (rpg-nvw0). OIT is a later option.

