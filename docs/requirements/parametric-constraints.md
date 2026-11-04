# Parametric sketch constraints and Smart Dimension

Owner-approved scope: [PlaneGCS evaluation and dimensions](../decisions/0009-planegcs-parametric-dimensions.md),
plus [multiple regions and bodies](../decisions/0010-multiple-regions-and-bodies.md).
The owner's detailed milestone attachment is the authority for this implementation.

TheCAD remains an independent C++/Qt/OCCT application. Only a standalone PlaneGCS
component solves supported planar constraints. No solver-specific types appear in
the public sketch model or versioned .thepart data. 3D sketching is future scope.
Current workflow extension: [direct dimension and shared-region approval](../decisions/0011-shared-regions-direct-dimensions.md).
Annotation and selection extension: [geometry-attached dimensions](../decisions/0012-dimension-graphics-selection.md).

In an edited sketch, select a line/circle then Smart Dimension, or start Smart
Dimension then select geometry. Two points support horizontal/vertical/aligned
distance, two parallel lines perpendicular spacing, and nonparallel lines angle.
Move the cursor to choose orientation/sector and preview, then click empty space
to place. Enter a value in the floating Modify editor beside the annotation;
Enter confirms and Escape/Cancel rejects. The command stays active for subsequent
dimensions until Escape or another tool. No dimension form is required on the left.
A circle defaults to diameter; R selects radius and D diameter before placement.
Choose Reference in the floating editor for measurement without driving geometry.
Double-click an annotation (or relation-list dimension) to edit its existing value.
Drag annotation text/lines in Select or idle Smart Dimension to move placement
without editing its numerical target. Graphics resolve current stable references
during geometry dragging; relative placement survives later geometry edits and
save/reopen. Circular dimensions use Ø/diametric and R/radial boundary arrows.
Tool buttons remain checked while active. Hover and picking use an adjustable
8-logical-pixel default, with points, annotations and nearest entities prioritized.
The separate relation manager adds, inspects and deletes supported constraints.
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
