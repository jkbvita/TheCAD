# 0011 — Shared-edge regions and direct Smart Dimension

Status: owner approved, 2026-11-04. Evidence: owner's screenshot of a rectangle
with triangular roof and the request to reproduce selection/placement behavior
in SolidWorks. The owner separately answered “Yes, include angular dimensions”.
No new kernel, solver, dependency, license or permanent architecture is selected.

## Selected regions

Support adjacent planar line-bounded regions sharing existing complete sketch
edges and vertices, in addition to independent/nested loops. The graph evaluator
walks bounded faces of the planar embedding instead of requiring every connected
component to have degree two. Open bridges are excluded from boundaries. Each
region retains the exact source entity IDs of its boundary. Selecting adjacent
regions fuses their extrusions; they remain independently selectable and editable.
When adjacent cells are enclosed by another loop, shared internal edges cancel
from the enclosing hole boundary. No entity is trimmed/deleted by recognition.

Unsplit crossings, partial overlaps, unsplit T-junctions and intersections involving
circles/legacy rectangle boundaries still reject. Automatic subdivision would need
additional persistent region-reference semantics; it is not invented here.

## Smart Dimension interaction

The primary command is now viewport driven: activate it, select geometry, move to
preview, click to place, then enter a value in a small floating Modify editor next
to the annotation. Enter confirms; Escape/Cancel rejects the pending edit without
changing the sketch. The tool stays active for repeated dimensions until Escape,
Select, another drawing tool, Finish Sketch or toggling Smart Dimension off.
A single explicit preselection is also accepted. The relation manager remains a
separate inspection/deletion/geometric-relation command.

One line or two points infer horizontal/vertical/aligned distance from placement.
One circle defaults to diameter; R chooses radius and D diameter before placement.
Two parallel lines infer perpendicular spacing, which preserves parallelism as
part of the dimension definition. Existing orientation equations are reused where
proved by relations/fixed endpoints, rather than creating duplicate derived
parallel equations. No stored owner constraint is silently disabled.
Two nonparallel lines infer angular dimension. Placement chooses the bounded
sector, including acute/obtuse variants, using stable endpoint direction roles
and sense. Current angular targets are strictly between 0 and 180 degrees.
Straight/reflex sectors and other SolidWorks dimension types remain future scope.

Reference status is optional in the floating editor. Double-clicking an existing
annotation opens that editor directly, preserving its stable ID and mathematical
references. Failed solves/regeneration leave the editor available for correction
and retain the last valid sketch/body. Annotation placement remains independent
of the equation. No dimension type or value form is required in the left manager.

## Persistence

Version 6 adds distance, line-distance and angle dimension kinds. Angle values are
degrees; linear values are mm. Existing fields store stable references, value,
sense, driving/reference status and annotation. Versions 1–5 remain readable.
Version 5 cannot be used to disguise the new kinds. No backend types are stored.

## Reference inspection

The computer-use skill was used to inspect the prepared SolidWorks sketch. The
single-line selection, placement and adjacent Modify editor were exercised; one
measured dimension was added without changing its numeric value or saving. The
desktop tool later reported physical Escape, and all further desktop control was
stopped. Angular behavior was owner approved and tested in TheCAD; it was not
claimed as exercised in the SolidWorks demo.

Primary reference: [SOLIDWORKS sketch dimensioning](https://help.solidworks.com/2026/english/SolidWorks/sldworks/t_Dimensioning_a_2D_Sketch.htm)
and [official 2025 education fundamentals](https://www.solidworks.com/sites/default/filesd10/2025-04/SOLIDWORKS_Fundamentals_3D_Design_Simulation.pdf).
This implements the approved supported interactions, not every SolidWorks tool.
