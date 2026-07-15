---
id: rpg-cn9e
status: open
deps: [rpg-cbax]
links: []
created: 2026-07-13T06:41:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-nt4y
---
# POM visual test + tuning (shallow parallax, deep shadow, min stretch)

Visual test on a real heightmap showing convincing parallax with minimal stretch at grazing angles (shallow displacement) yet deep correct self-shadows (full-depth march), with low step counts. Tune the depth/step parameters.

## Design

Core renderer visual test (SDL/GL, PPM screenshot).

