# Assembly mates: implementation and test guide

Implemented under owner-approved decision 0056 on `codex/complete-assembly-mates`.
This report distinguishes working functionality from complete SolidWorks parity.
Only official documentation was consulted; no local SolidWorks GUI inspection
or knowledge of its proprietary solver is claimed.

## Available relationships

All 22 main types have executable equations, persisted definitions and editable
PropertyManager controls. Invalid selections reject explicitly.

| Family | Types |
| --- | --- |
| Standard | Coincident, Concentric, Parallel, Perpendicular, Tangent, Lock, Distance, Angle |
| Advanced | Profile Center, Symmetric, Width, Path Mate, Linear/Linear Coupler, Limit Distance, Limit Angle |
| Mechanical | Cam, Slot, Hinge, Gear, Rack and Pinion, Screw, Universal Joint |

The three tabs provide named selection groups, alignment and numeric options,
previews, confirmation/cancel, editing, continued mating and common-reference
batch mating. Ctrl-selection offers compatible Quick Mates. Vertices, edges,
faces, origins and sketch/datum references are selectable. Picking uses logical
pixels; selections and previews preserve the camera.

- Coincident origins may align axes. Concentric supports relative rotation
  locking. Lock preserves relative pose, rather than globally fixing a component.
- Profile Center recognizes circles, rectangles and regular polygons, including
  rectangles recovered from supporting sides around corner treatments. It supports
  orientation/offset and circular rotation locking, without bounding-box substitutes.
- Width supports face pairs and single cylindrical/axis references, Centered,
  Free, Distance and Percent. Angular widths display degrees. Equal widths and
  unsupported mixed angular sets expose Centered only.
- Path uses exact curve arc lengths, connected chains, Free/Distance/Percent,
  tangent following and an optional up reference. Slots recognize straight and
  arced boundaries; slot-to-slot supports Free/Centered.
- Hinge combines concentric and seating groups with optional angle stops. Limit
  mates allow movement inside their bounds and reversal away from a stop.
- Gear uses editable or measured geometry ratios. Rack and Pinion and Screw use
  travel per revolution and reversal. Continuous phase survives complete turns,
  history and save/reopen. Screw uses relative twist, preserving pitch under
  common rigid motion. Singular 90-degree Universal Joints reject explicitly.
- Cam accepts closed tangent chains of planar/cylindrical extruded side faces,
  with point, planar or cylindrical followers. Picking a side face expands its
  closed tangent chain, excluding end faces. Normalized winding makes external
  roller contact independent of the picked seed face.
- General surface contact validates finite UV domains and trimmed faces; singular
  normals and invalid contact branches reject transactionally. Tangent mates now
  support finite straight edges against non-analytic surfaces by solving a shared
  surface/edge contact point and tangent direction. Infinite reference axes are
  not offered for that pair because they do not provide a bounded contact segment.

## Generated workflows

These have docked PropertyManagers and generate real document relationships.
They are not extra manual mate types.

- **Asset Publisher:** named component-local connectors from vertices, points,
  origins, circular centers or finite straight-edge midpoints; direction, optional
  ground plane and editable connector offsets/rotation. Publication is assembly-local.
  Repeated linked-source instances in that assembly reuse it.
- **Ground Plane:** root/component plane selection and optional grounding of
  published assets, without revealing hidden base planes or changing the camera.
- **Magnetic Mates:** proximity snapping during insertion/movement, configurable
  distance, `[` moving / `]` stationary connector cycling, editing and locks.
  Moving an unlocked asset retains its prior relationship as disconnected/suppressed
  with a diagnostic. Undo restores it.
- **Belt/Chain:** ordered parallel pulley references, measured/overridden diameters,
  side flips, optional location, thickness, engagement and driving length. Routing
  uses exact common tangents and circular arcs. An optional owned part contains the
  associative path sketch. Rigid pulley rotation reuses that part.
- **Lock To Sketch:** generated with the belt part; suppress/delete to float it.
  Deleting it does not silently recreate it.
- **In Place:** generated for an assembly-owned copy of a `.thepart` template, with
  associative support plane/origin and editable offset. Replace owned geometry
  from a template or export it. Unlink/delete preserves world placement. Linked
  user source files are never implicitly rewritten.

Owned parts, groups, parameters, anchors, continuous phases, publication and
active/suppressed/unsolved diagnostics persist in `.theasmb` schema 2. Schema 1
remains readable. Edits use candidate transactions, atomic saves and ten-action
history. Dependency deletion lists affected relationships/owned parts for review.
Invalid references never silently reattach.

## Verification and performance

Final verification (2026-11-15): the full Release application built successfully;
all 60 registered tests passed in 88.40 seconds, including the final cam refinements.
The separate package also passed absolute/relative Unicode-and-space filename
launch checks for parts and assemblies with development runtime paths removed.
The optional owner-model launch check was not configured. Focused
suites: `assembly.mate-solver`, `assembly.general-mates`, `assembly.typed-document`,
`assembly.generated-workflows`, `assembly.document-session`, `ui.assembly-workflow`
and its Light/150% scaling variant.

Tests cover residuals/remaining freedom for all types, geometry recognition,
Width modes, limits, paths, conical tangency, tangent cam chains, multi-turn Gear
and relative Screw motion, conflicts/force/unsolved states, suppression/deletion,
cancel/undo/redo, source regeneration and schema validation/roundtrips. Generated
fixtures cover belt engagement/driving length/owned-part reuse, support updates,
magnetic snapping/locking/disconnection and safe dependency deletion. GUI tests
exercise the new PropertyManagers and compare camera eye/target/up/scale/projection.

Independent graph blocks, immutable shared source/reference caches and retained
presentations preserve the previous responsiveness work. A development run of the
six-instance/eight-cylinder fixture opened Mate in 24–25 ms and ran five value
previews in 3–4 ms (Dark and Light/150%). These are fixture timings, not a guarantee for every model.
No new dependencies/licenses. Part modeling, PlaneGCS and `.thepart` are preserved.

## Remaining differences

This is a testable expanded mate implementation, not complete SolidWorks parity.

- Linked misaligned-concentric deviation options are not implemented.
- Cam requires planar/cylindrical extruded side faces; general spline cam surfaces,
  arbitrary face-chain contact and collision dynamics are not covered.
- Line-to-non-analytic-surface Tangent is limited to finite straight edges. Curved
  sketch entities and unbounded axes need separate contact equations.
- Some Standard entity pairs and complex nonparallel Width combinations reject.
  A failed nonlinear solve preserves the prior model; it does not prove that the
  physical configuration is impossible.
- Rack/Screw input is travel per revolution; separate pitch-diameter/inverse-pitch
  representations are not provided.
- Magnetic publication is assembly-local, not a portable part asset library.
- In-context creation copies templates; full sketch/feature authoring inside the
  assembly window is not included. The belt part is a path sketch, not a flexible solid.
- Arbitrary spatial mechanisms, large dense graphs and every geometry/option
  combination are not exhaustively validated. No force/friction/deformable simulation.

## Manual checklist

1. Insert parts, Ctrl-select faces and try Mate/Quick Mates. Change the suggested
   type, flip it, edit values and cancel a preview.
2. Try concentric rotation lock, Hinge, Width, Slot and a bounded slider. Drag
   inside limits, reverse from a stop, edit/delete/suppress and undo/redo.
3. Try Gear, Rack and Pinion, Screw, Path and Cam; turn through multiple revolutions
   and reopen the assembly.
4. Publish two assets, choose Ground Plane, move into range, cycle connectors with
   brackets and test locked/unlocked magnetic connections.
5. Create/edit Belt/Chain: diameter/order/side/thickness, engagement, driving length,
   optional owned part, deletion and undo.
6. Create an in-context template part, edit support/offset, change the source plane
   and inspect dependency deletion. Check its unlinked state after save/reopen.
7. Check camera stability, Dark/Light readability and assembly save/reopen. Keep
   original files backed up when testing schema-2 saves.

Official references: [research catalog](solidworks-assembly-mates.md),
[Standard Mates by Entity](https://help.solidworks.com/2025/english/SolidWorks/sldworks/r_Standard_Mates_by_Entity.htm),
[Profile Center](https://help.solidworks.com/2021/English/SolidWorks/sldworks/t_profile_center_mate.htm),
[Width](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_Width_Mates_SWassy.htm),
[Belt/Chain](https://help.solidworks.com/2023/english/solidworks/sldworks/HIDD_DVE_FEAT_BELT.htm),
[Magnetic Mates](https://help.solidworks.com/2026/English/SolidWorks/sldworks/c_magnetic_mates_intro.htm).
