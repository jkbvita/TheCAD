# 0003: Initial parametric workflow and revised native document names

- Status: Owner-approved milestone and workflow; provisional implementation.
- Date: 2026-11-02.
- Owner approval: Explicit development request and follow-up clarification in
  this chat. The owner approved sketches, extrusion/cut, a history tree, native
  extension renaming, and a SolidWorks-style initial workflow with explicit
  source sketch, contour, parameters, preview, and confirmation.
- Requirements: [Parametric modeling](../requirements/parametric-modeling.md).
- Supersedes: [0002](0002-native-part-documents.md) for native extension naming and
  the current writer schema. Its historical document-safety requirements remain.
- Related: [0001](0001-provisional-windows-prototype.md) remains provisional.

## Owner decisions

Use `.thepart`, `.theasmb`, and `.thedrw` as permanent Part, Assembly, and Drawing
extensions. Implement only parts. Existing `.tcpart` documents are readable
legacy input; saving converts to `.thepart` without overwriting the legacy path.

Reproduce the explicitly approved and owner-liked SolidWorks workflows first.
Discuss redesigns individually rather than silently replacing that baseline.
Build compact FEATURES and SKETCH tabs, origin-plane sketching, a real feature
history tree, numeric editing, Extrude, and Extruded Cut.

Extrude/Cut require explicit selection of both a source sketch and closed contour,
then length/depth, direction, a valid preview, and confirmation. Independent simple
contours are supported. Reject ambiguous or unsupported nested/touching/
intersecting regions. Never silently change a missing or invalid contour reference.

## Provisional implementation details

Retain the current C++/Qt/OCCT codebase. Sketch data and precise geometry operations
remain Qt-independent. A separate document layer walks an ordered feature history,
regenerates candidates, and commits only valid results. Stable feature and entity
IDs preserve explicit sketch/contour dependencies. New entities cannot inherit
the identity of removed entities during an edit.

The version 2 UTF-8 JSON writer records model inputs and references; actual OCCT
BRep geometry is reconstructed on load. Version 1 legacy boxes retain their exact
parameters and IDs during conversion. See [the schema](../file-formats.md).
This is a provisional evaluator/serialization, not a final feature graph or
reference architecture.

Origin-plane conversion passes through `sketchAxes`: XY=(X,Y,+Z), XZ=(X,Z,-Y),
YZ=(Y,Z,+X). This is a placement seam for future planar-face support, not a chosen
face attachment, topological naming, or reference-stability scheme. Those choices
need separate discussion and approval.

## Consequences and boundaries

New parts start empty. Sketch coordinates and supported values are editable;
feature length/depth/direction and cut mode regenerate downstream geometry.
Open sketches may be saved, but a solid operation requires a valid selected
closed profile. Failed edits or invalid files retain the active model.

There is no solver, automatic dimensioning, advanced relations, suppression,
rollback UI, feature reordering, face attachment, assemblies, drawings, fillets,
or chamfers. Existing file safety and viewport/navigation behavior are preserved.
No new dependency or application-license decision is introduced.

## Validation

Geometry and document tests cover valid/invalid profiles, feature regeneration,
explicit contour persistence, no silent retargeting, versioned round-trips, and
legacy conversion. Desktop tests exercise actual viewport drawing/region picking,
modeling dialogs, preview/confirm, numeric edits, tree/history restoration, and
real model-file storage through file-path hooks. Native Windows file dialogs are
not claimed as fully manually exercised for this milestone; the verification
report records the exact completed checks.
