# Graphical origin-plane verification

Date: 2026-11-03. Branch: `codex/graphical-sketch-plane-selection`.

The latest working parametric foundation was preserved in baseline commit
`e0783e3` before creating this branch. The pre-existing `untitled.tcpart` remains
untouched and outside source-control changes.

## Build and automated checks

Built the Windows Release application with the existing MSVC / Qt 6.8.4 /
OCCT 7.9.3 / CMake setup. No dependency was added.

```powershell
.\scripts\build.ps1 -GuiTests
```

The final complete CTest run passed all 10 suites:

- Existing box geometry, sketch/extrusion/cut geometry, and versioned part documents.
- Reference-plane names, right-handed transforms, bounded BRep faces, arbitrary
  forward rays, two-sided/rotated picking, nearest depth, overlap ties, and
  parallel/behind/empty rejection. Sketch coordinates remain independent of
  display boundaries.
- Existing viewport resize/navigation and sketch mouse-coordinate checks on all
  three planes, including 150% scaling and a large valid existing part.
- Main-window lifecycle, graphical selection without mutation, cancellation,
  real tree preselection, legacy compatibility, and plane persistence.
- Existing complete sketch/extrusion/cut GUI workflow, adapted to graphical
  origin-plane selection rather than the removed plane chooser.
- Graphical plane GUI workflow at normal and 150% scaling: actual plane hits,
  hover/leave styling, click versus orbit, empty clicks, real Esc, all three
  normal/up vectors, local rectangle drawing, plane-first viewport/tree entry,
  save/reopen, and existing-sketch editing (including tree double-click).

The hover-state regression injects Qt mouse-move/leave events deterministically;
other drawing/click workflows exercise the real widgets and handlers. Native
mouse focus is exclusive while desktop suites run. Qt test reports and rendered
artifacts are generated under `build/windows/`.

## Manual desktop observations

The actual application was launched and inspected through the Windows
computer-use tools. After pausing competing local input, the following checks
were performed and visually inspected:

- Three intersecting translucent blue planes, visible boundaries, and separate
  Front/Top/Right labels appeared inside the existing CAD viewport.
- Selecting Front from the Origin tree produced a green selected plane without
  creating a feature. Clicking Sketch immediately entered Front/XY sketch mode,
  removed selection graphics, and showed the origin and horizontal/vertical axes.
- Sketch-first selection showed the planes; pointer hover over Top produced an
  amber highlight. Clicking its geometric region entered Top/XZ sketch mode.
- A left drag orbited the scene while plane choice remained pending. Right was
  then selected in the rotated view and entered Right/YZ sketch mode.
- Finish Sketch returned each sketch to normal modeling mode.
- Clicking empty viewport space kept the feature count unchanged. Esc hid the
  temporary graphics, restored the UI, and created no sketch.
- The native Save As dialog wrote
  `build/windows/test-artifacts/manual-reference-planes.thepart`.
- The application was closed and relaunched; the file was reopened through the
  native Open dialog. The tree restored all four sketches, including the
  pre-existing user-drawn sketch and its 10 entities. File contents retain YZ,
  XY, XZ, YZ references. Reopened Front, Top, and Right sketch views and their
  local axes were inspected using the sketch-edit and normal-view controls.

No pre-existing sketch geometry was discarded during these checks. The manual
verification part is retained locally in the ignored build artifacts.

## Scope and limits

- Only origin planes are selectable; arbitrary solid-face attachment is deferred.
- Display patches are finite and model-scaled (capped at the geometry helper's
  supported display extent); the mathematical sketch references are infinite.
- Nearest-forward geometric hit wins overlaps; ties resolve Front, Top, Right.
  Rotate the camera or select the tree reference for an occluded/edge-on plane.
- No schema change, topology naming strategy, constraint workflow, or new
  modeling feature was introduced in this task.
- Forced 150% scaling was automated; physical mixed-DPI monitor transitions were
  not manually tested.
