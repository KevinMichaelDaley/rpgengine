---
id: rpg-1se7
status: open
deps: [rpg-hnkn]
links: []
created: 2026-07-13T06:41:35Z
type: task
priority: 2
assignee: KMD
parent: rpg-dweu
---
# Baker: coloured transmission in direct + solve

Direct and radiosity-solve light transport tints transmitted light by the transparent materials it passes through (coloured, attenuated), so a coloured pane casts a coloured light patch in the bake.

## Design

Core baker. Depends on transmittance visibility (H3). Applies transmittance colour along shadow/form-factor rays.

