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
4. Choose Polygon. No Sides control or Edit Polygon command should appear. Click
   a center, move around to preview radius and orientation, then commit a regular
   hexagon. Esc must discard only the unfinished polygon.
5. Select a polygon edge or center, use ordinary dimensions or vertex dragging,
   then undo/redo. Check that all six edges stay regular and connected. Draw
   non-hexagonal outlines with Line. Existing saved polygons retain their shape.
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
  selection at the origin, dragging, centerline creation, hexagon preview and
  creation, vertex dragging and closed-boundary recognition.
- `ui.modeling-workflow` and `.hidpi`: working toolbar commands and active checks,
  construction conversion/undo, Point Smart Dimensions, fixed hexagon creation,
  history, extrusion and save/reopen. The second run uses 150% scaling.
- All existing solver, document, modeling and UI tests remain regression checks.

Validation on 2026-11-04: the Release application and all test targets compiled;
all 21 available CTest cases passed in the final full run, including both display
scales and malformed polygon metadata/structural-connection rejection.

## Limits

Polygons use a circumradius and counterclockwise indexed vertices; inscribed-size
mode is not added. The UI creates six sides only. The general modeling API and
loader still support older saved regular polygons; their side-count/parameter
regression tests verify compatibility, not an exposed UI command. Deleting a member
dissolves regularity. Symmetry operations remain future scope. New files use schema
8 and require a compatible build; no new dependencies are introduced.
