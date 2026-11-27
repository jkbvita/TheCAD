# 0020 — Feature PropertyManager, immediate depth entry and material previews

Status: owner-approved scope, 2026-11-05. The owner requested consistent black
defined geometry and an improved left feature panel, using the approved workflow:
click inside bounded sketch regions, see the selected operation, and type depth
immediately without manually selecting/deleting the default value.

## Feature panel

Extrude and Cut use a shared PropertyManagerPage view: title/instructions, fixed
confirmation buttons, scrollable grouped settings and a persistent status area.
The layout is available for future approved feature editors, without adding any
new command or placeholder action. Existing source-sketch selection, direction,
reverse, Blind/Through All, multi-region selection and feature editing are retained.
Forms wrap at narrow widths, controls have visible focus and selections have a
count. Error status clears on recovery. A legend explains preview colors.

Opening the panel selects its depth value. Clicking a valid viewport region
toggles that region and returns focus to the selected depth text. A numeric key
from viewport focus also starts depth entry, allowing typing after navigation.
Other inputs and keyboard shortcuts are not redirected. Through All keeps depth
disabled. Enter from the depth control or viewport confirms only a valid candidate;
Escape cancels via the existing action. The preview reads localized numeric text
without reformatting on every keystroke, preserving an incomplete decimal such as
`23.` so the next digit can be entered. Invalid input cannot commit a stale preview.

## Graphical feedback

Selected closed regions are filled blue, including their holes and intersection
boundaries. The list and viewport use the same region references. Added material
is translucent amber; removed material translucent red, alongside the candidate
result body. MaterialChanges derives those volumes from non-destructive OCCT
differences between the original and candidate bodies. This also shows increases,
decreases and downstream regeneration changes when editing existing features.
Display-only geometry never enters persistence or commit data. Camera direction,
scale and target do not reset when updating the preview. Cancel, switching source,
invalid input, empty selection and confirmation clear the appropriate overlays.

## Definition colors and limits

Decision 0019 applies to every supported primitive regardless of construction
status. Tests cover circles, standalone points, lines, centerlines, construction
circles and regular polygon edges/vertices, including defined subsets in a free
sketch. Construction strokes retain their dash style. Direction and point freedoms
remain separate as explicitly approved in 0019. Splines and arcs are not currently
supported sketch entities; this task does not add them. Future types must expose
their actual parameters to the same mobility diagnosis when implemented.

The PlaneGCS solver, saved references and schema 12 are unchanged. No new dependency
or license change. Exact display differences add Boolean work on each valid preview;
very large sketches may need later measured optimization.

## Reference evidence

SolidWorks was launched through the computer-use skill, but Windows was locked.
No live SolidWorks interaction or proprietary graphic was inspected or copied.
Implementation uses the owner's described flow and official documentation:

- [Extrude PropertyManager](https://help.solidworks.com/2026/english/SolidWorks/sldworks/r_extrude_propertymanager.htm?format=P)
- [PropertyManager overview](https://help.solidworks.com/2020/english/SolidWorks/sldworks/r_pm_overview.htm?format=P&value=)

See [verification and owner checks](../verification/feature-property-manager.md).
