# 0015 — Contextual sketch relations and graphical callouts

Status: owner approved scope, 2026-11-04, through the SolidWorks-Style Sketch
Relations System request and supplied toolbar screenshots. Implementation details
below use the existing planar sketch, document transactions and PlaneGCS adapter.
No backend, dependency or license changes are introduced.

## Selection and contextual commands

Shift-click toggles stable entity IDs, suspending any drawing or dimension command
into Select mode. It does not start drawing, dragging or navigation. Releasing
Shift after selection opens a compact, nonactivating toolbar beside the cursor.
Existing Ctrl selection remains available. Plain empty-space selection clears it.

Availability is a modeling policy independent of widgets. Horizontal/Vertical
apply to individual lines in a batch, or align standalone points. Parallel,
Collinear and Equal use a star of pairwise constraints for multiple lines;
Perpendicular and Tangent require exactly two entities. Circles support Concentric,
Equal radius and Equal Curve Length. Point/line and point/circle selections expose
the existing on-geometry relations. Every batch solves once and records one edit.

For Construction toggles eligible selected lines, circles and legacy rectangles.
All construction becomes normal; a mixed selection becomes construction. IDs,
dimensions and relations remain intact. Centerline remains on the ribbon; the
construction conversion action has no primary ribbon button.

The toolbar uses original QPainter artwork and logical-pixel widget sizes. Its
position is clamped inside the viewport. A 75-pixel corridor around the toolbar
and invocation position permits moving into it. Leaving that area, clearing the
selection, choosing a command, Esc or deactivating the app dismisses it. There is
no timeout. Replacing/destroying the toolbar releases its event-filter reference
before widget destruction.

## Persistent mathematics

Collinear means the same infinite straight line, with no endpoint coincidence
requirement. Concentric joins centers while leaving radii independent. Equal
means line length or circle radius. Equal Curve Length compares full geometric
length; for a line and circle this is L = 2*pi*r, not L = r. Full circles with
equal circumference necessarily also have equal radii, but keep distinct relation
types and labels.

Fix freezes both line endpoints, circle center/radius, or point position. The
snapshot survives save/reopen and does not disappear if another anchor is removed.
Coordinates already implied by origin/coincident/axis anchors are not duplicated
by the adapter. A circle's existing driving radius dimension supplies its radius
equation, with the snapshot still validated. Other redundant/conflicting constraint
combinations are reported transactionally; existing owner constraints are retained.
Legacy Fix Point retains its single-point semantics.

Parallel/perpendicular equations use PlaneGCS L2LAngle with the nearest orientation
branch, avoiding dot/cross equations satisfying a new relation by collapsing an
initially parallel/perpendicular line. The mixed Equal Curve Length adapter uses
an auxiliary circumference parameter, a linear parameter-space point-on-line
equation and a length equation. This avoids PlaneGCS proportional-equality
reduction aliasing two unknowns without the ratio. Upstream PlaneGCS is unchanged.

## Symbols, inspection and persistence

Actual persisted relations produce original green 18-logical-pixel square sprites
in the native OCCT viewer. Placement follows current geometry on drag, dimension
edit, pan, zoom and resize. Greedy layout avoids geometry, dimensions and prior
symbols where practical. Dense layouts may require Hide/Show or the inspector.
Symbols have priority over geometry for picking; hover names and highlights them.
Click selects the relation and affected entities. Delete removes only the relation.

Display/Delete Relations filters by selected geometry even with symbols hidden.
Change selected relation replaces a compatible mathematical kind, preserving its
ID and solving transactionally; Fix is released by deletion. Add/delete/change
each use the existing ten-action undo/redo history. Failed edits add no history.

Schema 9 adds relation kinds and Fix snapshots; readers retain versions 1–8.
Older builds cannot read newly saved schema 9 files. No solver objects or graphics
are serialized. The future spatial-sketch seam remains unchanged.

Arcs and splines are absent from the current geometry model. Coradial and curved
relations requiring them are deferred and never offered as enabled commands.
Existing circle-circle Tangent remains external tangency; internal tangency is
not added in this milestone. Symmetry and other unrelated relation kinds are deferred.

## Reference and validation

Semantics were checked against official SolidWorks documentation, including
[relation descriptions](https://help.solidworks.com/2026/english/solidworks/sldworks/c_Description_of_Sketch_Relations.htm),
[Equal Curve Length](https://help.solidworks.com/2022/english/SolidWorks/sldworks/c_equal_curve_length_parametric_relations.htm)
and the supplied relation-callout and persistent-shortcut-menu references. The
owner's Shift behavior overrides the reference Ctrl-selection convention. No local
SolidWorks interaction was inspected, and no proprietary graphics were extracted.

Builds followed selection, toolbar, solver and graphical-callout stages. Focused
solver tests cover persistent edits, batches, exact lengths, canonical centers,
Fix and schema validation. Qt workflow tests cover actual Shift clicks, toolbars,
construction, relation picking/deletion/replacement, transactional rejection and
undo/redo at standard and high DPI. See the manual verification checklist.
