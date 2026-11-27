# Definition colors and planar face selection

Current correction: `codex/sketch-definition-camera-repair`, isolated `build/relations-worktree`.
Package: `build/package/TheCAD-Sketch-Definition-Camera/TheCAD.exe`. Save and close older
running copies, then reopen the desktop TheCAD shortcut to use this build.

## Automated checks

The original release passed 25 CTest cases. Current correction results are in
[sketch definition/camera verification](sketch-definition-camera.md).
`solver.entity-definition` checks complete curve and independent point
mobility, propagated constraints, reference dimensions, unchanged DOF counts and
continued dragging. Sketch interaction tests inspect black/blue batches, marker
sprite colors, live endpoint dragging and relation removal. Graphical-plane tests
check shaded light-blue face styles, persistent repeat selection, empty-click
clearing, no document mutation and entering an attached sketch.

## Owner GUI checks

1. Draw a horizontal line. Its stroke should be blue; its free endpoints should
   be blue. Drag either endpoint to resize/move it while preserving Horizontal.
2. Anchor one endpoint: the undimensioned line should remain blue. Dimension
   length: the line and both endpoints should turn black.
   Add unrelated free geometry: the defined line and points should remain black.
   Undo/redo or remove a relation and check colors update immediately.
3. Draw a circle, dimension its radius/diameter, then drag the blue center. Its
   outline should remain blue. Fix the center and check both turn black.
4. Select a planar face of an extrusion: the whole face should fill light blue,
   stay selected after moving the cursor away and remain selected on another click.
   Select another face and check only that face stays selected. Click blank space
   or press Escape and check it clears. Repeat after orbiting and at your usual scaling.
5. Select a face and click Sketch. Check the old fill clears, the sketch attaches
   to that face, and leaving the sketch and saving/reopening still works.

Hover and active selections intentionally override black/blue base colors.
Definition is local solver mobility; nonlinear singular cases can inherit the
solver's local DOF limitations. Face support still uses existing conservative
planar-face references, without adding support for curved sketch planes.
