# Basic sketch tools verification

## Manual checks

1. In an active sketch, choose Point and click free space, the origin, a line
   endpoint/midpoint, a circle center and its circumference. Confirm visible point
   markers and persistent snap relations. Select/drag free and constrained points;
   dimension two points with Smart Dimension. Save/reopen and repeat the drag.
2. Choose Centerline. Draw a chain with the same clicks, snapping and Esc behavior
   as Line. Check dot-dash styling, selection, hover, dimensions and relations.
   Select a line and toggle For Construction both ways. Verify IDs, attached
   dimensions and undo/redo are preserved.
3. Draw a closed normal rectangle with centerlines crossing through it and points
   inside/on its boundary. Only normal boundaries should form extrusion regions.
   A closed loop made entirely from centerlines must not produce a profile.
4. Choose Polygon; Sides initially reads six. Click a center, move around to preview
   radius and orientation, then commit. Try side counts 3, 5, 6 and 8, including
   changing Sides while previewing. Esc must discard only the unfinished polygon.
5. Select a polygon edge or center and choose Edit Polygon. Change center,
   circumradius and orientation, then undo/redo. Check that all edges stay regular
   and connected. Edit side count on a polygon without external edge constraints.
   Side-count changes with edge dimensions/relations should give a clear rejection.
6. Add a driving edge length or center-to-vertex distance. Change its value and
   drag a free vertex: the entire polygon should remain regular. Extrude its closed
   boundary, edit a supported dimension, and check solid regeneration. Repeat in
   Front, Top, Right and a planar-face sketch.
7. Delete an edge, undo and redo. The surviving geometry should behave as ordinary
   edges after deletion; undo must restore the regular group and dimensions. Save
   and reopen a part containing all three tools, then edit and regenerate again.

The owner performs primary visual testing. Automated GUI tests send Qt events;
they do not constitute human visual approval or a local SolidWorks inspection.

## Automated coverage

- `modeling.basic-sketch-tools`: point references/relations/dimensions and dragging;
  construction exclusion, including crossings; four polygon DOFs; numeric radius,
  orientation and side-count changes; stable IDs; edge/radius dimensions; all
  origin planes; extrusion regeneration; save/reopen and malformed metadata.
- `ui.sketch-interaction` and `.hidpi`: actual point placement and snaps, point
  selection at the origin, dragging, centerline creation, polygon preview/side-count
  changes, vertex dragging and closed-boundary recognition.
- `ui.modeling-workflow` and `.hidpi`: working toolbar commands and active checks,
  construction conversion/undo, Point Smart Dimensions, polygon numeric editor,
  history, extrusion and save/reopen. The second run uses 150% scaling.
- All existing solver, document, modeling and UI tests remain regression checks.

Validation on 2026-11-04: the Release application and all test targets compiled;
all 21 available CTest cases passed in the final full run, including both display
scales and malformed polygon metadata/structural-connection rejection.

## Limits

Polygons use a circumradius and counterclockwise indexed vertices; inscribed-size
mode is not added. Side count is bounded by existing sketch entity limits and
small-edge tolerance. Changing side count on externally constrained edges or a
saved downstream contour may require removing constraints or repairing references
first; changes are rejected rather than silently remapped. Deleting a member
dissolves regularity. Symmetry operations remain future scope. New files use schema
8 and require a compatible build; no new dependencies are introduced.
