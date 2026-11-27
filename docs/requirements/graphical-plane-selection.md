# Graphical origin-plane sketch creation

- Status: Owner-approved scope.
- Approval: Revised short-term goal and graphical sketch-plane request in this
  chat, 2026-11-03.
- Decision: [0004: Graphical origin planes](../decisions/0004-graphical-origin-planes.md).

Reproduce the fundamental SolidWorks-style modeling workflow before considering
alternative workflows individually. This task changes only sketch creation and
graphical plane selection in the latest working TheCAD application.

## Sketch-first workflow

1. Click **Sketch** in the SKETCH toolbar tab.
2. Display Front, Top, and Right as three intersecting translucent planes inside
   the existing OCCT viewport, with boundaries and labels.
3. Highlight the plane under the pointer.
4. Click a plane to select it, orient the view normal to it, and enter sketch
   editing immediately. Hide the temporary plane-selection graphics.

There is no plane dropdown, list, confirmation dialog, or plane-selection toolbar
button. Starting selection does not create or modify a sketch. Clicking empty
space has no modeling effect. Esc cancels selection without creating a feature.

## Plane-first workflow

Select a visible reference plane in the viewport or its Front/Top/Right entry in
the feature tree's Origin reference group. Click Sketch to enter editing on that
plane immediately. Reference entries are not sketch features or extra history
operations; sketches store the chosen origin-plane reference in their own data.

## Coordinate and visualization rules

| Display name | Stored plane | Local U | Local V | Positive normal / view side |
| --- | --- | --- | --- | --- |
| Front | XY | +X | +Y | +Z |
| Top | XZ | +X | +Z | -Y |
| Right | YZ | +Y | +Z | +X |

Keep these existing right-handed coordinate frames consistent across labels,
normal views, drawing, feature direction, save, and reopen. The camera looks
from the listed normal side toward the sketch, with local V pointing up.
Entering sketch mode shows the origin and local sketch axes. Existing Line,
Rectangle, and Circle tools use the selected plane's actual model coordinates.

Reference-plane graphics have a visible finite extent derived from the body or
an initial model-space span. The real reference planes are mathematically
infinite: their display boundaries must never clip or restrict sketch coordinates.

Use distinct default, hover, and selected styles: translucent blue, amber, and
green respectively. Labels and transparent rendering must not become hit targets.

## Picking and navigation

Use a 3D forward picking ray and the actual displayed planar BRep regions, not
hard-coded screen rectangles. Ignore parallel or behind-ray intersections. Of
eligible overlapping planes, choose the nearest forward hit; ties use Front,
then Top, then Right. Keep this rule deterministic when the view is rotated.

Distinguish a left-button click from an orbit drag using Qt's normal drag
threshold. Preserve orbit, middle/right-button pan, wheel zoom, and Fit All while
choosing a plane. Normally hide the reference planes after entering sketch mode.

## Compatibility and boundaries

Preserve working sketch/modeling behavior and versioned part data. Keep permanent
`.thepart`, `.theasmb`, and `.thedrw` names and legacy `.tcpart` reading/conversion.
No file-schema migration is needed merely to add graphical selection of the
existing stored XY/XZ/YZ references.

Do not add arbitrary-solid-face attachment, topology naming, constraints, or new
modeling features. Face attachment is a future extension of this workflow and
requires separately agreed reference semantics.

## Verification requirements

Build and run locally. Exercise Front, Top, and Right sketch creation, hover and
selection styles, plane-first creation, orbit before selection, empty clicks,
Esc cancellation, and entering/leaving sketch mode. Save/reopen and verify every
sketch's plane and orientation. Add practical tests for transforms, picking,
coordinate conversion, and persistence. Record actual manual observations
separately from automated tests; this requirement record is not a test result.
