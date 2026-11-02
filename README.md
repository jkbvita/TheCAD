# TheCAD

TheCAD is a standalone, open-source parametric mechanical CAD application under
development. It is not a FreeCAD fork.

## Windows prototype

The first prototype is a native C++ desktop application built with Qt 6,
Open CASCADE Technology (OCCT), and CMake. These are owner-approved provisional
implementation choices, not permanent architecture commitments.

It displays a rectangular solid (100 x 70 x 40 prototype millimeters) in an
OpenGL CAD viewport with orbit, pan, zoom, fit, and isometric view controls.
The window starts maximized and has native File commands for New, Open, Save,
Save As, and Exit. Parts save as versioned `.tcpart` feature recipes that rebuild
the solid and preserve its dimensions. There are no sketches, extrusion tools,
assemblies, or drawings yet.

See [Windows build and run instructions](docs/build-windows.md).
With dependencies installed, run these commands from the repository root:

```powershell
.\scripts\build.ps1 -GuiTests
.\scripts\run.ps1
```

## Source layout

```text
src/main.cpp       Application startup and demo composition
src/modeling/      OCCT geometry construction; no Qt dependency
src/document/      Part data, validation, and atomic native file storage; Qt Core
src/ui/            Qt main window and native OCCT viewport
scripts/           Windows development environment, build, and run helpers
tests/             Geometry tests and optional desktop/OpenGL navigation tests
docs/requirements/ Product vision and prototype acceptance criteria
docs/decisions/    Owner approvals, proposals, and decision template
```

## Project documentation

- [Development rules](AGENTS.md)
- [Documentation guide](docs/README.md)
- [Project vision and principles](docs/requirements/project-vision.md)
- [Prototype requirements](docs/requirements/windows-prototype.md)
- [Provisional technology approval](docs/decisions/0001-provisional-windows-prototype.md)
- [Dependencies and licenses](docs/dependencies.md)
- [Native document formats](docs/file-formats.md)
- [Window and document milestone](docs/requirements/part-documents-viewport.md)

Major architecture and functional behavior decisions require explicit approval
from the project owner before implementation.

## License status

The repository already contains GPL v3 text in [LICENSE](LICENSE), preserved
unchanged. License selection or confirmation remains a project owner decision.
Dependency license information is documented separately; this prototype does not
change the application license.
