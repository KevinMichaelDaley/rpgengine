---
id: rpg-eiua
status: closed
deps: [rpg-igs5]
links: []
created: 2026-07-05T22:56:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-3sff
tags: [srd, cleanup]
---
# srd-loop-03: remove dead SymX files from build

Remove the 8 obsolete source files from the Makefile and delete them: srd_loop.cpp, srd_energy.cpp, srd_optimizer.cpp, srd_loss_gradient.cpp, srd_eikonal.cpp, srd_transport.cpp, srd_loss_primitives.cpp, srd_loss_compiler.cpp. Verify build succeeds. Update srd_grammar.c to strip the rule table (keep only grid parsing functions).

## Design

Makefile: remove files from SRC wildcard exclusion list or explicitly exclude. srd_grammar.c: remove srd_rule_table(), srd_rule_count(), srd_rule_apply_to_node(), srd_grammar_collect_tiles() (moved to srd_layout_to_tiles). Keep: srd_grid_parse, srd_grid_region_at, srd_grid_adjacent_regions, srd_grid_region_symbol.

## Acceptance Criteria

make builds without errors; make test passes; no references to SymX in any remaining source file (grep confirms); srd_grammar.c contains only the 4 grid functions; deleted files are gone from repo


## Notes

**2026-07-06T06:10:10Z**

DESIGN REVISION (2026-07-06): Also remove old box-based layout files (srd_sdf_layout.h/c, srd_sdf_layout_ops.c, srd_sdf_layout_sdf.c, srd_sdf_layout_raster.c), box-based rule files (srd_rules_room_annex.c, srd_rules_room_add.c, srd_rules_room_split.c, srd_rules_room_modify.c), and the continuous phase (srd_continuous_phase.h/cpp). These are all superseded by the voxel SDF grid + voxel rewrite rules.
