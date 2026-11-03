# 0006: Foundational sketch relations and familiar desktop organization

Date: 2026-11-03
Status: Owner-approved milestone scope; implementation remains provisional

The owner requested a closer reproduction of the fundamental SolidWorks
experience, prioritizing functional sketch geometry over visual polish.

Approved scope includes an exact selectable sketch origin, identifiable line
endpoints, screen-space snapping, continuous connected line drawing, persistent
Coincident / Horizontal / Vertical relations, related rectangle and circle
improvements, and safe native-document serialization.

The owner explicitly excluded building a complete general-purpose solver.
A major solver dependency requires discussion if the current data model cannot
safely support the limited relation set. No solver dependency is selected by
this record.

The owner also approved a light desktop layout organized around conventional
menus, Features / Sketch / Evaluate command tabs, an actual document feature
tree, context properties and a neutral viewport. Unsupported requested commands
must be visibly disabled and explained, with no fake behavior. Icons and other
graphics must be original.

A one-click Windows launcher and a self-contained Release runtime folder are
approved deployment work. This does not select a permanent application license,
a public release process, or an installer technology.

See the [detailed milestone requirements](../requirements/sketch-interactions-desktop.md).
