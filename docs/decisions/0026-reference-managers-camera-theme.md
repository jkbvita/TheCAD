# 0026 — Reference PropertyManagers, deletion, camera stability and themes

Status: owner-authorized milestone, 2026-11-05. Ordinary implementation of the
owner's explicit reference geometry, camera and appearance requirements.

The latest accepted local implementation is 4ca0308. Work is isolated on
`codex/reference-managers-camera-dark-theme` in `build/relations-worktree`.
The original checkout, its untracked part, PlaneGCS and solver semantics are
preserved. No new dependency or license is introduced.

## Reference geometry

The existing datum definitions and sequential candidate regeneration remain
authoritative. Docked drafts infer supported construction methods from retained
references selected in the viewport, tree or lists. Users can override inference,
clear individual selections, enter XYZ/direction values, reverse a signed
offset/angle, inspect a preview, confirm or cancel. Unsupported/incomplete or
degenerate selections cannot confirm and never silently fall back to XYZ.
Edit Feature uses the same draft and only earlier history references.

Circular solid-edge centers receive a distinct persistent source kind in schema
15. Earlier schemas 1–14 still load; schema 15 makes the new semantics explicit.
Geometry references retain datum/sketch feature IDs and entity IDs. Existing
solid support signatures and topology indices remain conservative: changes to
solid topology reject regeneration and request explicit reselection, rather
than guessing a different support. Datum and sketch references regenerate
associatively. Persistent topology naming is not redesigned in this task.

Deletion accepts only custom reference features. Built-in origin planes and
origin have no deletable feature entry. A document-level ordered transitive
dependency closure includes sketches, references and solids. Without dependents,
eligible multiple selections delete together. With dependents, a default-Cancel
dialog names each feature and its affected parents; only explicit approval
permits deleting that complete closure. Remaining history regenerates before
the document is assigned. One ordinary edit-history transaction records the
whole operation for Undo/Redo. Neither partial removal nor automatic support
reattachment is allowed.

## Camera

Redisplay, reference visibility, ordinary sketch-display cleanup, reference
previews and contour selection preserve the camera. Selection/highlighting never
calls view navigation. Explicit Fit All and Isometric, mouse navigation, initial
viewport/document framing and deliberate sketch entry retain their navigation
behavior. A regression snapshots eye, target, up vector, scale and projection
across actual tree and native face/edge/vertex picks, reference editing, parameter
changes, cancel and theme switching, including display scaling.

## Appearance

An application-level theme manager owns the Qt palette/styles and emits a theme
change for independent OCCT recoloring. Dark is the first-run default. Preferences
offer Dark, Light and Follow system; QSettings remembers the choice independently
of document data. Qt file dialogs use the application palette; Windows window
controls and Qt high-DPI scaling remain native.

Selection remains light blue; hover remains orange. Underdefined geometry is
blue. Fully defined geometry retains its semantics and uses soft off-white on
Dark, black on Light. Construction curves remain dashed with a contrasting muted
blue, relation symbols green, errors red and references translucent. No solver
classification or modeling operation changes when switching theme.

See the [actual SolidWorks observations and documented scope](../research/solidworks-reference-managers-2026-11-05.md).
