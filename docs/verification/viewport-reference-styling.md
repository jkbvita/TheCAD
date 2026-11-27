# Viewport reference styling

Date: 2026-11-04
Branch: codex/viewport-reference-styling
Baseline: a5b1f04

## Changes

The owner requested styling based on the supplied SolidWorks references:

- Line endpoints are flat filled blue circles with a fine dark rim. Hover turns
  them orange; selected endpoints retain blue fill and a larger marker size.
  Textures are cached by color/size and use physical-pixel dimensions for DPI.
- Global XYZ and local sketch axes have solid arrowheads and a filled origin
  point. Local sketch arrows are red. The former O text label is removed.
  Graphics remain anchored to their actual model-space origins.
  Fit All frames displayed modeling geometry/planes without including datum arrows.
- Origin planes have a subtle translucent blue fill, thin blue outlines and an
  orange boundary for hover/selection. Dashed centerlines cross the origin.
  Italic plane labels follow each plane's orientation. Plane and solid-face
  hit-testing remain the existing geometry-based paths.

No model parameters, file schema, dependency, solver or license changed.

## Verification

Ran scripts/build.ps1 -GuiTests: Release build and all 14 CTest checks passed,
including 150% scale coverage. The existing plane interaction test's transparency
expectations were updated for the requested styling; no redundant new style
unit tests were added. Dragging, joining, sketch creation, face selection,
Extrude/Cut, document round trips and navigation retain regression coverage.

Inspected rendered test artifacts for plane presentation and the sketch's
filled endpoints, origin point and arrowheads. These are renderer snapshots,
not a claim of native manual interaction testing. The captures are local ignored
build outputs under build/windows/test-artifacts/reference-planes.png and
build/windows/test-artifacts/rendering/sketch-1-after.png, with 150% scale
variants under test-artifacts/hidpi.

The independent Windows package is refreshed at build/package/TheCAD, matching
the root Launch TheCAD.cmd. Inspect the style at your normal scale and hover the
endpoints and planes during owner acceptance. Antialiasing/font rasterization
can differ across graphics drivers and display scales.

The existing untracked root untitled.tcpart is preserved and excluded from the
commit. No changes are merged into main. No owner architecture decision is
needed for this visual change.
