# 0022 — Parametric 3D reference geometry

Status: owner-approved feature scope, 2026-11-05. The owner requested finding
SolidWorks documentation and implementing axes, planes and points in 3D space.
When asked about initial construction workflows, the owner explicitly chose
“Both reference-based construction and direct XYZ/direction entry”.

## Reference and supported methods

The approved feature is Reference Geometry, independent of full spatial sketch
solving. Official documentation:

- [Reference geometry](https://help.solidworks.com/2026/english/solidworks/sldworks/c_Reference_Geometry_Overview.htm)
- [Reference Axis PropertyManager](https://help.solidworks.com/2023/English/SolidWorks/sldworks/hidd_dve_ref_axis.htm?format=P&value=)
- [Plane PropertyManager](https://help.solidworks.com/2026/english/SolidWorks/sldworks/hidd_dve_create_plane.htm)
- [Point PropertyManager](https://help.solidworks.com/2025/English/SolidWorks/sldworks/hidd_dve_refpoint.htm?format=P&value=)

Initial methods are XYZ point; point on existing point/vertex; axis–plane and
two-axis point intersections; point projection onto a plane; XYZ/direction axis;
axis along a line/straight edge/axis or cylindrical/conical face; two-point and
two-plane axes; a point with plane-normal axis; XYZ/normal plane; offset plane;
plane through three points; plane at an angle about an axis; parallel plane through
a point. The full set of SolidWorks methods is not claimed or exposed as placeholders.
No proprietary source or graphics were used, and no live SolidWorks interaction is
claimed for this task. Direct coordinate entry is the owner's explicit addition.

## Model and persistence

ReferenceGeometry is an ordered non-solid feature with a stable ID and a typed
construction method. A Qt-independent modeling utility evaluates point, axis and
plane frames. A document adapter resolves references to built-in origin geometry,
earlier datums, stable sketch entities/points, and conservative solid topology.
Definitions are regenerated in history order; references to later/missing entities,
wrong types or nonunique intersections reject the entire candidate. Reference
geometry never becomes a sketch contour or the current solid-producing feature.

Schema 13 stores definitions, parameters and references. Versions 1–12 remain
readable. Newer files reject safely in older applications. Sketch plane support can
reference an earlier plane ID, with its derived frame rebuilt before solving local
2D coordinates. Frame metadata is retained in contour extraction, nested region
construction, selection overlays, extrusion and cutting. The existing PlaneGCS
backend and mathematical sketch constraints remain unchanged.

Solid face/edge/vertex references retain the previously approved conservative
strategy: source feature ID, topology index, and exact BRep signature. A changed
supporting solid needs explicit reselection; this task does not introduce permanent
topology naming. Sketch-entity and datum references follow their stable IDs when
geometry moves. Dependencies are explicit and validated at load and regeneration.

## Interaction

Features ribbon and Insert → Reference Geometry open the common PropertyManager
shell. Methods show only relevant parameters and type-filtered reference lists.
References can also be picked geometrically in the viewport; point and straight
edge picks use logical-pixel distance, and faces use actual ray/face intersections.
The next unfilled reference becomes active automatically. Lists provide explicit
selection when projected geometry overlaps. Coincident picks prefer created datum
features over built-ins, preserving intended dependencies at the origin.

Preview geometry is temporary and does not create history entries. Confirm commits
one undoable edit; Cancel/Escape preserves the original document and dirty state.
Enter from numeric fields or the viewport confirms only a valid definition. A
selected plane or planar face preselects Offset, with the distance text selected.
Features are selectable and editable from the tree. A selected reference plane
supports Sketch directly, including non-origin/non-axis-aligned extrusion and cut.
Reference geometry is hidden during sketch editing and restored in part view.

Planes/axes are mathematically unbounded; their 50 mm half-extent display patches
are only selection/display aids. Points/axes are visibly marked and labelled.
Fit All includes datums. Hover/selection override base colors. In part selection,
an existing solid face takes precedence over a plane patch covering it.

No dependencies or licenses were added. Full 3D sketch constraints, general curved
surface projection/tangent planes, midplanes, point distributions and datum-feature
deletion/reordering are outside this initial implementation. The preserved feature
history already supports editing, Undo/Redo and save/reopen. See the verification
checklist for test evidence and owner visual checks; no further scope decision is
required for these implemented methods.
