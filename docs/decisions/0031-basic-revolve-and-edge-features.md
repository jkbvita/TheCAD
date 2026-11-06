# 0031 — Revolve, Revolved Cut, Fillet and Chamfer

Status: Accepted on 2026-11-07. The owner selected this batch explicitly after
reviewing the official SolidWorks documentation. Baseline 3c503d4; feature branch
`codex/revolve-fillet-chamfer`. This approval does not cover every command in the
reference screenshot.

## Approved behavior

- Revolved Boss/Base and Revolved Cut use explicit closed sketch contours and an
  axis in the sketch plane. Choose a sketch line/centerline, straight solid edge,
  custom reference axis or global axis. Set a positive angle up to 360 degrees
  and forward/reverse direction. Profiles may touch, but must not cross, the axis.
- Fillet uses one constant radius for selected solid edges. Chamfer uses equal
  distances on both sides of selected edges. Both support multiple edges and
  later parameter/selection editing.
- Each command opens a docked PropertyManager with Confirm/Cancel, live material
  preview, numeric focus, Enter/Esc, selection feedback, and safe error messages.
  The viewport and lists select edges; axis selection also accepts reference
  features from the tree. Revolve's two pick controls switch between contours and
  the axis. Ambiguous overlapping references can be selected explicitly in the
  corresponding list. No automatic camera navigation is introduced.
- Confirmation records one document edit in the existing ten-action undo/redo
  history. Cancellation and failed candidates retain the committed document.
  A revolve started from an active sketch resumes that sketch on cancellation.

## Implementation

`modeling/SolidFeatures` uses the existing OCCT engine on isolated geometry
copies. Revolves use MakeRevol and non-destructive Boolean operations; edge
treatments use MakeFillet/MakeChamfer. Invalid, empty, crossing-axis, missing-edge
and non-removing cut results reject before assignment. No solver semantics change.

The document owns typed feature parameters, ordered dependencies and transactional
regeneration. Revolution axes use the existing GeometryReference representation.
Fillet/chamfer edges require unique persistent names of the immediately preceding
solid. OCCT Modified/Generated history propagates source names and names new faces;
derived edges/vertices retain incident-face identities. Removed, split or ambiguous
supports reject rather than use positional or index guesses.

`application::FeatureOperation` generates candidates/material changes. The new
`ui::SolidFeaturePanel` owns forms and uncommitted selections; the shell connects
the panel to the session and viewport. New commands use original vector icons.
Camera eye, target, up, scale and projection are regression-checked.

Files containing these features use schema 17; documents with only existing
feature kinds still write schema 16. Readers retain versions 1–16 and now accept
17. Older builds cannot read these new features. Schema 17 stores angle/axis or
size/edge references and rebuilds solid geometry on opening.

The added TKFillet toolkit is part of the already pinned OCCT 7.9.3 SDK, licensed
under LGPL 2.1 with the OCCT exception. No new external library/version or download
is required. Runtime packaging discovers its DLL and retains the OCCT notices.

## Explicit initial limits

This batch implements solid revolves with a single angular direction, constant
edge fillets, and symmetric distance chamfers. It does not implement thin/surface
revolves, mid-plane/two-direction/up-to end conditions, variable-radius/face/full
round fillets, asymmetric/angle chamfers, or the other screenshot tools. Edge
blends can fail when their size or topology is unsuitable; confirmation remains
disabled until a valid candidate exists. Changes that destroy dependent named
supports reject safely. Feature deletion retains the existing application's scope.

## Primary workflow references

- [Revolve PropertyManager](https://help.solidworks.com/2024/english/SolidWorks/sldworks/HIDD_DVE_FEAT_REV.htm)
- [Revolve profile rules](https://help.solidworks.com/2024/english/SolidWorks/sldworks/c_Revolves_Folder.htm)
- [Fillet PropertyManager](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_fillet_propertymanager.htm)
- [Chamfer PropertyManager](https://help.solidworks.com/2025/english/SolidWorks/sldworks/r_chamfer_pm.htm)

These are documentation-based references. No SolidWorks GUI interactions were
performed for this batch. No proprietary icons, graphics or code were copied.

Verification and the manual checklist are recorded in
[the feature testing guide](../testing/revolve-fillet-chamfer.md).
