---
id: rpg-lb1q
status: open
deps: []
links: []
created: 2026-07-10T06:31:12Z
type: epic
priority: 1
assignee: KMD
parent: rpg-pm1c
tags: [arch, materials, blender, pbr, procgen]
---
# Procedural Blender materials for architectural assets

A library of **procedural, parametric Blender materials** for the UV-mapped
architectural pieces produced under rpg-pm1c — columns (column.py), arches /
doorways (arch.py), and vaults / domes (vault.py). All of these now carry
clean, uniform-texel-density UVs (UV_SCALE = 1.0 UV/metre, cross-object), so a
tiling material reads consistently across the whole set.

## Goal

Each material is a node graph driven by a **small PBR parameter set** (base
colour range, roughness, specular/IOR, metallic, normal strength, tiling) plus a
bank of **seed textures** that inject believable surface detail. The seed
textures are AI-generated, deliberately *flat* albedo / specular maps that
behave like tunable noise/pattern sources — not full photoscans. A node network
randomly samples and blends the seeds, tinted and roughened by the params, to
build the base maps (albedo, roughness, specular, and a derived normal/height)
at material-evaluation time.

## First target: Romanesque

Start with the Romanesque palette (the reference architecture for this asset
set): dressed limestone/sandstone ashlar, lime plaster / fresco ground, marble
shafts, and metal fittings (bronze/iron). Later styles reuse the same node
framework with different seed banks and params.

## Pipeline (see child tickets)

1. Collect the Romanesque material types + their PBR parameters (research, via
   the search tool) into a reference doc.
2. Use Gemini (via OpenRouter, key at ~/.ssh/OPENROUTER_API_KEY) to generate a
   bank of sample-able flat albedo + specular seed maps per material — flat,
   noise/pattern-like, tuned to each material. Store them in assetsrc/.
3. Build the Blender node structure that randomly samples and constructs the
   base maps from the seed images using the params, and apply it to the
   column / arch / dome assets.

## Conventions

Materials and any Python that builds/assigns them live alongside the generators
under assets/arch/proc/ (a materials module); source seed images live in
assetsrc/ (checked in). Reuse the existing UV convention — do not re-unwrap the
meshes.

