# Foundational sketch interaction and relations

## Approved scope

Preserve TheCAD's existing C++/Qt/Open CASCADE implementation, graphical origin
plane selection, feature history, and explicit-contour extrusion/cut workflows.
Improve sketch interaction before adding more modeling tools.

- Every sketch has a selectable exact local origin at (0,0).
- Origin and endpoint snapping uses a screen-pixel tolerance but returns exact
  model coordinates and stable point references, independent of zoom and DPI.
- Line endpoints are separately identifiable from line bodies, highlight on
  approach, and show markers when selected.
- Line drawing continues from each completed endpoint. Closing a contour by
  snapping creates a persistent connection. Esc cancels the current operation
  while retaining completed segments. Zero-length/duplicate segments are avoided.
- Rectangles create four connected line edges with horizontal/vertical relations.
  Their corners and circle centers may snap to existing points or the origin.
- Coincident, horizontal, and vertical relations persist in the sketch model
  and in .thepart. Editing coordinates preserves their equalities or rejects
  an incompatible edit with a clear error. The origin remains fixed.
- Saving/reopening retains geometry and supported relations. Existing exact
  contour selections and downstream features continue to recompute.

## Boundaries

No complete constraint solver, dimension system, advanced sketch operation,
face attachment, assembly, drawing, or permanent topology-reference strategy is
introduced. Older documents remain readable without automatically inferring
relations from coincident-looking coordinates. The internal part schema remains
provisional; the permanent filename extension is unchanged.

## Acceptance checks

On Front Plane, start at the origin, draw a connected loop, snap closed, and
inspect its endpoints/relations. Test origin-anchored rectangle and circle tools,
then pan/zoom and repeat snapping. Save/reopen and verify exact equality plus a
working extrusion. Automated geometry/document tests must reject dangling or
contradictory references and preserve the previous model on failure.
