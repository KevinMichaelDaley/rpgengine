---
id: rpg-c6o7
status: open
deps: [rpg-haqg]
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-0n7d
---
# §5.2 Collision Body and Joint Setup

See ref/scene_editor_design.md §5.2. This is the parent ticket for setting up per-bone collision geometry, constraint anchors, and physics joints in the prefab editor. Divided into subtasks below.

## Subtasks

1. **rpg-2585** — Per-bone collision body inspector (shape selection, parameters, auto-fit)
2. **rpg-9d13** — Convex hull bone colliders (markers, vertex selection, weight threshold)
3. **rpg-b7bc** — Constraint anchor system (placement, gizmos, storage)
4. **rpg-4e2b** — Joint type editor (dropdown, per-type panels, parameter editing)
5. **rpg-c25c** — Constraint viewport overlays (cones, arcs, limit visualization)

---

## §5.2.1 Per-Bone Collision Body Inspector

Each bone can have a collision body with one of these shape types:
- **Box** — half-extents (x, y, z), auto-fit from mesh vertices weighted to bone
- **Sphere** — radius, auto-fit as bounding sphere of weighted vertices
- **Capsule** — radius + height, auto-fit along bone axis (head→tail)
- **Convex hull** — see §5.2.2

Inspector panel shows per-bone:
- Shape type dropdown (none / box / sphere / capsule / convex hull)
- Shape parameters (editable, with auto-fit button)
- Mass (kg), density option (compute mass from volume)
- Collision group / mask flags
- Auto-fit: compute tight-fitting shape from mesh vertices with bone weight > 0.1

---

## §5.2.2 Convex Hull Bone Colliders

Convex hulls for bone colliders cannot be naively inferred from all vertices weighted to a bone (the weight set is typically too large and includes distant vertices with small weights). Instead, the hull vertex set must come from one of:

1. **Manual marker placement** — spawn MARKER entities in prefab mode, parent to bone. All markers under a bone with hull collider type define the hull vertices. (Already supported via rpg-kte9.)

2. **Mesh vertex selection** — in mesh mode, select specific vertices, then assign them to a bone's hull. The selected vertex positions (in bone-local space) define the hull.

3. **Vertex group** — named vertex groups (weight paint groups) with a configurable **minimum weight threshold** for inclusion. Vertices with weight >= threshold for that bone are included in the hull. Default threshold = 0.5 (excludes low-influence vertices that would bloat the hull).

4. **Weight threshold auto-hull** — given a bone and a weight threshold (e.g., 0.8), automatically collect all mesh vertices with bone weight >= threshold, compute convex hull. Inspector shows vertex count and warns if > 64 (PHYS_CONVEX_MAX_VERTS). User can adjust threshold slider to include/exclude vertices and see hull update in real-time.

### Hull Constraints
- Minimum 4 vertices for valid hull (tetrahedron)
- Maximum PHYS_CONVEX_MAX_VERTS (64) — warn if exceeded, offer decimation
- Hull wireframe renders in collision overlay, updates live during editing

---

## §5.2.3 Constraint Anchor System

Constraint anchors are **attachment points** on bones where joints connect. Each bone can have multiple anchors. Anchors are positioned and oriented in bone-local space and define where constraint forces are applied.

### Anchor Properties
- **Position** — local-space offset from bone head (3D vector)
- **Orientation** — local-space rotation (quaternion / euler)
- **Name** — optional label for identification (e.g., "shoulder_front", "elbow_hinge")
- **Bone index** — which bone this anchor belongs to

### Anchor Gizmos
Anchors have their **own independent gizmos**, separate from the bone gizmo:
- Rendered as small diamond/cross markers at the anchor's world position
- **Selectable** — click an anchor in the viewport to select it (like clicking an entity pivot)
- **Movable** — translate gizmo to reposition the anchor in bone-local space
- **Rotatable** — rotation gizmo to orient the anchor (defines constraint axis for hinges, etc.)
- Gizmo respects world/local basis toggle (world axes vs bone-local axes)
- Multiple anchors on the same bone render as distinct markers; clicking near one selects it
- Selected anchor highlighted with a different color (e.g., yellow vs grey for unselected)

### Anchor Workflow
1. Select a bone in the outliner or viewport
2. Inspector shows "Anchors" section with list of existing anchors
3. Click "Add Anchor" → new anchor at bone head with default orientation
4. Select the anchor → gizmo appears → drag to position, rotate to orient
5. When creating a constraint, pick two anchors (one per bone) as endpoints

### Commands
- `anchor_add <bone_id>` — add anchor at bone head
- `anchor_delete <anchor_id>` — remove anchor
- `anchor_list [bone_id]` — list anchors on bone

### Storage
- Anchors stored in the .fskel or .fpfab file alongside bone data
- Per-bone array of anchor descriptors (position, orientation, name)
- Serialized as part of the skeleton/prefab definition

---

## §5.2.4 Joint Type Editor

Inspector panel for editing physics joints between two anchors:

### Joint Creation Workflow
1. Select first anchor (click in viewport or pick from dropdown)
2. Select second anchor (on a different bone)
3. Choose joint type from dropdown
4. Joint created with default parameters; inspector shows per-type panel

### Per-Type Parameter Panels

| Joint Type | Parameters |
|---|---|
| **Distance** | rest_length, min_distance, max_distance, stiffness, damping |
| **Ball** | (no axis params — free rotation) stiffness, damping |
| **Hinge** | axis (local), angle_min, angle_max, stiffness, damping |
| **Lock** | (no free DOF) break_force, break_torque |
| **Twist** | axis, twist_min, twist_max, stiffness, damping |
| **Cone-twist** | swing1_limit, swing2_limit, twist_limit (3-axis limits) |
| **Copy-rotation** | source_bone, influence (0–1), axis_mask |
| **Limit-rotation** | per-axis min/max angles |
| **Limit-position** | per-axis min/max offsets |
| **Aim** | target_bone, up_axis, influence |
| **IK** | target, pole_target, chain_length, iterations |

### Common Parameters (all joint types)
- Stiffness, damping, compliance
- Drive mode (off / position / velocity)
- Drive target, max_force
- Break force / break torque (0 = unbreakable)
- Enable/disable toggle
- Animation damping factor (blend between animation and physics)

### Constraint Stack
- Each bone pair can have multiple constraints (stacked)
- Constraint list in inspector with drag-to-reorder
- Per-constraint enable/disable checkbox
- Delete button per constraint

---

## §5.2.5 Constraint Viewport Overlays

Visual rendering of constraints and their limits in the 3D viewport:

- **Anchor markers** — small diamonds at anchor world positions, color-coded (grey=unselected, yellow=selected, red=broken)
- **Connection lines** — colored line/curve between the two anchors of each constraint
- **Hinge** — arc showing allowed rotation range around hinge axis
- **Cone-twist** — solid/wireframe cone showing swing limits, twist range as arc
- **Distance** — dashed line at rest length, solid within min/max range
- **Limit-rotation** — per-axis colored arcs showing allowed rotation ranges
- **Limit-position** — per-axis colored bars showing allowed translation ranges
- **IK chain** — highlighted bone chain from root to effector
- **Broken constraints** — red dashed line, X marker at midpoint
- Selected constraint overlay is brighter/thicker
- Toggle visibility: `V` key cycles collision overlay modes (off / shapes only / shapes + constraints)

---

## Acceptance Criteria

- [ ] Inspector shows collision body section per bone (shape type, parameters, auto-fit)
- [ ] Primitive shapes (box/sphere/capsule) with auto-fit from mesh
- [ ] Convex hull via markers, vertex selection, vertex group, or weight threshold
- [ ] Weight threshold slider with live hull preview in viewport
- [ ] Constraint anchors placeable per bone with independent gizmos
- [ ] Anchor gizmos movable/rotatable, selectable in viewport like pivots
- [ ] Joint type dropdown with per-type parameter panels
- [ ] All 11 joint types supported with full parameter editing
- [ ] Constraint stack with enable/disable and reorder per bone pair
- [ ] Constraint limits visible as colored overlays (cones, arcs, etc.)
- [ ] Anchor markers visible at world positions with selection highlighting
- [ ] Undo/redo for anchor and constraint create/delete/edit
- [ ] Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md

