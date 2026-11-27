# 0005: Persistent foundational sketch relations

- Status: Owner-approved milestone; provisional implementation details.
- Date: 2026-11-03.
- Owner approval: Request in this chat for exact origin/endpoint snapping,
  continuous lines, connected rectangles, and persistent coincident,
  horizontal, and vertical sketch relations without a full solver.
- Requirements: [Sketch relations](../requirements/sketch-relations.md).

## Decision

Retain the existing origin-plane sketches and feature history. Give drawn
entities stable IDs before recording references. A point reference identifies
the fixed local origin, a line endpoint, a circle center, or a supported legacy
rectangle diagonal corner. Relations belong to their sketch and have stable IDs.
They do not depend on viewport coordinates, labels, or OCCT presentation objects.

Represent newly drawn rectangles as four line edges with coincident corners and
horizontal/vertical relations. Preserve legacy rectangle entities and their IDs
when opening older documents, so existing extrusion contour references survive.

Use a limited coordinate-equality mechanism: coincidence joins both coordinates,
horizontal joins Y, vertical joins X. A coordinate edit propagates through the
affected groups while the origin remains fixed at (0,0). Reject incompatible
requested values and invalid resulting geometry transactionally. Do not silently
remove a relation or break a connection when editing coordinates.

This supports the requested relations with the current model safely and adds no
dependency. A general solver is unnecessary for these equalities. Dimensioned,
angular, tangent, or more complex constraints need a separately approved design
and solver evaluation; this decision does not choose a permanent solver.

## Serialization and compatibility

Write provisional part schema 3 with explicit relation records and stable point
references. Read versions 1 and 2 without inventing constraints. Reject missing
references, unsupported roles/types, duplicate IDs/relations, and unsatisfied
stored equalities. Continue exact contour references and candidate-history
recomputation. The permanent extension remains .thepart.

## Validation

Geometry tests cover exact propagation around a constrained rectangle, fixed
origin conflicts, circle-center connections, invalid references, deletion, and
valid extrusion after edits. Document tests cover relation round trips, dependent
solid regeneration, malformed file rejection, and legacy loading/upgrading.
Interactive drawing, snapping, and endpoint selection are verified separately
by the viewport/workflow tests and recorded desktop checks.
