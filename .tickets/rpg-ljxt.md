---
id: rpg-ljxt
status: in_progress
deps: [rpg-9hw5]
links: []
created: 2026-07-10T06:31:51Z
type: task
priority: 1
assignee: KMD
parent: rpg-lb1q
tags: [arch, materials, gemini, openrouter, textures]
---
# Gemini/OpenRouter seed-texture generation (flat albedo + specular)

Generate a bank of **sample-able flat seed textures** — albedo and specular /
roughness maps — for each Romanesque material, using **Gemini via OpenRouter**
(API key at ~/.ssh/OPENROUTER_API_KEY), and store them checked-in under
assetsrc/.

## Intent

These are NOT finished PBR textures. They are deliberately **flat**, evenly-lit,
tileable-ish detail sources that behave like **AI-generated noise/pattern
textures tuned to the target material** — grain, mottling, veining, tooling
marks, mortar speckle, patina. The material node graph (sibling ticket) samples
and blends several of these, tinted/roughened by the PBR params from the
research ticket, to build the final base maps. So each image should be:

- low dynamic range / flat lighting (no baked highlights or directional shadow)
- mostly mid-grey/neutral so tint comes from the params, not the image
- a few variants per material (so the node graph can randomly sample)
- separate albedo-detail and specular/roughness-detail variants

## Work

- A script (e.g. scripts/gen_material_seeds.py) that reads the material list +
  prompts (from the research doc), calls the OpenRouter image endpoint with a
  Gemini image model, and writes PNGs to assetsrc/materials/<material>/.
- Reuse the OpenRouter pattern already used for the dungeon image dataset
  (see reference_dungeon_image_dataset memory / scripts/gen_dungeon_images.py)
  — same key file, model = a Gemini image model.
- Prompts tuned per material toward FLAT, seamless, noise-like detail.
- Commit the generated seeds into assetsrc/ (they are inputs, kept in-repo).

Depends on the research ticket for the material list and per-material prompt
notes.

