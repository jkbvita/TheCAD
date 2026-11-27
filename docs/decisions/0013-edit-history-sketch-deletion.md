# 0013 — Bounded editing history and keyboard sketch deletion

Status: owner approved, 2026-11-04, through the request for Ctrl+Z, Ctrl+Y, ten
retained actions, light-blue selection and Delete/DD for geometry and dimensions.

The application retains ten completed modeling edits in an in-memory session
history. Ctrl+Z restores the previous edit state; Ctrl+Y restores an undone state.
An accepted edit after undo clears redo. New/Open clears both stacks. Save retains
the session history and records its revision so returning to the saved state clears
the modified flag. History is not serialized to `.thepart`.

States contain the existing part document, active sketch draft/support ID,
selected feature ID, displayed solid and edit revision. Geometry, stable IDs,
relations, dimensions and annotation placement restore together. Existing immutable
OCCT handles are retained; no solver or CAD-kernel history mechanism is replaced.
The bounded container is separate from UI restoration and is independently tested.

Completed line segments, circles, rectangles, point drags, annotation drags,
dimension/relation additions/edits/deletions, numeric entity edits and confirmed
features are actions. Each drag is one action on release. Selection, hover, camera
movement, drawing previews, canceled/failed edits and unchanged sketch submissions
are not actions. Finishing a sketch commits its draft without adding a duplicate
history entry. Undo after Finish can reopen the prior sketch edit state. Pending
feature panels and Modify editors must be completed/canceled before model history
is available; ordinary text editors keep their normal text-editing shortcuts.

Select-mode geometry and explicitly selected dimensions use light blue. Ctrl+click
toggles additional edges/dimensions so a contour's boundary can be deleted as one
action. Click empty space to clear selection. Dimensions have their own selection
IDs; selecting an annotation does not select its referenced geometry. Existing
annotation dragging and double-click value editing remain available.

Delete in the viewport removes the current selection. Delete also works in the
entity list. In the viewport, two discrete D presses within 750 ms delete selected
geometry/annotations. Another key, click, focus loss or leaving the viewport resets
the sequence; auto-repeat does not delete. Smart Dimension keeps its D-for-diameter
variant while creating a dimension; DD is available in Select mode and for an
explicitly selected annotation while the command is idle.

Deleting entities removes every relation/dimension that references their stable
IDs. Unrelated constraints remain. Deleting just a dimension removes its constraint
and annotation while keeping geometry at its existing coordinates. A selection
group is validated and accepted as one draft transaction, with undo restoring all
removed data. No dependent modeling features are silently removed. As with the
existing draft-deletion workflow, deleting an edge used by an extrusion can leave
the draft requiring repair before Finish/regeneration; the last solid is retained.

No new dependencies, license changes or proprietary inspection are involved.
The owner's stated interactions are the specification for this task; local
SolidWorks inspection was offered but was not necessary or performed.
