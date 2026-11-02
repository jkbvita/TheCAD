# Decision records

The project owner must explicitly approve major architecture and functional
behavior decisions individually. Record approval evidence; an agent-written
proposal or commit does not constitute owner approval.

## Process

1. Describe the problem, constraints, options, and trade-offs.
2. Create a proposed record using [the template](TEMPLATE.md).
3. Ask the owner to decide before implementing dependent behavior or architecture.
4. Record the explicit decision, approval reference, rationale, and consequences.
5. If a decision changes, retain its history and link its replacement.

Use sequential filenames such as `0001-descriptive-topic.md`. Proposed records
must remain marked proposed until explicit approval is received. See the records below for explicit approvals and the scope of provisional details.

## Pending owner decisions

Discuss these individually, in an order agreed with the owner:

- The next modeling workflow and milestone after native part documents.
- Target platforms beyond the initial Windows prototype and release packaging expectations.
- License selection or confirmation: the existing root `LICENSE` contains GPL v3
  text and remains unchanged.
- Permanent engine, framework, and architecture choices after evaluating the
  provisional prototype against requirements.
- Detailed sketch, part, assembly, drawing, and workflow customization behavior
  as each capability enters an approved milestone.

## Records

- [0001: Provisional Windows prototype choices](0001-provisional-windows-prototype.md)
  - Approved for the initial prototype only.

- [0002: Native document names and provisional part persistence](0002-native-part-documents.md)
