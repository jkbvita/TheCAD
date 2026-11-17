# 0058. Sketch dragging and feature interaction fixes

Requested by the owner on 2026-11-17. Branch:
`codex/sketch-feature-interaction-fixes`. These are corrections to approved
sketch and feature workflows; no new modeling tools or document schemas.

- Express midpoint relations as linear equal coordinate differences in the
  existing PlaneGCS adapter. Omit an extra H/V equation only when a midpoint
  construction already implies it. Keep persisted relations and geometry IDs.
  The bundled PlaneGCS implementation is unchanged.
- Drag straight line bodies using a transient parametric point target. Translation
  seeds preserve an unconstrained line's shape; existing relations determine the
  available movement of attached contours. No helper entities are persisted.
- Starting a solid feature closes Smart Dimension automatically, including an
  unconfirmed numeric editor. Existing dimensions stay intact; pending edits are
  canceled. Solid tools may hand off directly from an edited sketch.
- Linear/circular patterns and Mirror reuse the existing asynchronous preview
  worker, debounce input, discard stale results and cache identical requests.
  Confirm reuses the ready candidate. Expensive kernel operations can still take
  time, but do not run on the GUI thread or repeat for an unchanged request.
- Pattern/Mirror references use the shared geometric selection field. Activate it,
  select in the viewport/tree, then lock the choice until the field is reactivated.
- Fillet/chamfer material faces get a translucent ghost overlay through the solid.
  The body remains opaque; Extruded Cut keeps its existing depth-tested preview.
- Closing with an open preview uses the ordinary Save/Don't Save/Cancel decision.
  Cancel leaves the draft intact. Save writes the committed part and current sketch
  edits, then discards unconfirmed feature previews. A canceled file dialog or save
  failure leaves the window and draft available.

Verification covers midpoint rectangles with construction geometry, corner and
line grabs, origin/relations, free-line translation, Smart Dimension handoff,
reference locking, asynchronous/cached previews, ghost-layer isolation and closing
with active drafts. Release build and all 60 CTest suites passed on 2026-11-17 (91.74 seconds),
including Dark/Light and HiDPI workflows. Two optional owner-model cases were not
configured. The focused midpoint regression first reproduced unchanged geometry
then passed after the adapter correction. New GUI cases exercise the actual
corner/line drag path, all eleven solid-command handoffs, a pending dimension
editor, mirror pick/lock/re-pick, cached preview identity, treatment ghost layers,
and closing with an active preview (Cancel and Save). No new runtime or licensing
dependencies.
