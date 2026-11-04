# Dimension graphics and selection verification

The owner performs primary visual testing. Automated GUI tests exercise Qt mouse
and keyboard events; they do not establish exact visual parity with SolidWorks.

## Manual checks

1. Create a free diagonal line. Add a reference dimension (or a driving length
   dimension with remaining rotational freedom). Exit Smart Dimension, then drag
   an endpoint. Check that text, arrows and extensions follow throughout the drag,
   including rotation and resizing. Try H/V, two-point, line-spacing and angular
   dimensions; repeat in Front, Top, Right and a face sketch.
2. Drag dimension text, a dimension line or an extension line several times.
   Check that only annotation placement changes. Move its referenced geometry
   afterward. Escape during a drag must restore the previous placement. Double
   click the relocated text and change a driving value; dependent features must
   still regenerate.
3. Dimension a circle as diameter. Check Ø, the boundary-to-boundary line and both
   arrowheads. Drag around the circle, then move its center and edit its diameter.
   Add a reference radius with R during creation; check R and its circumference
   arrow, then rotate it by dragging. Check small and large circles, with text
   both inside and outside.
4. Save as `.thepart`, reopen and resume sketch editing. Check relocated linear,
   angular, radius and diameter placement; move/resize geometry again. Open an
   older part and confirm its initial annotation positions are retained.
5. Activate Smart Dimension and create several dimensions. The button must stay
   pressed through selection, placement and Modify. Select Line/Rectangle/Circle
   or Select, then try Escape and Finish Sketch. The checks must match the mode.
6. Hover/click within about 6 pixels of lines and circle boundaries. Try short
   lines, small circles, close parallel edges and overlapping references at
   different zoom levels and Windows display scaling. Endpoints take precedence,
   then annotations, then the closest entity. Geometry should highlight orange
   before selection. Change Tools > Sketch Picking Tolerance and compare.

## Automated coverage

- `modeling.dimension-graphics`: stable IDs after reorder; translation, rotation
  and resizing; repeated placement; unchanged targets; circle boundary arrows;
  current reference-angle geometry; all origin sketch planes.
- `document.dimensions`: placement round trips and subsequent radius changes,
  schema-6 migration, malformed placement rejection, solver/regeneration rollback.
- `ui.sketch-interaction` and `.hidpi`: annotation drag/repeat/cancel, live rendered
  text during geometry drag, unchanged values/entities, circular dragging while
  Smart Dimension is active, geometric picks, hover, tolerance and zoom.
- `ui.modeling-workflow` and `.hidpi`: persistent tool checks, mutual exclusion,
  Escape/Finish Sketch, repeated dimension creation, editing relocated annotations,
  conflict handling, persistence and dependent-feature regeneration.

Build with `scripts/build.ps1 -GuiTests`. Tests live under `build/windows` and use
Release configuration; `.hidpi` runs at `QT_SCALE_FACTOR=1.5`.

Validation on 2026-11-04: the Release application and all test targets compiled.
All 19 available CTest cases passed across the full run and the focused rerun of
the corrected legacy JSON fixture. A final small-circle selection refinement was
validated with the affected sketch/modeling workflow suites at both scales.

## Limits

No automatic annotation collision avoidance or selection cycling is added.
[Session undo/redo](edit-history-sketch-deletion.md) now retains ten edits.
Exactly coincident picks use stable document order. Circle hit testing
assumes the existing normal orthographic sketch-editing view. Picking tolerance
is a session setting. Circle text spacing scales with radius. New files use schema
7 and require this or a newer compatible build.
