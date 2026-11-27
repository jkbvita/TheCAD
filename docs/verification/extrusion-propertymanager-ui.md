# Extrude and Cut PropertyManager UI verification

Verified on Windows x64, Release, 2026-11-13.

## Changes

- Compact Boss-Extrude and Cut-Extrude header with confirm, cancel and preview.
- From, Direction 1, Direction 2, Thin Feature, Selected Contours and Feature
  Scope sections with direct reference picking and adjacent reverse control.
- Optional groups expand when enabled; depth/thickness fields select their
  value for immediate typing. Numeric display retains six-place precision.
- Contour/body boxes display chosen members. Delete removes membership rather
  than sketch geometry; context menus remove or clear selections.
- Starting without a sketch opens FeatureManager profile selection and keeps
  the editor visible. Picking the sketch preserves the camera and focuses depth.
- Dark and Light themes and scalable original icons; no new dependencies.

## Automated results

Release build of TheCAD and affected test targets succeeded.

CTest passed all four selected suites: `architecture.boundaries`,
`ui.documents` (31 Qt cases), `ui.extrusion-panel` (5 Qt cases), and
`ui.modeling-workflow` (36 Qt cases). Counts include Qt setup/cleanup cases.

At `QT_SCALE_FACTOR=1.5`, five focused modeling workflows plus setup/cleanup
passed (7 cases), and the extrusion panel tests passed (5 cases). These cover
tree profile selection/camera preservation, active-sketch start/cancel/confirm,
direct depth entry, contour changes, reference picking, previews, feature
editing, save and reopen. The camera regression compares eye, target, up,
scale and projection. Panel checks cover both themes, precision, reverse,
reference clearing and optional groups.

## Manual checklist

1. Open a sketch and start Extrude; type a depth immediately. Reverse direction,
   toggle preview, confirm with Enter, and edit the resulting feature.
2. Start Extrude without preselecting a sketch; pick one in the FeatureManager.
   Check the editor stays visible and the camera stays unchanged.
3. Use Cut-Extrude on a body. Try the available start/end conditions and pick
   their references in the viewport and tree. Clear a reference with Delete.
4. Enable Direction 2 and Thin Feature; change depth/thickness and check preview.
5. Select several contour regions. Check only chosen contours appear; remove
   one with Delete and with the context menu without deleting sketch geometry.
6. Check Feature Scope on a multibody part, including selected-body removal.
7. Check both themes and Windows display scaling; save/reopen and edit settings.

## Limits

This is a UI rebuild using official SolidWorks documentation and the owner's
screenshots, not a claim of complete SolidWorks feature parity. Existing
modeling limits in decision 0042 remain: assembly/sheet-metal options,
equations and direct model-edge profile contours are outside the implementation;
thin-profile, curved-start, draft and cap combinations retain their documented
restrictions. No owner decision is pending for this UI milestone.
