# Sketcher interaction and feature workflow

Owner-approved milestone, 2026-11-03. Extend the existing C++ / Qt / OCCT code.

## Sketch presentation and interaction

- Keep precise lines/circles separate from display tessellation. Improve line
  antialiasing and curve presentation, including zoom and high-DPI use, and
  retain before/after rendering evidence.
- Show ordinary line endpoints throughout sketch editing as small blue circles,
  with distinct hover and selected appearances and pixel-sized picking/markers.
- In Select mode, drag an endpoint without orbiting or drawing. Preview connected
  geometry interactively; release commits one validated update. Esc or losing
  the mouse interaction cancels the transaction.
- Dragging to an endpoint or the fixed origin must use exact target coordinates
  and create a stable Coincident relation. Later edits preserve the connection.
  Origin movement, contradictory constraints and invalid geometry must reject
  without leaving a partly modified sketch.
- Offer exact local Horizontal/Vertical drawing inference in all four axial
  directions, with temporary dashed guides and relation indicators. Keep free
  angles usable, prioritize explicit point snaps and avoid competing-target
  jitter. Show temporary alignments to other points without creating construction
  geometry or introducing new constraint types.

## Sketch-to-feature workflow

Switching Sketch/Features tabs keeps the current sketch edit active. Extrude or
Extruded Cut uses the active sketch, retaining completed geometry and canceling
only an unfinished gesture. An invalid/open profile explains the problem while
leaving sketch editing available. Contours always require an explicit choice.

The feature draft must remain separate from the committed document. Cancel
returns to the suspended sketch edit without altering its geometry, document
history or prior dirty state. Confirm commits the supported sketch changes and
feature together. Completed sketches and existing feature editing remain usable.

## PropertyManager

Extrude/Cut uses a nonmodal page in the left dock normally occupied by the
FeatureManager. Include title, Confirm/Cancel, Sketch Plane starting reference,
Direction 1, Blind/depth, reverse direction, explicit Selected Contours and
preview/error status. Cut retains Through All. Depth/direction changes preview
live; invalid input disables confirmation. Keep orbit/pan/zoom available and
restore the FeatureManager when the command ends. Do not use a floating ordinary
feature dialog or an Update Preview button.

## Boundaries and delivery

Retain the approved native format and architecture. No Smart Dimension, advanced
constraint solver, assemblies, drawings or unrelated features. Extend geometry,
GUI and state-transition tests, perform native checks where possible, and
separate actual manual evidence from automated checks. Build/test by stage,
commit and push a feature branch, and do not merge main.
