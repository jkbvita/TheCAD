# Sketcher interaction and docked feature workflow verification

Date: 2026-11-03
Branch: `codex/sketcher-interaction-property-manager`
Baseline: `371ef7e810eb35140682a440dd22c86c6ccfd615`

## Build and automated checks

Ran `.\scripts\build.ps1 -GuiTests` with MSVC 19.44, Windows SDK 10.0.26100,
Qt 6.8.4, OCCT 7.9.3 and CMake 3.31.8. The final Release build and all **13/13
CTest checks passed**. Earlier stage failures were fixed before this final run.

The suite covers:

- `modeling.box`, `modeling.sketch`, `modeling.reference-planes`: precise geometry,
  supported relations, transactional point moves/joins, fixed origin, invalid
  move rejection, local-plane H/V inference and existing operation validation.
- `document.part`: feature history, regeneration, serialization and invalid input.
- `ui.viewport`, `ui.viewport.hidpi`: rendering surface and navigation behavior.
- `ui.documents`: file commands and clean/dirty state preservation when canceling
  a feature, including immediately opening another draft.
- `ui.modeling-workflow`, `ui.modeling-workflow.hidpi`: four consecutive inferred
  lines forming a rectangle; Features tab retains editing; direct Extrude uses
  the active sketch; explicit region selection; docked non-window panel; live
  depth/reverse preview; invalid input and recovery; orbit during preview;
  cancel/resume; confirmation; direct Cut and Through All; save/new/reopen.
- `ui.graphical-planes`, `ui.graphical-planes.hidpi`: existing plane picking and
  saved plane frames.
- `ui.sketch-interaction`, `ui.sketch-interaction.hidpi`: visible point markers,
  point/body picking, drag-to-join, later connected movement, origin joining and
  immobility, canceled/invalid/conflicting drags, transient guides and indicators,
  all four H/V axial directions on each origin plane, free diagonals, snap
  behavior after pan/zoom and relation persistence after reopening.

High-DPI GUI checks use Qt scale factor 1.5 in addition to normal-scale tests.
No new test framework or runtime dependency was introduced.

## Rendering comparison

The interaction test captures the same exact diagonal/circle sketch at zoom
factors 0.75, 1 and 3. The before presentation uses the former coarse deviation
settings with antialiasing disabled; the after presentation uses the new finer
OCCT display deviation and supported multisampling/line antialiasing. This is a
controlled presentation comparison, not a claim that the prior binary was run.
The geometric sketch definitions are identical.

The before/after images were inspected visually. The finer presentation removes
visible circle faceting and improves diagonal edge continuity on this machine.
Antialiasing availability still depends on the graphics driver. Test output is
local, ignored build data:

- `build/windows/test-artifacts/rendering/sketch-{0.75,1,3}-{before,after}.png`
- `build/windows/test-artifacts/hidpi/rendering/sketch-{0.75,1,3}-{before,after}.png`

## Native graphical checks actually exercised

Used the packaged Windows application through native mouse/keyboard input:

1. Started maximized from the independent Release folder with a system-only PATH
   and without development Qt/OCCT environment overrides.
2. Started a Front-plane sketch, drew two independent lines and observed their
   blue endpoints without selecting each line. Observed smooth line rendering.
3. Dragged one endpoint onto the other line's endpoint, then moved the joined
   point. Both edges moved together and the horizontal relation was preserved.
4. Dragged an endpoint onto the origin and verified its exact zero coordinates.
   Attempting to drag the origin itself left it and the geometry fixed.
5. Observed temporary horizontal and vertical guides/indicators while drawing.
   Closed a three-line profile. An unfinished continuation was safely canceled
   when starting Extrude.
6. Switched to Features while still editing and launched Extrude directly. The
   left PropertyManager used Sketch1 automatically; no contour was preselected
   and confirmation stayed disabled until a region was explicitly clicked.
7. Selected the actual triangle interior. Changed depth from 10 to 18 mm and
   reversed direction; both updated the preview automatically. Orbited the
   viewport while the panel remained active.
8. Canceled and returned to the same three-line sketch. Tried collapsing one
   line onto its other endpoint; the application reported that a line needs two
   distinct points and retained the original geometry.
9. Repeated Extrude, explicitly selected the contour, set positive depth 18 mm
   and confirmed. The history contained Sketch1 and Extrude1.
10. Saved with the native file dialog, reopened the resulting `.thepart` file,
    and verified the regenerated solid and two history features. Opened the saved
    extrusion for editing: the dock showed 18 mm, the saved source and its
    three-entity contour. Cancel restored the normal FeatureManager cleanly.

The native saved test file is
`build/windows/test-artifacts/manual-sketcher-feature-workflow.thepart`.
Inspection of its actual JSON confirmed schema 3, three edges, five persistent
relations, exact matching edge endpoints, a point at (0,0), and the extrusion's
18 mm depth, positive direction, stable contour references and sketch dependency.
The application was left open on this saved solid.

The four-consecutive-line rectangle, complete Cut/Through All paths, high-DPI
scale and pan/zoom snapping regressions were automated checks; they are not
claimed as additional native manual exercises in this milestone.

## Packaging and repository checks

Ran `.\scripts\package-windows.ps1`. Runtime dependency verification passed;
the Release folder includes Qt plugins, OCCT resources and dependency notices.
The packaged executable SHA-256 matches the tested build:
`C809900529550FA685AFC8B64AD3FFDB45BEEE58428E2FBA50223B0AD7CB4F95`.
Launch using the root `Launch TheCAD.cmd` or `build/package/TheCAD/TheCAD.exe`.

The pre-existing untracked root `untitled.tcpart` was preserved and excluded
from this change. Before replacing the previously running test document, its
contents were saved to the local artifact `preserved-before-sketcher-workflow.thepart`.

## Limits and next decisions

The existing equality propagation remains limited to supported Coincident/H/V
relations; no general constraint solver or Smart Dimension was added. Invalid
or contradictory edits reject transactionally. Simple independent closed
contours remain the supported operation inputs; nested/intersecting regions and
new attachment/topology behavior are outside this milestone. Multisampling falls
back to the available hardware capability. Visual checks cover this computer,
not every Windows graphics driver or scale factor.

No major architecture decision is required to use this milestone. Before adding
dimensions or broader constraints, the owner should approve a solver proposal
and its behavior. The C++/Qt/OCCT structure, schema 3, native extensions and license
status are unchanged. No merge into main is part of this delivery.
