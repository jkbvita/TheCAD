# Assembly foundation verification

2026-11-15, Windows x64 Release. The scope and architecture were explicitly
approved by the owner; see [decision 0053](../decisions/0053-assembly-foundation.md).

## Delivered

- Separate assembly document, atomic `.theasmb` storage, session/history, rigid-body
  mate solver and Qt assembly workspace. Existing part/PlaneGCS semantics remain.
- Linked saved solid parts with shared immutable source data and independent
  rigid instances, relative paths, stable IDs and named topology/datum supports.
- Insert preview with mouse/numeric placement, first-component Fix default,
  Fix/Float, numeric XYZ/Euler rotation, mouse translation/rotation, hide/show,
  component tree, open-source-part command and dependent-mate deletion confirmation.
- Docked Mate editor with viewport/tree reference selection, compatible types,
  preview, alignment, values, Confirm/Cancel and double-click editing.
- Coincident and signed Distance for two planes; Concentric for two axes,
  including cylindrical faces; Angle (0–180°) for two planes or two axes.
  Solves active mates together, reports remaining freedom, preserves fixed
  placements and rolls back conflicts/invalid references.
- Ten-action undo/redo, source Rebuild, save/open and startup file arguments.
  Source part files are never rewritten by assembly commands.
- Shared camera navigation, theme, solid appearance and CommandManager widgets.
  Ordinary selection and mate previews preserve camera pose/scale/projection.

## Automated checks

The focused tests cover simultaneous concentric/shoulder/angle mates, remaining
freedom, ungrounded systems, fixed components and conflicting mates; linked
instances, relative-folder relocation, named source-face changes, missing-name
rollback, cylindrical face references, rigid matrix validation and ten-action
history; native viewport face picking after placement, mouse translation and
rotation, cancellation, editing a mate, dependent deletion, save/reopen and camera
invariance. Assembly UI tests run in Dark and Light at normal and 150% scaling.
Startup tests launch actual part and assembly files with absolute/relative paths,
spaces and accented characters and verify source bytes are unchanged.

The complete Release build succeeded. All 34 core suites passed. Ten relevant
GUI suites passed: `ui.selection-construction`, `ui.viewport`, `ui.solid-features`,
`ui.documents`, `ui.feature-commandbar`, `ui.extrusion-panel`,
`ui.modeling-workflow`, `ui.startup-file-launch`, `ui.assembly-workflow` and
`ui.assembly-workflow.hidpi`. Two pre-existing optional saved-part fillet cases and
the optional owner-provided startup fixture were not configured and were skipped.
An old document test was updated to use the still-unsupported `.thedrw` extension
instead of `.theasmb`, and new assembly entry-point checks pass.

The standalone Windows package passed runtime dependency discovery and actual
part/assembly startup checks with only its bundled DLLs/resources. Its executable
SHA-256 matches the tested Release executable. No repeated screenshot testing or
local SolidWorks UI interaction was performed.

## Manual checklist

1. From a part window choose File → New Assembly. Insert two saved solid parts.
   Move the second preview, enter XYZ, confirm/cancel; verify the first is fixed.
2. Float a component. Drag its face, use Rotate Component, and enter numeric
   rotation/translation. Undo/redo; fixed and fully mated components must stay put.
3. Mate two planar faces Coincident/Distance. Mate cylindrical faces Concentric,
   then use planes/axes for Angle. Check Flip alignment, preview and remaining
   freedom. Cancel and try a conflicting mate; the document must stay intact.
4. Double-click an existing mate, change its value/references and confirm. Delete
   it, undo, then select multiple components in the tree and press Delete. Read
   the dependent-mate list and test both Cancel and confirmed removal.
5. Orbit/zoom, select faces/components/tree references and use a mate preview.
   The camera should remain in place. Check Ctrl+1…7 and F for explicit views.
6. Save/reopen `.theasmb`, including dropping it onto the desktop shortcut. Check
   placements, mates and hidden components. Move it with its linked part files
   to a different folder and reopen.
7. Edit/save a source part separately, then Rebuild (Ctrl+B). A surviving named
   face should follow; removed/ambiguous supports must reject safely. Undo the
   rebuild and verify the previous loaded source/placement is restored.
8. Check the Features flyouts and Assembly commands in Dark/Light and at your
   display scaling. Existing sketch/solid commands should behave as before.

## Limits

Small assemblies: at most 64 components/256 mates, using a bounded dense nonlinear
solver. Difficult systems can report unsolved without changing the document.
Only the listed mate/reference pairs are supported; no point mates, mixed
plane/axis mates, advanced/mechanical mates or automatic tangent propagation.
Concentric mates retain axial translation and spin until further constrained.

No subassemblies, in-context edits, virtual parts, configurations, component
patterns, interference/collision analysis, assembly cuts or STEP assembly import.
There is no interactive missing-file/link repair yet; restore the linked path.
Part and assembly documents open in separate top-level windows. No pending owner
decision is required for the delivered milestone.

References consulted: official SolidWorks [Insert Components](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_DVE_INSERT_COMPONENT.htm),
[Fix/Float](https://help.solidworks.com/2020/English/SolidWorks/sldworks/t_Fixing_the_Position_of_a_Component.htm),
[Mate PropertyManager](https://help.solidworks.com/2026/English/SolidWorks/sldworks/HIDD_NEW_ADD_MATE.htm),
[Standard Mates by Entity](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_Standard_Mates_by_Entity.htm)
and [Mates Overview](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_Mates_Overview_Assemblies.htm).
These informed the approved baseline; no proprietary assets or code were copied.
