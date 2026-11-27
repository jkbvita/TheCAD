# Clean feature tree and hexagon verification

Validated on Windows on 2026-11-04 against decision 0015.

## Automated results

The Release application and test targets compiled. The final complete CTest run
passed all 21 cases, including native GUI checks at normal and 150% scaling.

`ui.documents` now checks that a sketch starts directly under the part, moves
under its first consuming feature, and returns to the root on undo. Redo nests
it again. A second extrusion and a cut reuse the nested sketch and each show
one child referencing the same sketch ID. Editing through the second occurrence
changes the common sketch and regenerates all three features; solid volume is
checked. Save/reopen restores the shared structure without duplicating sketches.
The test also checks collapse-state and selected-occurrence retention.

Plane-selection tests use the Front, Top and Right entries directly beneath the
part, including viewport preselection after selecting the part root. Native
sketch interaction tests check six-edge polygon creation, preview and regular
vertex dragging. Modeling workflows verify absence of Sides/Edit Polygon,
single-action hexagon undo/redo, extrusion, stable IDs and save/reopen.
General polygon model tests retain coverage of earlier variable-side data.

An initial full run passed 20/21 cases: the existing
`deleteDimensionedGeometryAndIndependentDimension` GUI check missed its first
Ctrl+Z at 150% scaling. The next full run passed all 21. A later targeted run
missed Ctrl+Z in `deleteContourAsOneUndoAction` at normal scaling. The workflow
test setup now explicitly waits for an active native window before using
shortcuts; being exposed alone does not ensure shortcut activation. This changes
test synchronization only. The exact cause of the earlier misses is unproven.
After that change, both workflow suites passed twice consecutively with
`ctest -R '^ui\.modeling-workflow(\.hidpi)?$' --repeat until-fail:2` (four runs).

The local Release package was refreshed with `scripts/package-windows.ps1`;
packaged runtime dependency verification passed (50 imported runtime DLLs).

The tree-only screenshots from `ui.modeling-workflow` and `.hidpi` were visually
inspected: a single part root contains the three planes, Origin, Extrude1 with
Sketch1, and ExtrudedCut1 with Sketch2. Artifacts are in
`build/windows/test-artifacts/part-tree.png` and its `hidpi/` counterpart.
Owner visual approval remains separate from these checks.

## Manual review and limits

Open the updated application and compare the English tree with the references.
Select a nested sketch to edit it or start another Extrude/Extruded Cut. Expand
and collapse the features; verify the shared sketch remains available beneath
every consumer. The existing TheCAD icons are retained at a compact size.

Polygon creates hexagons only, by center and radius/orientation clicks. Other
outlines use Line. Existing saved non-hexagonal polygons are preserved. The
general model representation and schema 8 remain unchanged. Expansion state is
retained during UI refreshes, not serialized to the part file.

No new dependency or owner decision is required for this implementation.
