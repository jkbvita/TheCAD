# 0070 — Shared viewport shell and BRep picking

- **Status:** Accepted for implementation
- **Date:** 2026-11-26
- **Scope:** Part and assembly viewport infrastructure

## Decision

Use one application UI viewport base for resources and behavior that should be
identical across workspaces: Open CASCADE surface lifecycle, camera navigation,
wheel/touchpad routing, DPI conversion, theme application, fit and standard
views. Keep Part and Assembly as separate workspace adapters because their
document selection, sketch gestures, component manipulation and mate
interactions have different state and semantics.

Move exact solid-face ray queries, front-to-back visibility ordering, instance
placement handling, and displayed-edge projection hit testing into a shared
`BRepScenePicker` service in the graphics module. Both workspaces provide their
visible BRep scene and consume stable item IDs and local face indices from the
service. The service owns cached OCCT intersectors, not application document
state or widgets.

## Alternatives considered

1. **Continue with independent viewport implementations.** Minimal change, but
   camera/input and selection fixes keep drifting between part and assembly.
2. **Make one monolithic CAD viewport for every workspace.** Maximizes code
   reuse but entangles sketch editing, assembly mates and component dragging,
   and makes each workspace depend on the other's interaction state.
3. **Share a viewport shell and focused graphics services (chosen).** Reuses
   common rendering and geometric picking while preserving workspace-specific
   controllers. The remaining cost is a small adapter layer in each workspace.

## Consequences

- `ViewportWidget` is the common owner of `GraphicsScene`, camera navigation
  and touchpad navigation for both workspaces.
- `BRepScenePicker` is the common exact BRep visibility/picking service and
  supports transformed assembly instances.
- Sketch entity selection, part feature selection and assembly mate/component
  interactions remain in their respective workspace implementations.
- `GraphicsScene`, OCCT geometry, document formats and camera selection policy
  are not replaced.
- New viewport workspaces can reuse the shell and picker while supplying their
  own interaction adapter.

## Acceptance

- Both Part and Assembly build against the shared viewport base.
- Both use the same nearest-visible-face and edge occlusion service.
- Non-GUI regression tests cover nearest face selection, transformed instances,
  hidden components and cleared scenes.
- No geometry or document serialization changes are made.
