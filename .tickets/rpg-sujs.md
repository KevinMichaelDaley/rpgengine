---
id: rpg-sujs
status: open
deps: [rpg-h4ez, rpg-0z5c]
links: []
created: 2026-07-13T05:24:17Z
type: task
priority: 2
assignee: KMD
parent: rpg-hrb6
---
# Dynamic-object ambient from probes in material shader + test

In the material shader, dynamic objects take their ambient term from interpolated SH probes (evaluated against the shaded normal), paralleling the static lightmap term; combine with shadows. Test.

## Design

Core renderer. Depends on probe sampling + the PBR shader (rpg-w1qe).

