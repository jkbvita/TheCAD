# Feature PropertyManager verification

Branch: `codex/feature-property-manager`. Package in the isolated worktree:
`build/package/TheCAD-Feature-PropertyManager/TheCAD.exe`.
Save/close older TheCAD instances, then reopen the updated desktop shortcut.

## Automated results

Release build and all 26 CTest cases pass, including nine GUI cases, four at 150%
scaling. `modeling.feature-preview` verifies added/removed volume for new solids,
growth/shrinkage, cuts, unchanged and empty bodies, and unchanged source geometry.
`solver.entity-definition` covers defined ordinary/construction geometry, circle
size versus center freedom and definition propagation through a regular hexagon.

The GUI tests check actual black ordinary/construction presentations, retained
dash style and black center/endpoint marker sprites with unrelated free geometry.
The feature workflow types decimal depths immediately, toggles multiple regions,
checks list counts and region fills, verifies added/removed volumes, preserves
camera state, resumes depth typing after viewport focus, rejects zero/invalid
input on Enter, confirms valid input and cancels back to the sketch with Esc.
Existing save/reopen, attached face, feature editing, Through All, history and
intersection-region tests remain passing.

## Manual owner checks

1. Draw two overlapping closed profiles. Click Extrude, then click the wanted
   bounded region(s). Check blue fill, matching selected list entries and count.
   Click a selected region again to deselect it.
2. Without clicking the left input, type `23.5`. It must replace the current
   depth and update the preview. Select another region and type another depth.
   Orbit/pan, then type a depth; the camera must stay where you left it.
3. Inspect amber added material, then press Enter. Reopen the feature to edit its
   depth. Check that growth/shrinkage previews show changes before confirmation.
4. Start a Cut, select its region and type a blind depth. Check red removed material
   and the resulting cavity. Try Reverse direction and Through All. Depth should
   be disabled for Through All and reselected when returning to Blind.
5. Enter zero or invalid text; Confirm must disable and Enter must not commit.
   Restore a valid value. Esc must cancel and return to the unmodified sketch/part.
6. At your usual scaling and a narrow dock width, check all settings remain
   accessible by scrolling and the Confirm/Cancel and status area stay visible.
7. Fix circles and centerlines, including construction circles. Check black
   geometry and points, retained dash styles, and blue unrelated free geometry.
   Hover and selection intentionally override definition colors.

Limitations: splines/arcs remain unsupported; no new modeling tools are introduced.
The display Boolean work may need optimization for large models. A live comparison
against SolidWorks and primary visual review remain for the owner because Windows
was locked; official documentation was used. No new behavior decision is pending.
