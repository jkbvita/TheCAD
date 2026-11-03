# CAD Project — Development Rules

## Project goal

Build a free, open-source parametric mechanical
CAD application with parts, assemblies and drawings.

## Development principles

- Reproduce the SolidWorks workflows explicitly approved and liked by the
  project owner as the initial baseline; discuss redesigns individually.
- Do not assume any workflow is permanently optimal. Unapproved CAD behavior
  remains reference material, not a specification.
- Prefer modular, testable architecture.
- Keep the CAD engine separate from the UI where
  practical.

## Decision-making

- Major behavior and architecture decisions must
  be approved by the project owner.
- Record decisions in docs/decisions/.
- Do not implement an unapproved major feature.
- For approved implementation tasks, work autonomously on ordinary details:
  implement, test, fix problems, commit and push to a feature branch.
- Obtain explicit owner approval for major architecture and functional
  behavior decisions; ordinary implementation details do not need repeated approval.
- If a requirement is unclear, identify the
  options and trade-offs before implementing it.

## Coding

- Use a separate branch for each feature.
- Add appropriate automated tests.
- Run available tests before submitting changes.
- Document new dependencies and their licenses.
- Never push directly to main.
- Never merge a pull request automatically.

## Deliverables

For each completed task, provide:

- Summary of changes
- Tests performed and results
- Known limitations
- Questions requiring the owner's decision
