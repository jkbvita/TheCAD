# 0014 — Point, Centerline and regular Polygon

Status: owner approved, 2026-11-04, through the Basic Sketch Geometry Tools request.
This extends the existing C++ sketch data and PlaneGCS adapter; it introduces no
new backend, dependency, license choice or unrelated modeling command.

## Point and construction geometry

A Point entity has one stable `First` point reference and two solver coordinates.
The Point toolbar command places one point per click. Origin/endpoints/centers
create Coincident relations; placement near a line or circle body projects to it
and stores PointOnLine/PointOnCircle. These use the corresponding PlaneGCS
equations and survive save/reopen. Points support selection, dragging, Fixed and
Coincident relations, two-point Smart Dimensions, property editing and deletion.
Explicit points coincident with the origin remain selectable independently of the
fixed origin marker. Point markers are always visible during sketch editing.

Centerline creates ordinary stable-ID Line entities with `construction=true`,
using the existing continuous-line, snapping and H/V inference interaction.
Construction lines use a dot-dash presentation; equations and dimensions remain
the same as normal lines. Select a line and choose For Construction to toggle its
flag without changing references. This is an edit recorded by undo/redo.

Contour recognition excludes points and construction entities before building
the planar boundary graph, including crossing/intersection checks. A centerline
inside a closed profile therefore neither splits it nor prevents extrusion. It
can remain a stable reference for dimensions, relations and future symmetry tools.
No symmetry or other modeling command is added in this task.

## Regular polygon

Polygon defaults to six sides. The toolbar Sides control supports 3–999, subject
to the existing 1000-entity and geometry-tolerance limits. Click the center, move
to set circumradius/orientation, then click to commit. Preview is transient and
updates when the pointer or side count changes. One polygon creation is one undo
action. Esc cancels the pending preview.

The stored polygon is a group of ordinary Line entities with stable endpoint
references, an explicit center Point and persistent boundary connections. Its
size and orientation are derived from center and the first vertex. The solver
adds equal-side and equal-center-distance equations using existing PlaneGCS
EqualLength constraints. A free polygon has four degrees of freedom: center U/V,
circumradius and orientation. Edge or center-to-vertex driving dimensions preserve
regularity; solved winding and closure are validated before accepting changes.

Select any polygon member and choose Edit Polygon to edit side count, center,
circumradius and orientation numerically. Same-count edits retain all IDs. Side
count edits retain the center and surviving indexed edge IDs. Edge dimensions or
external edge relations must be removed before changing side count. Downstream
features whose saved boundary no longer matches reject regeneration transactionally.
Conflicting numeric requests preserve the prior geometry and history.

Boundary connections are structural while a regularity group exists. Deleting
one member dissolves that group and leaves the remaining ordinary geometry and
unrelated constraints intact. Undo restores the group. Independent deletion of a
structural join is rejected with a diagnostic rather than producing a broken
regular polygon. Polygon parameter edits do not silently discard dimensions.

## Persistence and integration

Schema 8 adds Point entities, explicit construction booleans, on-geometry relation
types and polygon groups (`id`, `center`, ordered `edges`). Versions 1–7 remain
readable with ordinary geometry defaulting to non-construction. Structural IDs,
types, side counts, connectivity and regular shape are validated. New state is
included in session history; generated topology and solver types are not stored.
Older builds cannot open schema 8. The existing kernel, document transactions,
annotation placement, selection policy and ten-action history remain in use.
