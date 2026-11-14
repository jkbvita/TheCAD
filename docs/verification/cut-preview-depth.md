# Material-preview occlusion

2026-11-14, Windows x64 Release.

The yellow removed-material overlay used OCCT's Topmost layer, which has an
independent depth buffer. This made material behind opaque body faces appear
in front of them. Material highlights now use the Top layer, inheriting the
solid's depth while retaining their overlay styling. The same shared preview
path serves extrusions, cuts and edge treatments. Modeling results are unchanged.

The new rendered regression places one preview cylinder behind an opaque body
and another in front. It checks that the hidden one does not tint the body and
that the visible one remains yellow. The test reproduced the failure before
the layer change and passes after it, at normal and 150% scaling.

The Release application build and five CTest suites passed:
`architecture.boundaries`, `ui.viewport`, `ui.viewport.hidpi`,
`ui.solid-features`, and `ui.modeling-workflow`. The solid-feature suite's two
optional saved-model fixtures remain unconfigured/skipped; no tests failed.

Manual check: start a blind Cut-Extrude and orbit around the body. Nearer opaque
faces should cover yellow material behind them; visible preview material should
stay yellow. Check depth edits, reversal, preview toggling, cancel and confirm.
Annotations/selection overlays retain their existing presentation. No new
dependencies or document-format changes. No owner decision is pending.
