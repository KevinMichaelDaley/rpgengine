---
id: rpg-gus3
status: open
deps: [rpg-qdr5]
links: []
created: 2026-07-21T22:42:08Z
type: feature
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen A1.1: cursive nameplate (text or SVG) on the dingbat facade

Dingbats often have the strangest names -- mount one in script on the front facade. Shared module assets/arch/proc/la/nameplate.py (B2 googie signs / F4 banners will reuse it): build_nameplate() takes EITHER name_text + name_font (cursive TTF/OTF path; system fonts found on this machine: Z003 Medium Italic at /usr/share/fonts/opentype/urw-base35/Z003-MediumItalic.otf, Lobster Two at /usr/share/fonts/opentype/lobstertwo/) OR name_svg (path; bpy svg import generates the curve). Curve -> extruded solid letters -> mesh (caps triangulated). Redo-panel params on la_dingbat: name_text (STRING, default a classic like 'The Capri'), name_font (STRING path, auto-discovered cursive default), name_svg (STRING path, overrides text), name_size (FLOAT, ~0.45 m), name_slant (FLOAT deg, default ~12 -- they were often mounted diagonally), name_x (FLOAT 0-1 fraction along the facade). Mount 1 mm proud of the stucco on the top-floor spandrel band, avoiding the loggia span. Vertex group 'nameplate' (add to VGROUPS). Rule 4 still applies: planar-projected UVs on the generated mesh. AUDIT EXEMPTION (documented): typography fill is inherently triangulated and cursive glyph overlaps self-intersect, so the nameplate object is audited for doubles/UVs only, NOT the all-quad/T-junction gate; state this in the smoke check beside the exemption.

## Acceptance Criteria

Acceptance: (1) passes programmatic topology validation; (2) every parameter in the redo panel, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session. Nameplate additionally: text route AND svg route both demonstrated; cursive font renders legibly at 0.45 m; slant and position params verified in the redo panel.

