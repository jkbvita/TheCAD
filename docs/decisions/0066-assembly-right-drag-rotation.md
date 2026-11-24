# 0066 — Right-button component rotation

Status: Implements the owner's approved SolidWorks assembly interaction baseline.

[SolidWorks' rotation help](https://help.solidworks.com/2021/english/SolidWorks/sldworks/t_Rotating_a_Component_SWassy.htm)
specifies right-button dragging to rotate a component within its available mate
degrees of freedom. Documentation was consulted; no SolidWorks GUI session was
used for this change.

Right drag now maps logical cursor coordinates onto a virtual trackball centered
on the selected component's bounding-box center. Free components rotate around
that center. Mate constraints determine the actual pivot and permitted rotation
for constrained components. Each update starts from the last valid preview, so
blocked motion does not accumulate a hidden rotation that later jumps.

Left drag retains the picked-point movement from decision 0065. Neither gesture
classifies the cursor path as straight or circular. Middle-button and wheel
navigation cannot alter the camera during an active component drag.

The existing drag solver accepts an optional orientation objective. Its mate
equations and constraint validation are shared with point dragging. The rotation
objective is temporary and is not serialized. Invalid rotations are rejected;
fixed components and locked rotational degrees of freedom remain protected.

The document previews through a candidate copy, release commits one history
entry, and Escape discards it. The history limit and file formats are unchanged.

## Verification and scope

Headless checks cover trackball yaw/roll/reversal/scaling, rotation with a fixed
pivot, concentric rotation with an off-axis preferred pivot, locked rotation,
rigidly connected floating components, invalid requests, and preview/undo/redo.
The application and viewport workflow regression executable are compiled. Native
viewport tests require an interactive OpenGL surface and are left for owner
testing because previous Qt offscreen runs failed during initialization.

Manual checks: right-drag a free component; rotate a seated concentric component;
lock its rotation and repeat; cancel with Escape; release and undo/redo. Check
that left dragging and the camera remain consistent. Collision dynamics and
triad handles are outside this increment.
