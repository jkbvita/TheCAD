# Intersecting sketch extrusion and cutting

Save and close any older running TheCAD window, then use the desktop shortcut.
The current branch is codex/intersecting-sketch-regions, based on 6b33e71.
The separate executable is build/package/TheCAD-Intersection-Regions/TheCAD.exe
inside the isolated relations worktree.

1. Reproduce the screenshot: draw a circle centered at one rectangle corner, with
   its circumference crossing two rectangle edges. Add the dashed construction
   midlines if desired. Activate Extrude: three bounded regions should be offered;
   construction lines must not introduce more regions.
2. Click only the overlap. The preview should extrude only that area. Toggle the
   circle-only area too: together they should produce the complete circle.
   Instead combine overlap and rectangle-only to produce the rectangle. Selecting
   all three should produce their union, without internal walls.
3. Confirm, undo and redo the feature. Change depth and direction. Cancel a
   preview and verify the original sketch and constraints are retained.
4. On an existing solid, repeat with Extruded Cut. Test the overlap alone, several
   cells, blind depth, Through All and reverse direction. Only material inside
   selected cells should be removed. Repeat on a planar face/other sketch plane.
5. Try two overlapping circles: each crescent and their lens should be independently
   selectable. Try a circle crossed by a diameter, a rectangle crossed by two
   normal lines, and a divider ending at the middle of a boundary (T-junction).
6. Include nested loops/holes and dangling open lines. Holes should stay empty
   unless their inner cells are also selected; open tails should not extrude.
7. Save/reopen and change a driving dimension while intersections remain. The
   selected region should regenerate. An edit that removes its intersections
   should report the unavailable region and preserve the previous valid result.
8. Click interiors rather than intersection points/shared boundaries. Boundary
   clicks can be ambiguous; choose the intended region in the list. Duplicate or
   coincident overlapping sketch edges require cleanup. Invalid zero-thickness
   profiles remain unsupported.

Release build and 24/24 CTest cases pass, including normal/high-DPI UI selection,
preview, Extrude/Cut, undo and reopening. Focused model tests verify exact areas
and volumes, all planes, inclined frames, holes, construction exclusion and stable
references. New saves use schema 12; supported older documents remain readable.
