# Undo/redo and sketch deletion verification

## Manual checks

1. Draw several line segments, a rectangle and a circle. Ctrl+Z should remove the
   last completed edit; Ctrl+Y should restore the exact IDs, geometry and relations.
   Make more than ten edits and verify that only the last ten can be undone.
2. Undo twice, then draw something new. Redo must become unavailable. Selecting,
   zooming, panning and canceled/invalid edits must not consume a history step.
3. Dimension a line and circle. Select a line boundary in Select mode: it should
   turn light blue. Delete it, undo and redo. Attached dimensions must disappear
   and return with it; unrelated dimensions must remain. Repeat with DD.
4. Click dimension text or a dimension line and press Delete or DD. The dimension
   should disappear while its line/circle remains unchanged. Undo must restore its
   original driving/reference status, value, references and annotation placement.
5. Ctrl+click the edges around a closed contour, then Delete. One Ctrl+Z must restore
   the entire contour with its relations/dimensions. Click empty space and try
   Delete: nothing should be removed.
6. Drag an endpoint or dimension text, then undo/redo. Each completed drag should
   take exactly one step. Double-click a dimension, change its driving value and
   undo/redo; dependent solids must return to their previous/new shapes.
7. Confirm an extrusion/cut, then undo/redo. Undo should return to the preceding
   sketch/part state. Save, undo and redo back to the saved state; the modified
   indicator should clear again. New/Open starts with empty history.
8. A single D or holding D must not delete. Another key/click/focus change between
   the two D presses should reset the sequence. Numeric fields must retain their
   normal Delete and text undo behavior. Smart Dimension's creation-time R/D
   choices and double-click Modify must continue to work.

## Automated coverage

- `editing.history-delete`: ten-action retention, exact traversal, branching,
  session reset, stable-reference cleanup for attached dimensions/relations,
  unrelated constraint preservation and independent dimension deletion.
- `ui.modeling-workflow` and `.hidpi`: actual Ctrl+Z/Ctrl+Y, history limit/no-op
  behavior, Delete/DD/autorepeat, light-blue selection, Ctrl+click contour deletion,
  independent annotation deletion, annotation/value history, real drawing,
  feature creation and body restoration, saved-state dirty tracking and New/Open.
- Existing solver/document/modeling/sketch and UI suites remain required. GUI
  tests run at normal and 150% scaling; the owner provides primary visual review.

Validation on 2026-11-04: the Release application and all test targets built.
All 20 available CTest cases passed across the full run and focused reruns. The
new feature-history test was corrected to measure the actual mouse-drawn profile
area rather than assume pixel-quantized clicks produce ideal model coordinates.

## Limits

History is session-only, bounded to ten modeling edits, and does not include camera
or selection state. Feature/Modify drafts must be resolved before undo/redo.
There is no new feature-deletion or feature-error suppression workflow. Deleting
sketch boundaries used downstream may require repairing the sketch before Finish;
undo is available to restore them. DD applies in the viewport; Delete also works
in the entity list. Fully overlapping picks retain the existing picking policy.
