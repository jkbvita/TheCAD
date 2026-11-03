# Reference geometry, face sketching and contour verification

Date: 2026-11-03
Branch: codex/reference-faces-and-contours
Baseline: 054711c (latest local implementation inspected before editing)

## Automated verification

Ran scripts/build.ps1 -GuiTests incrementally after reference visualization,
face frames/selection and contour graph changes. Final Release build: 14/14
CTest checks passed. The existing geometry, document, viewport, navigation,
plane-selection, sketch-interaction and modeling workflows remain covered,
including Qt scale factor 1.5 variants.

New modeling.reference-faces-contours checks cover:

- A: closed rectangle.
- B/C: rectangle with one or several attached dangling lines, including a chain.
- D: closed triangle with an attached open line.
- E: independent cycles, including two cycles connected by an open bridge.
- F: only open geometry produces no region.
- G: save/reopen retains all open geometry, selected perimeter IDs and solid.
- Actual face-interior hit tests, selected-profile extrusion and cuts with branches.
- Legacy rectangle/corner branch support; explicit crossing/circle-intersection rejection.
- Top and side face attachments on an extruded rectangular solid.
- Local/global transforms, translated and rotated planar faces, extrusion and Through All cuts.
- Face attachment save/reopen, dependency regeneration, changed upstream geometry
  and tampered frame rejection without changing the prior valid document.
- Curved-face rejection.

Extended ui.graphical-planes and its 150% scale variant exercise actual OCCT face
picking for face-first and Sketch-first startup. The tests create a block, select
top/side faces, check attached frames and normal camera orientation, draw local
geometry and confirm an extrusion. Sketch-first picking occurs with origin-plane
graphics displayed, checking that real faces retain selection priority.
Existing hover/plane selection assertions were updated for intentional new tints.
The prior unsupported-version test now uses version 99 because version 4 is supported.

These are automated GUI/geometry tests. No claim is made that native manual
visual interaction testing was performed for this milestone. Primary visual
acceptance is left to the owner as requested.

## Build and deployment

The independent package was generated successfully with
scripts/package-windows.ps1 -OutputDirectory build/package/TheCAD-reference-faces.
It contains 49 runtime DLLs, Qt plugins, OCCT resources and dependency notices.
No development dependency was added or installed. The packaged executable matched
the tested build and was launched successfully with a system-only PATH and no
development Qt/OCCT environment overrides. This startup check is not manual
visual acceptance.

Launch the new build at:
C:/Users/jakub/projects/TheCAD/build/package/TheCAD-reference-faces/TheCAD.exe

The older application instance had an unsaved Untitled document and was left
untouched. The default package and root launcher still refer to that earlier
package; use the path above for this milestone, or scripts/run.ps1 for the new
build. This avoids replacing files used by the running application.
The existing untracked root untitled.tcpart was excluded from this change.

## Owner manual acceptance checklist

1. Launch this milestone's executable. Inspect all three translucent planes,
   boundaries, labels and overlapping regions while orbiting. Hover/select each.
   Pan/zoom and confirm the colored +X/+Y/+Z axes stay at the true global origin;
   the corner orientation indicator should remain separate.
2. Extrude a rectangular sketch. Select its top face then click Sketch. Draw a
   profile, extrude it, save and reopen. Repeat on a side face using Sketch first
   then clicking the face. Check normal view, local axes, snapping and Cut in the
   intended direction. Try a curved face; it should report unsupported support.
3. Draw a closed rectangle or triangle with one/multiple dangling lines. Select
   only the desired enclosed region, extrude/cut and save/reopen. All dangling
   lines should remain editable. Try only open lines, a crossing and nested loops;
   unavailable or unsupported profiles must report clearly.
4. Try changing a feature upstream of an attached sketch. The provisional
   attachment intentionally rejects changed support instead of guessing a new face.

## Limits and decisions

Face attachments use a source-feature ID, strict whole-body untriangulated BRep
fingerprint, face index and validated local frame. Even harmless support changes
may reject. No automatic reattachment or existing-sketch support replacement UI
is provided. This is not permanent topology naming. See
[the recorded strategy](../decisions/0008-reference-face-attachments.md).

Simple independent cycles are supported after removing bridges from analysis.
Nested regions, touching/shared cycle boundaries, unsplit intersections and
ambiguous overlapping geometry remain explicitly unsupported. No geometry is
deleted to obtain a contour. Plane transparency remains graphics-driver dependent;
automated success does not replace visual review on this computer.

Face-supported parts write version 4; origin-only parts remain version 3. Older
files remain readable. An older TheCAD binary cannot open version 4 parts.

No dimension behavior or solver was implemented. Approve a solver evaluation,
license/dependency scope and dimension/anchoring semantics before Smart Dimension.
See [the sourced proposal](../requirements/smart-dimension-proposal.md).
