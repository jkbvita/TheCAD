# Sketch relation checks

The current implementation is isolated on `codex/midpoint-snaps-rectangle-drag` and
combines accepted relation commit `db4f515` with feature-tree/hexagon commit
`bd71e05`. The original checkout/branch and `untitled.tcpart` are preserved.
Use the repaired desktop TheCAD shortcut, or the isolated worktree's executable
`build/package/TheCAD-Midpoint-Rectangle/TheCAD.exe`. The old package is retained
because it may still be running with an unsaved sketch. Save and close that window
before opening the repaired desktop shortcut.

## Supported commands

| Selection | Relations |
| --- | --- |
| One or more lines | Horizontal, Vertical, Fix |
| Two or more lines | Collinear, Parallel, Equal length |
| Exactly two lines | Perpendicular (in addition to the above) |
| Two or more circles | Concentric, Equal radius, Equal Curve Length |
| Two circles | External Tangent (in addition to the above) |
| Line and circle | Tangent, Equal Curve Length |
| Two or more standalone points | Coincident, Horizontal, Vertical |
| Point and line | Point on Line, Midpoint |
| Point and circle | Concentric, Point on Circle |
| Any supported lines/circles/points | Fix |

Eligible lines, circles and legacy rectangles also offer construction conversion.
Legacy endpoint Coincident and Fix Point remain supported through snapping and
the relation manager. Coradial requires arcs; arcs/splines, internal circle
tangency, symmetry and other unsupported commands are not offered.

## Manual GUI checks

1. Draw two oblique lines. Hold Shift and click their bodies. Both should remain
   light blue. Toggle one off/on; release Shift. No line, drag or orbit should
   start. Repeat from an active Line or Smart Dimension command; Shift moves to
   Select. Plain empty-space click clears the selection.
2. Hover toolbar icons for names. Move into the toolbar and pause: it should stay.
   Move far away: it should hide. Reopen and press Esc. Repeat near each viewport
   boundary and at 100%, 150% and 200% Windows scaling; the toolbar should fit.
3. Apply Parallel, Perpendicular, Equal or Collinear separately. Drag endpoints,
   edit a driving length and rotate an unconstrained line. Relations should remain
   satisfied. Collinear lines need not touch. Test three selected lines with Equal
   and Parallel. An incompatible constraint should explain rejection and retain
   all previous geometry, dimensions, relations and undo history.
4. Select circles: Concentric preserves independent radii; Equal changes both
   radii together. For a line/circle Equal Curve Length, dimension the radius and
   verify line length is circumference. Try line/circle and external circle/circle
   tangency. Fix a point, line and circle; dragging/resizing should retain the fixed
   state. Delete Fix to release it. Redundant combinations may be rejected.
5. Select constrained geometry and choose For Construction. Check dashed styling,
   retained dimensions/relations and exclusion from extrusion boundaries. Repeat
   for several entities, undo/redo, then convert back using Normal Geometry.
   Centerline remains available in the ribbon; conversion is contextual.
6. Hover green icons: tooltip and highlight should identify the relation. Click
   one: affected geometry highlights. Delete: only the relation disappears.
   Ctrl+Z/Ctrl+Y should restore/remove it as one action. Move/dimension geometry;
   icons should follow. Zoom/pan/resize; their screen size should remain readable.
7. Hide symbols under View > Hide/Show > Sketch Relations. Select an entity and
   open Display/Delete Relations: only associated constraints should be listed.
   Select/delete a relation there or use Change selected relation. Undo/redo each
   change once; its ID should remain stable when changing type.
8. Save a sketch containing the new relations and construction geometry. Reopen,
   inspect symbols and constraints, drag again and regenerate an extrusion.
   Repeat with an older supported document. New files use schema 11 and require
   this build; retain a copy when comparing with an older executable.

## Circle and Equal repair checks

- Create a circle away from the origin, move the pointer away and clear selection:
  the center point should remain visible. Drag either its center or its outline.
  An outline drag should move the circle smoothly from the grabbed position and
  retain its radius, including with driving dimensions and tangent relations.
  Test the live preview, repeated drags, Escape cancellation and undo/redo.
- Apply Equal to two entities: each should have one green icon. Apply Equal to
  three or more together: every member should have exactly one icon for that
  group, including the shared reference member. Click any icon to highlight all
  members; Delete releases the group while retaining geometry. Undo once restores
  the whole group. Independently created equality groups remain independent.
- Hide symbols and select a nonreference member. Display/Delete Relations should
  list the group once. Save/reopen, then repeat group selection and deletion.
- Start from the desktop shortcut and verify that both the cleaned feature tree
  and contextual relation toolbar are present in the same application.

## Automated validation

Release build with MSVC 2022, Qt 6.8.4, OCCT 7.9.3 and the existing PlaneGCS.
`ctest --test-dir build/windows -C Release --output-on-failure` runs 23 cases,
including solver.relations-system and normal/high-DPI modeling workflow tests.
Final midpoint/rectangle result: 23/23 passed, 0 failures (48.81 seconds). The standalone package was
also generated successfully with the existing runtime dependency/license scanner.
The suite also covers existing dimensions/annotations, ten-action history,
deletion, basic geometry tools, documents, feature regeneration and profiles.
Primary visual assessment remains with the owner.
