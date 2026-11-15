# 0055. Assembly mate preselection and responsive reference handling

The owner requested selecting two assembly planes before clicking Mate, an
automatically suggested mate that remains editable, and faster assembly/mating
interaction. This is an explicit extension of the approved assembly workflow,
not a change to the part/sketch engines or document formats.

## Interaction

Ctrl or Shift adds/toggles a viewport face selection. Tree multi-selection can
select component/global datum planes and axes; Ctrl can combine a tree reference
with a viewport reference. Both selections stay highlighted without navigating
the camera. Body faces use filled blue selection; datum planes retain outlines.
Empty-space clicks clear the selection. Modifier-assisted selection does not
start a component drag.

Mate prefills one or two selected references. Two planes default to Coincident;
two axes (including cylindrical faces) default to Concentric. The user can change
the compatible mate type, references, alignment and value before confirming.
Unsupported pairs remain invalid; more than two preselected references require
the user to choose a pair rather than silently dropping selections. Editing an
existing mate preserves its saved type and parameters. Confirmation is one
undoable transaction, and cancellation never commits the preview.

## Performance changes

- Build eligible component-local reference descriptors once per loaded source.
  Repeated instances and history snapshots share immutable descriptors and source
  geometry. Do not copy PartDocuments or regenerate reference lists for each tree
  refresh, face pick or Mate editor. Reload creates a new cache with the new source;
  missing named supports still reject transactionally through the existing resolver.
- Exclude unmated floating bodies' zero columns from the dense SVD. Preserve their
  exact placements and add their six free degrees of freedom analytically. Active
  mate equations, tolerances, fixed bodies and conflict validation remain unchanged.
- Reuse an unchanged successful preview when confirming or refreshing identical
  inputs. Do not solve the same mate twice just to confirm it.
- Keep existing component/highlight presentations when shapes are unchanged;
  update transforms when needed. Skip unchanged scene updates. Do not rebuild
  selection outlines or shaded face presentations on each preview update.

No dependency or serialization change is required. Source references remain
persistent names/IDs, not transient face indices. The cache is derived session
data and is not written to `.theasmb` or `.thepart`.

## Verification

The Release build, all 34 core suites and eight GUI suites passed: assembly
workflow at normal and 150% scaling, startup-file launch, document UI, toolbar at
normal and 150% scaling, solid features and modeling workflow. Existing optional
owner-model fixtures were unconfigured/skipped. Tests cover viewport Ctrl/Shift
selection and toggling, tree plane/axis preselection, default mate suggestions,
changing the suggestion, cancel/confirm/undo/redo, clearing selection, stable
camera state, shared/replaced source caches, unchanged AIS object reuse, repeated
preview reuse and DOF/conflict validation with 20 unrelated floating components.
The standalone package also passed runtime dependency verification and native
part/assembly startup checks with absolute/relative Unicode paths. Its executable
hash matches the tested Release build.

A local before/after timing fixture uses six instances of a part containing eight
extruded cylinders. One normal-scaling run measured:

| Operation | Before | After |
| --- | ---: | ---: |
| Enumerate references for six instances | 518 ms | 0.17 ms |
| Refresh tree and scene | 539 ms | 3.0 ms |
| Open Mate | 587 ms | 18.4 ms |
| Five distance-value previews | 3.46 ms | 1.63 ms |
| Ten unchanged scene updates | 1.26 ms | 0.62 ms |

These are fixture/machine measurements, not universal latency guarantees. The
fixture logs timings without brittle wall-clock assertions; structural cache,
presentation-reuse and solver-correctness assertions remain deterministic.

Manual check: Ctrl-select two planar faces or tree planes, click Mate, change
Coincident to Distance/Angle, preview and confirm. Repeat with axes/cylinders for
Concentric. Test toggling a selection, blank-space clearing, cancellation,
undo/redo, save/reopen and source Rebuild. Resize/maximize the application: the
Features strip should keep its spacing, with empty space after Curves.

First loading/rebuilding complex source parts still performs geometry/reference
preparation. Large active mate networks still use the bounded dense nonlinear
solver from the first assembly milestone. There is no pending owner decision.
