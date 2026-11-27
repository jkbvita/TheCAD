# Feature-preview camera verification

Windows x64 Release, 2026-11-13.

TheCAD and the modeling, solid-feature and plane-navigation test targets built
successfully. CTest passed `architecture.boundaries`, `ui.solid-features`,
`ui.modeling-workflow` and `ui.plane-navigation` (4/4 suites).

The solid-feature suite passed 18 Qt cases, including all nine data rows for
starting solid tools from a flat view. Its two optional saved-model fillet
regressions were skipped because `THECAD_TEST_DRAFT_JUNCTION_PART` and
`THECAD_TEST_FILLET_PART` were not set; these are unrelated geometry/performance
fixtures, not failures. Modeling and navigation suites had no skipped cases.

Regression coverage checks profile visibility and depth on XY, XZ, YZ and an
arbitrary translated/angled frame, from front/back/edge-on directions, in both
orthographic and perspective projections. The camera target, scale and
projection are preserved. Already useful oblique views remain unchanged.
Real command tests cover Extrude, Cut, Revolve/Revolved Cut, Sweep/Swept Cut,
Fillet, Chamfer, Mirror and Linear/Circular Pattern. Parameter and contour
updates, tree profile selection and ordinary face selection keep their camera.

At 150% scaling, the new frame/orientation regression and the focused
Extrude/Cut depth/contour and tree-profile workflows also passed (7 Qt cases
including setup/cleanup).

Manual checks:

1. Start Extrude from a normal sketch view on each base plane and an angled
   face/custom plane. Verify the preview's cross-section and depth are visible.
2. Start Cut, reverse its direction and type a new depth. The initial angle
   should reveal the operation; editing values should not turn the view again.
3. Start Fillet/Chamfer or a pattern from Front/Top, then from a useful oblique
   view. Flat views should angle; the existing oblique view should be preserved.
4. Rotate while the editor is open, then change parameters and pick references.
   Check the camera stays where you placed it. Cancel back to an active sketch
   and verify the existing normal-to-sketch editing workflow resumes.

Limits: this changes orientation at command entry, without automatic Fit All,
zooming or continuous camera following. Commands started without a known
profile use a world-frame oblique view. Selecting a profile later does not
reorient the camera. Geometry, constraints and file data are unchanged.
