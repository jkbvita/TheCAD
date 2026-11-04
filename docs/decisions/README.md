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

- [0005: Foundational sketch relations](0005-sketch-relations.md).
- [0006: Desktop layout](0006-sketch-desktop-layout.md).
- [0007: Sketcher and feature workflow](0007-sketcher-feature-workflow.md).
- [0008: Conservative planar-face attachment](0008-reference-face-attachments.md).
- [0009: Provisional PlaneGCS, dimensions and future 3D seam](0009-planegcs-parametric-dimensions.md).
- [0010: Multiple selected regions and solid bodies](0010-multiple-regions-and-bodies.md).

- [0011: Shared-edge regions and direct Smart Dimension](0011-shared-regions-direct-dimensions.md).
- [0015: Clean part tree and fixed hexagon command](0015-clean-feature-tree-hexagon.md).

## Pending owner decisions

- Resilient face references beyond the approved conservative planar-face strategy.
- Permanent solver selection after PlaneGCS evaluation; 3D backend and later workflows.
- Permanent engine/framework/architecture choices after prototype evaluation.
- Target platforms beyond Windows, public release packaging, and application license confirmation.

The root LICENSE is unchanged. Current scope includes the approved planar solver and Smart Dimension milestone.
