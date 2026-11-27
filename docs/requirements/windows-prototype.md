# Initial Windows desktop prototype

- Status: Owner-approved scope.
- Approval: Project owner's implementation request in this chat, 2026-11-02.
- Related decision: [Provisional implementation choices](../decisions/0001-provisional-windows-prototype.md).

## Scope and acceptance criteria

- Run a native desktop application named TheCAD on Windows.
- Host a 3D CAD viewport in its main window.
- Support orbit, pan, and zoom navigation.
- Create and display a rectangular solid using Open CASCADE Technology.
- Separate UI code from geometry/modeling code in the source tree.
- Configure dependencies and the CMake build; document building and running.
- Compile and launch on the owner's computer if the environment permits.
- Add practical automated checks.

TheCAD is a standalone application and is not a FreeCAD fork.
The prototype uses C++, OCCT, Qt 6, and CMake provisionally.

## Prototype details within approved scope

The initial demo solid is 100 x 70 x 40 model units, displayed as millimeters.
This label does not establish a future document units system. The viewer uses
an initial isometric view. Temporary navigation bindings and fit/reset controls
are documented in the build guide and do not establish final workflow behavior.

## Exclusions

No sketches, extrusion tools, assemblies, drawings, persistence, document model,
parametric feature history, or permanent architecture decisions are introduced.
Dependency packaging is for local development; no installer or redistribution
bundle is produced. Project licensing remains pending owner confirmation.
