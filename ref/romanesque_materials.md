# Romanesque architectural materials — PBR reference

Parameter source for the procedural Blender material library (epic **rpg-lb1q**,
child of **rpg-pm1c**) that clothes the UV-mapped `column.py` / `arch.py` /
`vault.py` assets. Each material below has a concrete PBR block that drops
straight into a Blender **Principled BSDF**, plus a **seed-texture note** that
drives the Gemini/OpenRouter generation prompts (ticket **rpg-ljxt**) and the
node-graph defaults (ticket **rpg-lbky**).

This first pass targets **Romanesque** (c. 1000–1200): heavy, load-bearing
masonry — locally quarried limestone/sandstone ashlar and rubble, plastered and
frescoed vault soffits, marble or granite shafts where wealth allowed, fired
brick in Italy/Germany, and sparing bronze/iron/gilt fittings. Finishes are
**dry, matte, tool-dressed** — not the wet, polished extremes of modern stone.

---

## Conventions

- **Colour space.** `base_color` is given as **sRGB** hex (what you type into
  Blender's colour picker in the default *sRGB* field) and as approximate
  **linear** RGB (what the shader actually uses). Convert
  `lin = (srgb/255)^2.2`. Seed images are sampled in their own colour space
  (albedo detail = sRGB/Non-Color-aware tint source; roughness/spec = Non-Color).
- **Blender Principled mapping.** Dielectrics: `Metallic = 0`, `IOR = 1.5`
  (= *Specular IOR Level* 0.5). Metals: `Metallic = 1`, and the **Base Color is
  the F0 reflectance colour** (its "metal tint"), `IOR` ignored.
- **UV scale.** All assets carry uniform texel density at `UV_SCALE = 1.0`
  UV/metre (1 UV unit = 1 real metre). The `tiling` column below is the
  real-world feature size, so a seed sampled at that scale reads correctly with
  no per-asset tweaking.
- **Randomisation.** Every material exposes a small jitter so instances differ:
  `hue_jitter` (± in HSV hue, small), `value_jitter` (± multiplicative on
  brightness), `rough_jitter` (± on roughness). The node graph seeds these from
  object info / a random-seed input (ticket rpg-lbky).
- **Relief.** `normal_strength` is a hint for the height→normal derived from the
  seed detail — the physical depth of the surface texture (mortar joints, tooling
  marks, plaster mottle), in mm at the tiling scale. Bump only; no displacement.

---

## Master table

| Material | Base color sRGB (hex) | Linear RGB ≈ | Metallic | Roughness | IOR | Normal (mm) | Tiling (m) |
|---|---|---|---|---|---|---|---|
| Limestone (dressed ashlar) | `#CFC7AE` | 0.60, 0.56, 0.42 | 0 | 0.65–0.85 | 1.5 | 1–3 | 0.9 course |
| Sandstone (dressed) | `#C2A579` | 0.53, 0.38, 0.20 | 0 | 0.70–0.90 | 1.5 | 1.5–4 | 0.9 course |
| Tufa / travertine | `#D8CDB4` | 0.68, 0.60, 0.44 | 0 | 0.60–0.85 | 1.5 | 2–6 (voids) | 0.6 |
| Granite (dressed/honed) | `#8C8A88` | 0.28, 0.27, 0.26 | 0 | 0.30–0.65 | 1.55 | 0.3–1 | 0.4 |
| Flint (knapped, in mortar) | `#454B4E` core / `#B9B4A6` cortex | 0.06, 0.07, 0.08 | 0 | 0.35–0.6 core | 1.5 | 3–8 | 0.15 nodules |
| Marble (shaft/revetment) | `#D4CAC0` | 0.83, 0.79, 0.75 | 0 | 0.20–0.45 | 1.5 | 0.2–0.6 | 1.2 vein |
| Fired brick | `#8A4B3A` | 0.27, 0.10, 0.07 | 0 | 0.70–0.90 | 1.5 | 1–3 | 0.075×0.23 |
| Terracotta | `#8E361C` | 0.55, 0.21, 0.11 | 0 | 0.65–0.85 | 1.5 | 0.5–2 | 0.3 |
| Lime plaster / fresco ground | `#E4DED0` | 0.75, 0.71, 0.63 | 0 | 0.75–0.92 | 1.5 | 0.3–1 | 1.5 broad |
| Bronze (fittings) | `#B9803E` polished / patina `#5E7A5A` | 0.68, 0.36, 0.11 | 1 | 0.35–0.75 | — | 0.5–2 | 0.2 |
| Wrought iron | `#3C3A38` dark / `#6E6A64` bright | 0.05, 0.05, 0.04 | 1 | 0.45–0.8 | — | 1–3 | 0.25 |
| Gold leaf / gilding (accent) | `#E8C24E` | 0.79, 0.55, 0.09 | 1 | 0.12–0.35 | — | 0.1–0.5 | 1.0 |
| Oak timber (doors/tie-beams) | `#6B4E32` | 0.15, 0.08, 0.035 | 0 | 0.5–0.75 | 1.5 | 0.5–2 | 0.6 grain |

Metal base colours are anchored to measured F0 reflectance (iron
0.53/0.51/0.49, copper 0.93/0.62/0.52, gold 1.06/0.77/0.31, brass
0.91/0.78/0.42 sRGB, per physicallybased.info); bronze sits between copper and
brass. Stone/masonry IORs are the dielectric 1.5 (granite slightly higher for
its quartz/feldspar sheen).

---

## Per-material detail + seed-texture notes

Seeds are **flat, evenly-lit, low-contrast, mostly mid-neutral** detail sources
(they behave like AI noise/pattern textures) — tint and roughness come from the
params, not from baked lighting. Generate **3–4 albedo-detail** variants and
**2–3 spec/roughness-detail** variants per material for random sampling.

### Stone

- **Limestone** — the default Romanesque wall/voussoir/column stone. Buff to
  pale grey, fine even grain, soft claw-tooth/boaster tooling marks, thin lime
  mortar joints. `hue_jitter ±0.02`, `value_jitter ±0.12` (colour varies bed to
  bed). *Seed:* fine speckled grain + faint parallel tooling striations + sparse
  darker inclusions; a separate joint/mortar mask at ~0.9 m courses.
- **Sandstone** — warmer, tan/ochre, visible sedimentary bedding lines, slightly
  coarser and more porous than limestone; can show iron banding.
  `hue_jitter ±0.03`, `value_jitter ±0.15`. *Seed:* directional bedding
  striations + granular speckle; roughness seed a touch rougher than limestone.
- **Tufa / travertine** — creamy, lightweight, **pitted with elongated voids**;
  common where light vaulting stone was wanted. *Seed:* smooth cream ground with
  clustered dark pores/holes (drives both albedo darkening and a deeper normal in
  the pits); high normal strength in voids.
- **Granite** — grey (with pink/black fleck) crystalline; harder to work, used
  for shafts/bases where available. Lower roughness than the sedimentary stones
  (crystal facets glint). `hue_jitter ±0.02`. *Seed:* dense multi-tone speckle
  (grey base, scattered pink/white/black crystals), very little directional
  structure; roughness seed with per-crystal variation.
- **Flint** — dark grey-black knapped nodules set in thick mortar (rubble walls,
  regional). Two-tone: glassy dark core + pale chalky cortex rim. *Seed:*
  irregular rounded nodule mask (dark glassy centres, light cortex edges) over a
  light mortar matrix; small tiling (~0.15 m).
- **Marble** — prestige shafts/revetment. Pale, translucent-looking, soft grey
  **veining**, lowest roughness (honed, not mirror). `value_jitter ±0.08`.
  *Seed:* gentle branching vein network + faint cloudy mottle, very low contrast;
  roughness seed nearly flat with subtle polish variation.

### Masonry / ceramic

- **Fired brick** — Lombard/Rhenish Romanesque. Red-brown, matte, slight per-
  brick colour variation and a running-bond joint pattern (~75 mm × 230 mm).
  `hue_jitter ±0.03`, `value_jitter ±0.18` (brick-to-brick). *Seed:* fine clay
  grain + occasional darker fire-flash; separate bond/joint mask.
- **Terracotta** — decorative fired clay (corbels, plaques). Warmer orange-red,
  smoother than structural brick, low relief. *Seed:* smooth clay mottle with
  fine surface pitting.

### Plaster / paint

- **Lime plaster / fresco ground** — the **vault & dome intrados** finish (pairs
  with `build_dome(interior_lens=…)`). Off-white/cream, matte, broad soft mottle
  and faint trowel sweeps; can carry pigment washes. Highest roughness.
  `value_jitter ±0.1`. *Seed:* very broad low-frequency mottle + faint trowel arcs,
  almost no high-frequency detail; roughness seed near-uniform high.

### Metals

- **Bronze** — door furniture, grilles, cap-accents. Warm gold-brown when
  burnished; commonly a **green/brown patina** in recesses. Provide the polished
  base colour and a patina colour; the node graph blends patina into cavities via
  the seed's dark/AO channel. Roughness rises with patina (0.35 clean → 0.75
  corroded). *Seed:* brushed micro-scratches + blotchy patina mask.
- **Wrought iron** — hinges, straps, grilles. Dark near-black, low saturation,
  hammered facets; brighter on wear edges. *Seed:* hammer-planished dimples +
  streaky wear; roughness seed varied (worn spots lower).
- **Gold leaf / gilding** — sparing accent (halos, inscriptions). Warm yellow,
  low roughness, very thin relief over its substrate. Keep coverage small — use
  as a masked accent, not a whole-surface material.

### Timber

- **Oak** — plank doors, tie-beams, centring left in place. Mid-brown, directional
  grain, medium roughness, adze/saw marks. `hue_jitter ±0.03`. *Seed:* directional
  grain lines + ray fleck + occasional knot; roughness seed follows grain.

---

## Notes for downstream tickets

- **rpg-ljxt (seed generation):** one prompt per material built from its *Seed*
  note above; enforce **flat lighting, seamless-tileable, mid-neutral, no baked
  shadow/highlight**. Emit separate albedo-detail and roughness/spec-detail
  images into `assetsrc/materials/<material>/`.
- **rpg-lbky (node graph):** the table columns are the node-group input defaults;
  `*_jitter` columns are the randomisation ranges; `tiling` sets the seed sample
  scale against the existing UVs (do not re-unwrap). Joint/void/patina masks
  listed in the seed notes are the secondary channels the graph blends over the
  base tint.
- Suggested first assignments for the acceptance render: **column** → marble or
  granite shaft on a limestone base; **arched doorway** → limestone ashlar with
  the voussoir band picked out; **dome/vault intrados** → lime-plaster/fresco
  ground.

---

## Sources

- [Physically Based — the PBR values database](https://physicallybased.info/) — marble, concrete, brick, terracotta, iron, copper, gold, brass base colours and IOR.
- [PBR Color space conversion and Albedo chart — Iri Shinsoj (ArtStation)](https://www.artstation.com/shinsoj/blog/Q9j6/albedo-chart) — dielectric albedo 60–240 sRGB range, specular F0 0.02–0.05, metal albedo 0.5–0.98.
- [PBR — From Rules to Measurements (racoon-artworks)](https://www.racoon-artworks.de/blog_PBRfromrulestomeasurements.php) — measured F0 values (iron, copper, gold).
- [Romanesque building materials — Ancient Worlds Archive](https://ancientworldsarchive.com/romanesque-building-materials/) and [Romanesque architecture — Wikipedia](https://en.wikipedia.org/wiki/Romanesque_architecture) — material palette (limestone, sandstone, granite, flint, brick; plastered/painted interiors, ashlar-over-rubble).
- Stone colour anchors: [flint #B3B1B0](https://kidspattern.com/color/name/flint-stone-b3b1b0/), [limestone #DCD8C7 / #bebd8f](https://encycolorpedia.com/bebd8f), [sandstone #b29082 / #d4bf8e](https://encycolorpedia.com/b29082).
