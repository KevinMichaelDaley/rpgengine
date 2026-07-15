---
id: rpg-7v7w
status: open
deps: [rpg-lkxp, rpg-ya6c]
links: []
created: 2026-07-13T05:23:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-fsvq
---
# Static shadow-map pre-generation (render all static casters once per stationary light)

Render every STATIC shadow caster once per stationary light into its cascaded shadow map and cache it; not redrawn per frame. Rebuild on static-scene change.

## Design

Core renderer. Depends on shadow resources + CSM setup. Persist/cache the static depth per light.

