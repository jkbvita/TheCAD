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

- [Windows build, run, and sketch workflow](build-windows.md)
- [Graphical origin-plane selection requirements](requirements/graphical-plane-selection.md)
- [Graphical plane-selection decision](decisions/0004-graphical-origin-planes.md)
- [Parametric modeling foundation](requirements/parametric-modeling.md)
- [Native document formats and compatibility](file-formats.md)
- [Dependencies and licenses](dependencies.md)

## Historical verification

- [Window and part-document milestone](verification/part-documents-viewport.md)

## Current verification

- [Graphical plane-selection checks](verification/graphical-plane-selection.md)
