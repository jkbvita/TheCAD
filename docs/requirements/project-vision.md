# Project vision and development principles

## Owner-stated direction

Build TheCAD as a free, open-source parametric mechanical CAD application.
TheCAD is a standalone application, not a FreeCAD fork. Its eventual scope
includes parts, parametric sketches, assemblies, technical drawings, and
customizable workflows. Exact future behavior, priority, and acceptance criteria
require individual agreement with the project owner.

## Development principles

- Reproduce the SolidWorks workflows explicitly approved by the owner as the
  initial baseline. The owner likes these workflows; do not redesign them
  implicitly while implementing a milestone.
- Discuss and approve workflow redesigns individually. No existing CAD workflow
  is assumed permanently optimal; unapproved behavior remains reference material.
- Prefer modular, testable code and keep geometry, document data, and UI logically
  separate. Current implementation boundaries are provisional, not a permanent
  full-application architecture.
- Obtain owner approval before major functional or architecture choices; record
  approved decisions and their scope in `docs/decisions/`.
- Present options and trade-offs when requirements are unclear.
- Work autonomously on ordinary details within approved scope: implement, test,
  fix, commit, and push to a dedicated feature branch.
- Document dependencies and their licenses, run appropriate checks, never push
  directly to main, and never merge a pull request automatically.

## Current approved scope

The owner approved a Windows prototype using C++, OCCT, Qt 6, and CMake as
provisional choices, then native part documents and viewport fixes. The current
[parametric milestone](parametric-modeling.md) adds origin-plane sketches,
explicit contour selection, editable extrude/cut operations, a feature tree,
transactional regeneration, and `.thepart` saving with legacy `.tcpart` import.

The permanent native names are now `.thepart`, `.theasmb`, and `.thedrw`; this
owner decision supersedes the earlier names. Only parts are implemented.

A placement conversion boundary (`sketchAxes`) isolates the origin-plane mapping
for future planar-face placement. It does not select face attachment, a permanent
feature-reference scheme, or topology naming. Those designs, constraint solving,
and future modeling tools still need separate approval. The root LICENSE remains
unchanged.
