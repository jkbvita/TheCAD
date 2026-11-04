# Parametric sketch constraints and Smart Dimension

Owner-approved scope: [PlaneGCS evaluation and dimensions](../decisions/0009-planegcs-parametric-dimensions.md),
plus [multiple regions and bodies](../decisions/0010-multiple-regions-and-bodies.md).
The owner's detailed milestone attachment is the authority for this implementation.

TheCAD remains an independent C++/Qt/OCCT application. Only a standalone PlaneGCS
component solves supported planar constraints. No solver-specific types appear in
the public sketch model or versioned .thepart data. 3D sketching is future scope.

In an edited sketch, select a line/circle then Smart Dimension, or start Smart
Dimension then select geometry. For horizontal/vertical distance choose that type
and click two endpoints, centers or the origin. Click empty viewport space to
place the annotation, enter millimeters and Confirm dimension. Choose Reference
for a measurement that does not drive geometry. Double-click an annotation (or
its list entry) to edit its value; Close/Cancel or Escape discards a pending edit.
The panel also adds supported geometric relations and lists/deletes constraints.
Fixed point uses the selected point's current exact coordinates. H/V relations
apply to one line; H/V dimensions can reference distinct entities' points.

Solving and feature regeneration happen on candidate copies. Invalid values,
conflicts, redundant independent dimensions, missing references and failed
regeneration preserve prior geometry and values, with clear diagnostic text and
problem IDs. Do not silently delete or disable constraints. Stable IDs, dimension
kind/references/value/driving status, side, annotation position and planar space
metadata survive save/reopen; old supported .thepart/.tcpart inputs remain readable.

Dragging dimensioned lines and circles respects constraints and remaining freedom;
fixed points do not move. The previous valid geometry is the next initial guess.
Legacy exact equality propagation is retained for relation-only Coincident/H/V
sketches. Drag previews are transient; release accepts one candidate. There is no
undo/redo framework yet. Numeric property edits are exact requests and reject
incompatible driving dimensions rather than changing their targets silently.

Blue means under-defined; black requires a successful whole-sketch DOF=0 result.
This conservative prototype does not yet classify individual entities in a mixed
sketch. Relation indicators and selected entity constraint IDs aid inspection.
Red diagnostic text and involved saved constraint labels indicate rejected edits;
the geometry shown remains the last valid geometry. It is not a complete
SolidWorks diagnostic visualization or minimal-conflict explainer.

Tests must cover owner cases A–K before completion: anchored rectangle width edit,
diagonal length and drag, radius/diameter, contradiction/redundancy, persistence,
dependent extrusion, all origin planes/face frames, numeric inputs, tolerances
and nonconvergence. The owner performs most manual GUI testing; distinguish Qt
event-driven tests from human visual review.
