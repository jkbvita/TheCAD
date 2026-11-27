# 0071 — Associative cross-part sketch references

- **Status:** Approved for implementation by the Assembly Milestone 2 task
- **Date:** 2026-11-27
- **Scope:** Stable references from Edit In Place sketches to other assembly parts

## Decision

Allow an explicitly activated External References command to project stable
source geometry from another visible, linked `.thepart` component into the
currently edited part's sketch. The user explicitly chooses either an
associative link or a non-associative copy. References are attached to the
source assembly component instance, linked file path, source feature/entity
IDs and point role. Source and target component transforms are composed into
the target sketch's local frame.

Supported sources are datum and base reference geometry, persistent sketch
points (including circle centers and line endpoints/midpoints), and straight
sketch lines. Projected geometry is construction geometry. BRep face/edge
associativity is excluded until persistent topology naming is available.

The `.thepart` writer advances to schema version 28 only when a sketch has
external-reference data. Readers continue supporting versions 1–27. The
assembly format is unchanged. Existing path and component-instance identity is
used; an immutable document UUID migration is deferred because stable part
feature/entity IDs, the canonical linked path and exact component instance
identify the supported source in the present workflow. A replacement file at
the same path is treated as a new revision of that component and is subject to
source ID/type validation; adding stronger replacement detection would require
a future persistent document UUID.

Associative updates are transactional: resolve references, validate the
dependency graph, update a candidate sketch/document, regenerate downstream
features and solve mates, then commit. A failed resolve/regeneration preserves
the last valid dependent geometry and reports a diagnostic. Cycles are rejected
and references are never switched to another instance or nearest geometry.
Breaking a link removes association metadata while preserving the projected
construction entity.

## Alternatives considered

1. **One-time copy only.** Simple and robust, but does not support source-driven
   updates requested for in-context editing.
2. **Implicit association on context click.** Convenient, but creates hidden
   cross-document dependencies. Rejected; the user selects the explicit mode.
3. **Stable datum/sketch references with explicit Copy or Associative mode.**
   Chosen. It delivers useful parametric links without relying on volatile BRep
   topology.
4. **Add immutable part UUIDs now.** Stronger detection when a path is replaced,
   but requires a native file schema migration and policy for legacy documents.
   Deferred; the existing linked path, assembly instance ID and persistent
   source feature/entity IDs are adequate for the supported milestone. The
   remaining path-replacement limitation is documented.

## Consequences

- Ordinary context geometry remains presentation-only; the dedicated picker is
  active only for the External References command.
- The viewport selection mode cannot modify a source component.
- Repeated instances of one part are distinct reference sources.
- The dependent part remains independently stored as `.thepart`; references
  persist in that sketch and are resolved through the owning `.theasmb`.
- A part opened outside its owning assembly retains the last projected geometry
  but cannot refresh its cross-part links until resolved in that assembly.
- Existing part and mate solvers remain in place; no persistent topology
  naming, nested assemblies or new mate types are introduced.

## Acceptance checks

- A stable sketch point, circle center, sketch line and datum can be referenced
  across component transforms.
- Two instances of one source part remain distinguishable.
- Associative geometry follows source edits; copied geometry does not.
- Invalid or missing references do not overwrite the last valid result.
- Save/reopen retains the source identity and mode.
- Circular dependencies are rejected.
- Existing assembly mates and placements survive a valid source update.
