# 0004: Graphical origin-plane sketch creation

- Status: Owner-approved workflow; limited implementation details.
- Date: 2026-11-03.
- Owner approval: Revised request in this chat to reproduce fundamental
  SolidWorks workflows faithfully, focusing only on graphical sketch creation
  and origin-plane selection.
- Requirements: [Graphical plane selection](../requirements/graphical-plane-selection.md).
- Supersedes: The sketch-plane chooser portion of
  [0003](0003-parametric-sketch-features.md). Its modeling and document behavior
  remains preserved.

## Decision

Replace the plane chooser with direct OCCT viewport interaction. Sketch-first
shows three labeled, translucent reference planes; hovering highlights a plane
and clicking it starts sketch editing without another confirmation. Plane-first
allows selection of a visible plane or an Origin tree reference before clicking
Sketch. Esc cancels a pending selection with no feature or document change.

Keep the existing stored origin-plane references and right-handed frames:

| Name | Reference | U axis | V axis | Normal |
| --- | --- | --- | --- | --- |
| Front | XY | +X | +Y | +Z |
| Top | XZ | +X | +Z | -Y |
| Right | YZ | +Y | +Z | +X |

The normal view looks from the positive normal side with local V upward. Naming
the existing frames preserves orientation and feature-direction meaning in
previously created parts; it does not reinterpret their stored coordinates.

## Picking and display details

Display finite bounded faces for discoverability and hit testing, but retain the
underlying infinite mathematical planes for sketch coordinates. Derive display
extent from model bounds or a default model-space span, independent of screen
resolution. Draw visible boundaries and separate nonselectable labels. Use blue
for the normal state, amber for hover, and green for selection.

Intersect a forward 3D ray with the actual displayed faces and classify the
intersection against the planar geometric region. Choose the nearest eligible
hit. Equal-depth hits resolve in Front, Top, Right order. Labels, transparency,
and draw order do not decide which plane is hit.

Treat pointer movement beyond Qt's drag threshold as navigation rather than a
selection click. Existing orbit, pan, zoom, and Fit All remain available during
selection. Entering sketch mode hides plane-selection graphics and displays the
sketch origin and local axes.

## Scope and consequences

Origin tree entries are reference controls, not extra features in the saved
history. A created sketch retains a real XY/XZ/YZ reference in document data,
independent of camera orientation. The current `.thepart` schema and legacy
`.tcpart` support remain intact. No new dependency is introduced.

This milestone does not select a permanent face-reference or topology-naming
strategy, implement face attachment, or expand sketch/modeling features. Those
are separate decisions. Alternative workflows can be considered after the
approved SolidWorks-style baseline works reliably.

## Validation

The implementation must be built and launched. Geometry tests should verify
mapping, ray intersections, overlap order, and unbounded sketch coordinates;
document tests should preserve all three plane references. Desktop verification
must record the interactions actually performed, including hover, plane-first,
Sketch-first, navigation, cancellation, and save/reopen. This decision record
does not itself assert that those tests passed.
