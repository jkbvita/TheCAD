# 0001: Provisional choices for the initial Windows prototype

- Status: Approved for this prototype only.
- Date: 2026-11-02.
- Owner approval: Explicit implementation request in this chat on 2026-11-02:
  "For this initial prototype, use" C++, Open CASCADE Technology, Qt 6, and CMake;
  "These are provisional implementation choices, not permanent architectural decisions."
- Related requirements: [Windows prototype](../requirements/windows-prototype.md).
- Supersedes / superseded by: None.

## Problem and constraints

Establish a minimal working standalone Windows CAD application with a native
window, a CAD viewport, navigation, and one rectangular solid. TheCAD is not a
FreeCAD fork. Keep UI and geometry code separate without committing to a full
future application architecture.

## Decision

Use C++ for application code, OCCT for geometry and CAD visualization, Qt 6 for
the desktop interface, and CMake for the build. The owner selected these tools
for the experiment; this record does not claim a completed comparison of
alternative permanent technology stacks.

## Implementation details and consequences

Use Qt Widgets and an OCCT native Windows/OpenGL viewport for the prototype.
Keep geometry creation in a small Qt-independent modeling library and window,
rendering, and input handling in a UI library. Use prebuilt MSVC-compatible
libraries from an isolated conda-forge environment to make local builds practical.
These are limited implementation details, not approval of a permanent document
model, persistence format, plugin system, or parametric architecture.

Dependency versions, licenses, and reproducible package inputs are recorded in
[dependencies](../dependencies.md). The existing root LICENSE is unchanged.

## Validation

Compile a Release application on Windows; test the solid's validity, volume,
bounds, and invalid dimensions. Exercise actual viewport initialization and
navigation on a desktop with OpenGL, inspect rendered output, and launch TheCAD.
