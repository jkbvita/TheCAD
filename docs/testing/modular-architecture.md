# Modular architecture verification — 2026-11-06

Baseline: `0a0e027`, feature branch `codex/modular-architecture`.
Owner-approved behavior-preserving extraction; no solver/geometry algorithm,
SDK, saved schema, runtime dependency or interaction redesign.

## Automated results

- Pre-refactor baseline: 36/36 CTest targets passed.
- Final Windows desktop Release build: succeeded; 39/39 CTest targets passed.
- Clean GUI-free configuration with `THECAD_BUILD_GUI=OFF`: succeeded.
- Final GUI-free Release build: succeeded; 22/22 CTest targets passed.
- Qt Gui/Widgets packages were absent from the headless CMake configuration.
  The application-test executable imports Qt6Core, with no Qt6Gui, Qt6Widgets,
  TKV3d or TKOpenGl import.
- New tests cover session history limits/branching, sketch-change detection,
  transactional previews, saved-file round trips, tool activation, independent
  screen-space picking across zoom/DPI, dimension text and camera gesture state.
- Existing GUI checks retain their behavioral assertions. They inspect the new
  resource owners where they previously inspected private facade fields. This
  includes rendered selection colors, camera invariance, annotation reuse,
  high-DPI navigation, sketch constraints, reference geometry and file launching.
- Configure-time dependency checks and `architecture.boundaries` passed.
- Standalone packaged saved-file launch passed with a system-only PATH and bundled
  runtime, including relative/absolute Unicode filenames with spaces.
- Packaged runtime dependency scan passed (50 DLLs with existing license notices).
- Built/package executable SHA-256 match:
  `9DA970B6E59F68310BC9432D161A03FAD7446924F5B0CC17CDA843BE24F78DCD`.

The desktop shortcut targets this worktree's
`build/package/TheCAD-Modular-Architecture/TheCAD.exe`. Its previous shortcut and
package remain available. Existing owner windows were not closed. Original
checkout and untracked `untitled.tcpart` remain untouched.

## Manual checks

Restart TheCAD from the desktop shortcut before checking the new package.

1. Open an existing `.thepart`, also by dropping it onto the shortcut. Check the
   feature tree, shared sketch occurrences and expansion/selection behavior.
2. Draw/edit a sketch with snapping, constraints and Smart Dimensions. Drag points,
   circle outlines and dimensions; try Delete, Undo and Redo.
3. Extrude/cut selected regions, type depth immediately, reverse direction and
   cancel/confirm previews. Edit the feature again.
4. Create/edit reference geometry, change a source dimension, and check regeneration.
   Try dependency-aware deletion with cancel, then Undo/Redo.
5. Check hover/selection colors, orbit/pan/zoom, Ctrl+8 and drawing after sketch
   orbit. Selection must not move the camera. Repeat at 150% scaling if available.
6. Switch Dark/Light, save/reopen and verify geometry, dimensions and references.

Some tool-specific event wiring/PropertyManager composition remains in the UI
adapters. The current Windows platform and sequential regeneration design remain.
The new component APIs and enforced layers provide boundaries for further work.
There are no outstanding owner decisions for this approved refactor.
