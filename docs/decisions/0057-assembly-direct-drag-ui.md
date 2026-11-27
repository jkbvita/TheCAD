# 0057. Direct assembly dragging and compact assembly panels

Status: requested by the owner on 2026-11-15 and implemented on
`codex/assembly-drag-ui-stability`.

The owner requested a checkbox-crash fix, direct point-based component movement,
automatic circular dragging and improved assembly UI. No modeling engine,
assembly format or persistent mate types change.

## Interaction

- Left-drag grabs the actual picked face/edge/vertex position. A transient target
  follows the cursor on the grab-depth plane, projected into the screen plane.
- Existing mates remain hard constraints. Small null-space drag steps are
  reprojected through the mate solver; unreachable cursor positions keep a valid
  constrained placement. The temporary target does not alter persistent freedom.
- Free components translate on straight strokes. Several consistently curved
  samples activate rotation about the component's geometric center and camera
  normal. The transition starts from the current pose without a placement jump.
- Right-drag provides immediate 3D rotation. Numeric position/orientation remains
  available as an expandable detail, without setting a persistent interaction mode.
- A completed drag is one history action; Esc cancels the entire preview. Fixed
  and fully constrained components retain their existing movement protection.

SolidWorks documents left-drag movement within mate freedom and right-drag
rotation. Automatic circular-stroke recognition is the owner's requested TheCAD
behavior, not a claim that SolidWorks uses this gesture classifier.
[Moving Components](https://help.solidworks.com/2020/english/SolidWorks/sldworks/t_moving_a_component_swassy.htm),
[Rotating Components](https://help.solidworks.com/2019/english/SolidWorks/sldworks/t_Rotating_a_Component_SWassy.htm).

## Stability and presentation

Checkbox handlers capture IDs/state and defer transactions until Qt finishes its
model notification. They never open a component editor or destroy the emitting
item synchronously. Visibility remains transactional and undoable for linked and
assembly-owned parts.

The tree gets most of the left dock while idle. Selection details are compact;
numeric placement expands on demand. Assembly PropertyManagers share compact
Confirm/Cancel controls, contextual titles and framed mate selection groups.
Existing Dark/Light palettes and logical-pixel sizing are retained.

## Verification and limits

Focused tests cover free translation, actual grabbed points, hinge dragging,
unreachable targets, mate limits and reversal, free circular gestures, right-drag,
Esc/history, checkbox state and linked/owned visibility history, and camera
stability. Final Release build succeeded. All 37 non-GUI CTest suites passed (15.56 s);
both assembly GUI suites passed, with 10 QtTest cases each in Dark and Light/150%.
The GUI cases include repeated linked/owned checkbox toggles, circular and right
drags, camera preservation and history. The brief computer-control layout review
was stopped by the owner before the assembly panel was inspected; no further
automated desktop input was performed. The final package matches the compiled
executable hash.

Gesture recognition requires enough curved samples to distinguish intent from
jitter; rotation begins after that threshold. A sustained straight stroke returns to
translation; constrained components continue to follow their permitted mate motion. Freehand gestures are inherently
ambiguous. Right-drag and numeric placement remain deterministic alternatives.
This does not add collision/contact dynamics or change existing mate eligibility.
No new dependencies or licenses.
