# Project vision and development principles

## Owner-stated direction

Build TheCAD as a free, open-source parametric mechanical CAD application.
The eventual scope includes:

- Part modeling.
- Parametric sketches.
- Assemblies.
- Technical drawings.
- Customizable workflows.

These are long-term capabilities. Their exact behavior, priority, and acceptance
criteria require separate agreement with the project owner.

## Development principles

- Do not assume SolidWorks workflows are optimal.
- Treat existing CAD systems as references, not specifications.
- Prefer modular, testable architecture.
- Keep the CAD engine separate from the UI where practical. This is a design
  principle, not a selected architecture or engine.
- Let the project owner approve major architecture and functional behavior
  decisions individually before implementation.
- Record decisions and their rationale in `docs/decisions/`.
- Present options and trade-offs for unclear requirements.
- Work autonomously on ordinary implementation details within approved scope.
- Use a feature branch, appropriate automated tests, and documented dependencies
  and licenses; commit and push completed approved work.
- Never push directly to main or merge a pull request automatically.

## Current boundaries

This setup introduces documentation only. It selects no CAD engine, framework,
architecture, or license and implements no CAD functionality.
