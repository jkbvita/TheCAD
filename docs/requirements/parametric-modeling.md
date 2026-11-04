# Parametric sketch and feature milestone

Current extension: [approved parametric constraints and multiple regions](parametric-constraints.md)
supersede this milestone's earlier exclusions of dimensions, solver relations, nested
regions and multiple bodies. Historical scope below is retained for context.

- Status: Owner-approved scope and clarified workflow.
- Approval: Project owner's request and subsequent clarification in this chat,
  2026-11-02.
- Decision record: [0003](../decisions/0003-parametric-sketch-features.md).
- File structure: [Native part schema](../file-formats.md).

Improve the latest working standalone C++/Qt/OCCT application in place. Preserve
existing viewport navigation, Windows window behavior, document safety, and work.

## Native documents

Use permanent `.thepart` (Part), `.theasmb` (Assembly), and `.thedrw` (Drawing)
names, superseding the previous naming decision. Implement only `.thepart`.
Read valid legacy `.tcpart` boxes, preserve their dimensions and IDs, and convert
on Save using a separate `.thepart` destination. Never silently overwrite the
legacy file or discard supported model data.

Persist a versioned ordered history with sketch entities, numeric parameters,
feature IDs, source sketch/contour references, dependencies, extrusion/cut
parameters, and final-result identity. Reopening reconstructs the editable
history and actual OCCT BRep. Failed loads/edits/writes retain existing data.

## Sketches and initial UI

Provide compact FEATURES and SKETCH tabs beneath the standard application menu.
FEATURES contains Extrude and Extruded Cut. SKETCH contains Create Sketch, Line,
Rectangle, Circle, and Finish Sketch. Commands are enabled only in applicable
contexts and perform real operations.

New parts may be empty. Sketches use XY, XZ, or YZ origin planes; entering sketch
mode aligns the camera normal to the chosen plane. Draw lines, rectangles, and
circles using model coordinates, edit their supported values numerically, and
finish/reopen sketch editing. Keep sketch data independent of Qt.

Support several independent simple closed contours within a sketch. Reject
nested, touching, intersecting, open, or unsuitable selected profiles clearly;
never silently choose a different sketch or contour. Preserve persistent entity
identities so deleting and replacing an entity cannot silently retarget an
existing feature.

## Feature workflow

Use the owner's explicitly approved SolidWorks-style initial workflow:

1. Start Extrude or Extruded Cut.
2. Explicitly choose the source sketch.
3. Explicitly choose a closed contour, by clicking its viewport region or choosing
   it from the contour list.
4. Set length/depth and explicit direction. Cut also offers Through all.
5. Inspect the actual BRep preview.
6. Confirm to commit, or Cancel to retain the original part.

Store each operation as an editable feature referencing that sketch and contour.
Recompute dependent features when sketch geometry or extrusion/cut values change.
Additive extrusion and Boolean cut must leave one valid solid. Invalid selections
or failed regeneration do not replace the previous document.

The left feature tree reflects the real ordered history. Selection identifies
features; supported parameter editing works through the tree. Do not implement
suppression, rollback, reordering, or decorative commands.

## Placement boundary and exclusions

Keep UI, document history, and geometry operations separate. Isolate plane
conversion through `sketchAxes` so future planar-face placement can be added
without embedding camera or screen-space coordinates in model data. This seam
does not settle attachment, persistent face references, or topology naming.

No constraint solver, automatic dimensioning, advanced relations, assemblies,
drawings, fillets, chamfers, or unapproved additional tools. The internal file
schema and history evaluator remain provisional. Workflow redesigns and major
future architecture choices require individual owner approval.

## Verification

Build and launch locally. Exercise an empty part, XY rectangle sketch, extrusion,
circular sketch, through-all cut, extrusion-length edit, valid regeneration,
`.thepart` saving, close/reopen, and restored tree/geometry/editable parameters.
Add geometry, invalid-profile, regeneration, serialization, reference-stability,
and legacy-conversion tests. State exactly which GUI interactions were exercised;
do not substitute action hooks for a claimed manual native-dialog workflow.
