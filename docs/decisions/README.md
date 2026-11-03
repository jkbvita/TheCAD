# Decision records

The project owner explicitly approves major architecture and functional behavior
choices individually. Record approval evidence; a proposal or commit does not
constitute owner approval.

## Process

1. Describe the problem, constraints, options, and trade-offs.
2. Create a proposed record using [the template](TEMPLATE.md).
3. Obtain the owner's decision before implementing dependent major behavior.
4. Record the approval reference, rationale, and consequences.
5. Retain superseded history and link its replacement when a decision changes.

Use sequential filenames such as `0001-descriptive-topic.md`. Proposed records
stay proposed until explicit approval is received. Ordinary details within an
approved scope do not need repeated approval.

## Records

- [0001: Provisional Windows prototype choices](0001-provisional-windows-prototype.md).
- [0002: Native document names and initial part persistence](0002-native-part-documents.md) — historical extension names and writer schema.
- [0003: Parametric workflow and revised native names](0003-parametric-sketch-features.md) — current `.thepart` family and modeling foundation.
- [0004: Graphical origin-plane sketch creation](0004-graphical-origin-planes.md) — current sketch-creation workflow.

## Pending owner decisions

- Arbitrary planar-face sketch attachment and persistent reference semantics.
- Constraint solving, advanced relations, and later modeling workflows.
- Permanent engine/framework/architecture choices after prototype evaluation.
- Target platforms beyond Windows, deployment packaging, and license confirmation.

The root LICENSE is unchanged. The immediate scope is to make graphical
origin-plane selection reliable, preserving the working application.
