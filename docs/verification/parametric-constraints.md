# PlaneGCS, Smart Dimension and multiple-region verification

Date: 2026-11-04. Branch: codex/shared-regions-and-direct-dimensions.
Baseline: 1aef95e, preserving the prior solver/dimension/contour milestone and the user's untracked untitled.tcpart.

## Phased verification

Phase A: standalone PlaneGCS mathematical prototype and Qt-independent adapter
were built and tested before UI work. Phase B: driving/reference dimension model,
version-5 persistence, solve-on-open and transactional regeneration tests passed
before UI implementation. Phase C: event-driven Qt selection, placement, numeric
confirmation, annotation double-click, cancellation and conflict tests passed.
Phase D: constrained endpoint/circle dragging and full regressions passed.
The follow-up fixes bounded shared-edge cells and direct Smart Dimension placement
with an inline Modify editor; see decision 0011.

Release build via scripts/build.ps1 -GuiTests: 18/18 CTest suites passed.
Normal and QT_SCALE_FACTOR=1.5 runs cover viewport geometry/navigation, file
commands/safety, graphical planes, modeling workflows and sketch interactions.
Current Qt tests cover line preselection, live mouse-move preview, floating entry,
Enter confirmation, Escape cancellation, stable-ID edits, repeat creation after an
edit, conflicting values, circle diameter/radius/reference dimensions, automatic
H/V distances between points, two-line spacing and angular values. An actual shared
wall/roof sketch is selected and extruded through viewport events. Additional
math/document cases cover angular/spacing persistence, angle-driven solid volumes,
version-6 rejection under an older schema tag and solver tolerance settings.

| Owner case | Evidence |
| --- | --- |
| A/B: anchored H/V rectangle, width 50 -> 80, height retained | PlaneGcsPrototypeTests, ConstraintSolverTests, DimensionDocumentTests; DOF=0 with width/height |
| C/D: anchored diagonal length and constrained drag | Adapter tests plus actual Qt endpoint press/move/release |
| E/F: radius and diameter | Adapter, document round-trip and Smart Dimension circle-selection UI tests |
| G/H: contradiction and redundancy | Separate statuses and stable problem constraint IDs; rejected edit retains geometry |
| I: save/reopen | Dimension kind, refs, ID, value, driving/reference, annotation, sense and frame survive; reference value updates |
| J: dependent extrusion | Numeric volume 15000 -> 24000 mm3; GUI double-click width edit previews/recomputes the existing feature |
| K: local frames | XY/XZ/YZ persistence/regeneration and actual planar-face attachment; rotated/translated adapter frame |

Invalid numeric values, missing references, unsupported spatial data, malformed
JSON dimension fields and invalid solver options reject. A nonlinear external
circle tangency with one permitted iteration reports nonconvergence and retains
the input. Full runs use default tolerance 1e-8 and enforce <=1e-6 mm residuals.
Kernel validity and positive solid volume remain required.

Multiple-region tests cover independent circles, nested rings, rectangles with
multiple holes, selecting adjacent nested regions, two disconnected bodies,
simultaneous holes/bosses, forty selected hole regions, exact numeric volumes,
save/reopen, editing operation length, missing-region rollback, shared wall/roof
cells in all origin planes, an internal divider, nested enclosing holes with shared
internal edges cancelled, and rejection of
empty/duplicate selections. A real Qt viewport selection test picks two regions
and confirms a feature containing two bodies.

## Runtime package

scripts/package-windows.ps1 created build/package/TheCAD, about 66.6 MiB and 50
imported DLLs. Import discovery passes with only the package and Windows system
PATH. thecad_planegcs.dll matches the tested Release DLL by SHA-256. The package
contains corresponding modified solver source, provenance, LGPL text, Eigen
headers, Boost/MPL notices, dependency lock and independent rebuild instructions.

The prior milestone packaged TheCAD.exe launched from this folder using only Windows system PATH,
without inherited shader/plugin paths. It reached an idle, responding native
window. This is a startup/process check, not a human visual review. Qt event-driven
workflow tests are the GUI interaction evidence; no claim of exhaustive manual
SolidWorks-equivalence testing is made. The owner remains the primary manual tester.

## Manual acceptance checks

1. Activate Smart Dimension; select a line, two points or two lines and move the
   cursor. Check live orientation/sector preview, click to place and type in the
   floating editor. Enter confirms; Escape cancels. Repeat without leaving the tool.
   Create a rectangle on each origin plane. Add H/V width/height dimensions,
   anchor a corner and edit a width annotation. Check the expected geometry and
   blue/black whole-sketch status. Cancel a pending edit and confirm no change.
2. Dimension a diagonal line, drag its free endpoint, and verify the specified
   length. Fix a point and check that it cannot move. Dimension a circle by radius
   or diameter and try an incompatible second driving dimension; inspect the
   red diagnostic and retained geometry.
3. Edit a sketch driving an extrusion. Double-click a dimension and verify the
   body updates, then save/reopen .thepart and check values and annotation placement.
4. Draw several independent loops and a loop enclosing holes. Select several
   actual regions for Extrude/Cut; check toggling selection and resulting bodies.
5. Inspect annotation readability, placement, relation indicators and selection
   feedback at your usual display scaling; report workflow/visual adjustments.

## Known limits and future decisions

- Diagnostics classify the whole sketch. Individual constrained entities within
  a mixed sketch are conservatively blue. IDs aid inspection; there is no minimal
  conflict explanation or complete SolidWorks constraint visualization.
- Circle-circle tangency is external; line-circle preserves its previous side.
  No arcs, internal tangency control or spatial geometry yet. Angular dimensions
  currently support bounded sectors strictly between 0 and 180 degrees.
- Shared complete line edges/vertices now work. Unsplit crossings, partial overlaps
  and touching circle/legacy rectangle boundaries still require further design.
- Multiple bodies have no persistent body names or per-body feature scopes.
- Conservative face fingerprints reject upstream support changes instead of
  guessing references. Dimensioning on an unchanged face works.
- No undo/redo infrastructure; each accepted drag/edit is transactional.
- Escape cancels the floating numeric editor or exits Smart Dimension from the
  viewport. Relation management retains its explicit Close button.
- PlaneGCS remains provisional. Future 3D geometry, references, schema and solver
  require separate evaluation; current public types and files contain no backend
  objects. Application license confirmation remains an owner decision.

## Follow-up runtime package

After the owner saved and closed the older running app, the normal package at
build/package/TheCAD/TheCAD.exe was updated. Launch TheCAD.cmd opens this build.
The source/license bundle is retained. No new dependency was installed. Packaging
never closes an application or discards an open document. The final normal package
was launched with only Windows system PATH, without inherited shader/plugin paths,
and reached an idle, responding native window. Its executable matches the tested
Release binary by SHA-256. The final checkbox-state and popup-position GUI checks
passed at normal/150% DPI, and new required-metadata document checks passed.

The SolidWorks demo exercised single-line selection, placement and its adjacent
Modify dialog. It added one measured dimension, without changing its numeric
value or saving the file. Desktop control stopped when the tool reported physical
Escape; no further computer-use input was issued. Automated TheCAD Qt tests are
the evidence for the broader new interactions; exhaustive visual equivalence is
not claimed. The owner remains the primary manual GUI tester.
