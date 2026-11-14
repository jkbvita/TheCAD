# Planar Spline verification

2026-11-14, Windows x64 Release.

## Delivered

The Sketch toolbar's Spline action creates native interpolating B-spline curves.
Click fit points, use the live preview, then double-click or Enter to finish.
Click the first fit point to close a loop. Esc cancels an unfinished stroke and
exits the tool. Endpoints and interior fit points can be selected and dragged;
Edit Geometry exposes their coordinates in a scrollable table with validation.
Use existing point dimensions/relations on fit points. Construction splines
are dashed references and do not form profiles. Closed spline and mixed
line/spline profiles can drive Extrude/Cut.

Geometry and fit-point references persist with schema 27, and retain their IDs
through dragging, editing, undo/redo, save/reopen and feature regeneration.
Existing part formats and solver equations retain their behavior. The new
curve uses the current OCCT and PlaneGCS dependencies; no dependency was added.

## Automated checks

- The complete Release build succeeded.
- All 32 non-GUI CTest suites passed, covering documents, solver, history,
  modeling, export and architecture boundaries, including the new
  `modeling.sketch-spline` suite.
- Six GUI suites passed: `ui.selection-construction`, `ui.viewport`,
  `ui.solid-features`, `ui.documents`, `ui.viewport.hidpi`, and
  `ui.modeling-workflow`. Two pre-existing optional saved-model fillet fixture
  tests were unconfigured/skipped; there were no test failures.
- Focused Spline GUI workflows passed at normal and 150% scaling. They cover
  drawing/preview, double-click and Enter completion, closure, cancellation,
  fit-point dragging, undo/redo, curve picking, numeric editing and rejection
  of duplicate consecutive points, deletion/undo, save/reopen, Extrude and Cut.
- Geometry checks cover all three base planes, interpolation through each fit
  point, native edge validity, interior-point dimensions and constrained
  dragging, fully defined classification, periodic closed profiles, mixed
  line/spline profiles, construction exclusion and regeneration after editing
  an existing extruded spline profile.

The two visualization fixes in the preceding commits remain included: material
previews share the body's depth, and Fillet/Chamfer tree selection highlights
only surviving treatment faces. The viewport depth regression also passes at
150% scaling in this build.

## Manual checklist

1. Create a sketch, activate Spline, click several fit points and double-click
   to finish. Repeat using Enter; use Esc to discard an incomplete spline.
2. Drag interior points and endpoints, then undo/redo. Select the curve and
   use Edit Geometry to enter coordinates. Try point dimensions and snaps to
   the origin, line endpoints and midpoints.
3. Close a spline by clicking its first point. Extrude it, create an inner
   spline for Cut, save/reopen and edit the source points. Check regeneration.
4. Toggle a spline to construction and confirm it stays selectable and no
   longer bounds a profile. Check Dark/Light and your display scaling.
5. Orbit a cut preview to check body occlusion. Select a Fillet/Chamfer from
   the tree and verify only treatment faces highlight; blank space clears it.

## Initial limits

This is regular planar fit-point Spline, with 2 to 128 fit points including the
closing point. Style Spline, tangent/curvature handles, inserting/removing
individual fit points and driving curve-length dimensions are not implemented.
Spline trim/extend/offset, sketch replication and spline sweep paths retain
their existing unsupported-type behavior. Complex spline intersections may
require simplifying the sketch if the current contour naming is ambiguous.
There is no pending owner decision for this implementation.
