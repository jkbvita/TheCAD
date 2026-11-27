# Sketch definition colors and camera preservation

Branch: `codex/sketch-definition-camera-repair` in `build/relations-worktree`.
Package: `build/package/TheCAD-Sketch-Definition-Camera/TheCAD.exe`.

## Automated checks

Release build and all 28 CTest cases pass (75.25 seconds). The five focused
definition/interaction/plane tests also passed separately. Focused checks cover:

- `solver.entity-definition`: Horizontal/Parallel/Perpendicular alone remain blue;
  a fixed endpoint without length remains blue; a fully defined line turns black;
  removing length or anchoring restores blue. A radius without center definition
  stays blue. Fixed ordinary/construction geometry and regular polygons stay black
  beside unrelated free points. Legacy rectangle axes alone do not define size.
  Diagnosis preserves the real solver DOF count and input geometry.
- `ui.sketch-interaction` and `.hidpi`: inspect actual line batches and point
  sprites, drag underdefined endpoints, add an anchor and driving length, and
  remove length to check black/blue transitions. Fully defined circles and
  construction geometry retain black strokes and dashed styles.
- `ui.graphical-planes` and `.hidpi`: create attached sketches on top and side
  faces of an extrusion. Zoom and pan, create a line and circle with live previews,
  drag a line endpoint and undo/redo. Assert camera scale, eye, center, up and
  direction stay unchanged. Continue with a rectangle and a dependent extrusion.

These use the real MainWindow regeneration path, with GUI suites serialized and
HiDPI runs at 150%. No repeated screenshot comparisons are used.

The standalone package's saved-file startup test also passes (absolute and
relative Unicode/space paths, system-only child PATH and no development Qt/shader
overrides). Its executable SHA256 matches the tested Release executable:
`A012BD48C3470F3D91525763E50E988A5CE5F63BE59CC60C62B7DC70B80DE2DD`.
Packaging verifies runtime dependencies stay inside the package (50 DLLs, 67 MiB).

## Owner GUI checks

1. Draw a horizontal line starting at the origin. It should remain blue and its
   free endpoint should drag. Add a driving length: it should turn black. Remove
   the dimension or undo: it should return to blue. Repeat with a vertical line
   and a centerline; construction strokes must remain dashed.
2. Dimension a freely positioned circle: it should remain blue. Define its center
   as well: it should turn black. Add unrelated free geometry: the fully defined
   circle should stay black. Hover/selection still override its base color.
3. Extrude a block, select a face and start Sketch. Zoom in and pan. Draw several
   lines and circles: the camera should stay where you placed it during previews,
   completion, dragging and Ctrl+Z/Ctrl+Y. Repeat on a side face. Explicit Fit and
   leaving/re-entering the sketch should continue to frame the view normally.

Restart from the updated desktop shortcut to use this package. Already running
older copies are left untouched. All previous packages and a shortcut backup
are retained (`build/desktop-shortcut-backup/TheCAD-before-sketch-definition-camera.lnk`).
Save files and their schema are unchanged. Definition uses local
solver mobility; singular nonlinear cases retain the solver's existing limits.
No owner decisions remain outstanding for these corrections.
