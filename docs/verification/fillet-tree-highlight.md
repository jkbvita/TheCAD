# Fillet/Chamfer tree highlighting

2026-11-14, Windows x64 Release.

Selecting an edge-treatment feature in the tree now highlights only its
generated faces. Other solid-feature tree selections retain their existing
behavior. The document query follows the existing persistent treatment-face
names into the current body, so a downstream cut highlights the remaining
shortened faces rather than historical surfaces. Deleted faces have no
highlight; there is no whole-body fallback. Clicking does not regenerate
geometry or execute a Boolean operation.

The viewport validates highlighted faces against the displayed body. Tree
selection changes, empty-space clicks, ordinary face selection and body
replacement clear the feature-face highlight. Camera position is unchanged.

Release builds and four CTest suites passed: `architecture.boundaries`,
`ui.solid-features`, `ui.modeling-workflow`, and `ui.plane-navigation`.
New data-driven GUI cases cover Fillet and Chamfer, each with and without a
downstream cut, save/reopen, face-only highlighting, empty-space clearing,
switching to direct face selection, and camera preservation. Those four cases
also pass at 150% scaling. Two pre-existing optional saved-model fillet fixtures
remain unconfigured/skipped; no tests failed.

Manual check: select a Fillet and Chamfer in the tree. Only the treatment faces
should turn blue. Clear by clicking empty space, then select an ordinary face.
Cut through part of a fillet, save/reopen and select the earlier fillet: only
its surviving surfaces should highlight. Orbit and repeat without camera jumps.

No geometry algorithms, solver semantics, file schema or dependencies changed.
This fix covers Fillet/Chamfer highlighting; it does not redesign selection for
all other feature types. No owner decision is pending.
