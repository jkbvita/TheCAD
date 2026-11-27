# 0060. Sketch Mirror axis stability and selection

Owner-requested correction, 2026-11-17. Mirror opens with **Mirror about** active.
The first valid viewport pick supplies a straight sketch line (ordinary or
construction), then picking automatically switches to **Entities to mirror**.
Both use the existing PropertyManager selection controls, without a dropdown or
an additional pick button. Entity picks accumulate without Ctrl; picking one
again removes it. Click the axis field to replace the axis; later entity picks
cannot change it. Delete/context commands in the fields remove selection
membership only. Preselected seed entities are retained, excluding the axis.
Confirmation, cancellation, document undo/redo and persistence remain unchanged.

Dragging or numerically editing a mirrored source or copy temporarily anchors
the associated mirror-axis endpoints. This includes connected replication
chains. Anchors constrain only the current solve, are chosen independently of
existing dimensions/relations, and never become persisted Fix relations. Direct
axis edits remain possible; changing the axis still updates its mirrored
geometry. Existing conflicting geometry is not released to permit a drag.

An additional regression exposed a singularity in the adapter's mirror equation
when paired points coincide exactly on the axis. The adapter now normalizes the
normal equation by the axis length alone, retaining the same reflection
relationship with a finite Jacobian at zero point separation. PlaneGCS source,
document formats and geometry IDs remain unchanged. No dependencies added.

Automated coverage includes unfixed, vertical, partially dimensioned and angled
axes; source/copy endpoint and line-body drags; repeated drags; direct axis and
numeric source edits; on-axis endpoints; save/reopen; axis-first picking, invalid
axis rejection, multiple entity selection, field replacement/removal, cancel and
undo/redo. Qt workflow and live-drag tests run at normal and high DPI.

Manual checks:

1. Mirror an unfixed line about an unfixed construction line. Drag endpoints and
   outlines on both sides: the axis stays still throughout the drag.
2. Drag the axis itself: it remains editable and the reflection follows.
3. Invoke Mirror with nothing selected. Pick the axis, then several entities.
   Click the axis field to replace it; entity picks must retain the chosen axis.
4. Confirm, undo/redo, save and reopen. Repeat a source/copy drag.

Existing limitation: Mirror supports straight lines, circles and sketch points;
adding mirror support for spline/arc entities is outside this correction.

Verification: Release build succeeded; all 60 CTest suites passed in 100.42
seconds, including both DPI variants of the selection and sketch-drag tests.
The standalone package passed all four part/assembly document argument launches
(absolute and relative Unicode/space paths); the optional owner-model check was
not requested and was skipped. Packaged and built executable SHA-256 hashes match.
