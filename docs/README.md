# Project documentation

- `requirements/` describes intended behavior and owner-approved scope.
- `decisions/` records proposals, trade-offs, and explicit owner decisions.
- `verification/` records completed checks and their limits.
- The root `AGENTS.md` defines development and delivery rules.

Long-term scope is a vision, not approval to implement individual features.
Keep proposed requirements distinguishable from approved behavior and link them
to decision records when a major decision is approved. Initially reproduce the
owner-approved SolidWorks workflows; discuss redesigns individually.

Within approved scope, proceed autonomously with ordinary implementation details,
testing, fixes, commits, and a push to a dedicated feature branch. Seek explicit
owner approval for major architecture or behavior choices. Never merge into main
automatically.

## Current guides

- [Reference faces and contour verification](verification/reference-faces-contours.md)
- [Face attachment and contour decision](decisions/0008-reference-face-attachments.md)
- [Parametric constraints and Smart Dimension](requirements/parametric-constraints.md)
- [Approved PlaneGCS evaluation](decisions/0009-planegcs-parametric-dimensions.md)
- [Multiple regions and bodies](decisions/0010-multiple-regions-and-bodies.md)
- [Current solver/workflow verification](verification/parametric-constraints.md)

- [Sketcher interaction and feature workflow](requirements/sketcher-feature-workflow.md)
- [Workflow state and PropertyManager decision](decisions/0007-sketcher-feature-workflow.md)
- [Independent Windows launch and packaging](windows-deployment.md)
- [Sketch interaction and desktop requirements](requirements/sketch-interactions-desktop.md)
- [Supported sketch relations](requirements/sketch-relations.md)
- [Relation representation](decisions/0005-sketch-relations.md)
- [Desktop layout and deployment](decisions/0006-sketch-desktop-layout.md)
- [Windows build, run, and sketch workflow](build-windows.md)
- [Graphical origin-plane selection requirements](requirements/graphical-plane-selection.md)
- [Graphical plane-selection decision](decisions/0004-graphical-origin-planes.md)
- [Parametric modeling foundation](requirements/parametric-modeling.md)
- [Native document formats and compatibility](file-formats.md)
- [Dependencies and licenses](dependencies.md)

## Historical verification

- [Window and part-document milestone](verification/part-documents-viewport.md)

## Current verification

- [Sketcher and docked feature workflow checks](verification/sketcher-feature-workflow.md)
- [Sketch relations, layout and independent startup](verification/sketch-relations-desktop.md)
- [Graphical plane-selection checks](verification/graphical-plane-selection.md)
