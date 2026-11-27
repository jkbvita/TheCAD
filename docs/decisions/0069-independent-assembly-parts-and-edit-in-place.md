# 0069 — Independent assembly parts and Edit In Place

- **Status:** Accepted for Milestone 1 implementation
- **Date:** 2026-11-25
- **Scope:** Assembly ownership, in-place editing, association and source refresh

## Decision

New parts created in an assembly are saved as independent `.thepart` documents.
The `.theasmb` retains an external component instance and uses the existing
version 2 workflow metadata for an optional `InPlacePart` association. Editing
reuses the normal part `MainWindow`/`DocumentSession`; saving the source asks
open assemblies to reload and transactionally commit the new source.

An associative in-place reference is limited to an existing stable plane or
planar frame reference that drives the component's placement. A stale,
ambiguous or invalid reference is rejected. Existing embedded generated parts
and belt workflows remain supported. No solver redesign, persistent topology
naming, nested-assembly flattening or serialization version change is included.

## Alternatives considered

1. **Keep all in-context parts embedded in `.theasmb`.** This retains the
   existing workflow but does not provide independently editable/reusable part
   files or external-source refresh.
2. **Make `.thepart` the definition and keep an instance in `.theasmb` (chosen).**
   This matches the existing external component model and preserves part
   ownership and history. The risk is path-based source identity and the need
   to reject missing or stale sources safely.
3. **Introduce a new component-definition abstraction and document UUID now.**
   This would better prepare for nested assemblies and robust relinking but
   expands the format and core-model change beyond the first workflow slice.

## Consequences

- Existing `.theasmb` v1/v2 and `.thepart` formats are preserved.
- A new external part can initially have no solid body.
- Source refresh uses the existing candidate regeneration and mate-solve path,
  so a failed refresh leaves the previous committed assembly intact.
- The first association is placement-only. It does not create cross-part
  sketch projections or geometry dependencies.
- Nested assembly definitions and unresolved-component relinking remain future
  architecture work.

## References

- Implementation and limitations: [`docs/architecture/assembly-system.md`](../architecture/assembly-system.md)
- Autodesk Fusion references are linked in the architecture document.
