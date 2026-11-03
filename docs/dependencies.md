# Development dependencies

The provisional Windows prototype uses C++17, Qt 6 Widgets, Open CASCADE
Technology (OCCT), and CMake. These are owner-approved experimental choices;
no permanent architecture or application licensing decision is implied.

| Dependency | Tested version | Use | Upstream license information |
| --- | --- | --- | --- |
| Qt Core, Gui, Widgets | 6.8.4 | Desktop UI, JSON, atomic file I/O | [LGPL v3 / GPL / commercial options](https://doc.qt.io/qt-6/licensing.html); package declares LGPL-3.0-only |
| Qt Test | 6.8.4 | Optional desktop tests | Part of the Qt package; see Qt license information |
| Open CASCADE Technology | 7.9.3, novtk | Solid geometry and OpenGL CAD viewer | [LGPL 2.1](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/LICENSE_LGPL_21.txt) with [OCCT exception](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/OCCT_LGPL_EXCEPTION.txt) |
| CMake | 3.31.8 | Build configuration and CTest | [BSD 3-Clause](https://cmake.org/licensing/) |
| Ninja | 1.13.2 | Available development tool; VS generator uses MSBuild | [Apache 2.0](https://github.com/ninja-build/ninja/blob/v1.13.2/COPYING) |
| Microsoft Visual Studio 2022 Build Tools | MSVC 19.44 | Windows C++ compiler, MSBuild, Windows SDK | Microsoft toolchain terms; not vendored |
| micromamba | 2.10.0 used to bootstrap | Isolated dependency installation | [BSD 3-Clause](https://github.com/mamba-org/mamba/blob/main/LICENSE) |

No new third-party package is required for native part serialization. The
`thecad_document` target uses Qt Core and the existing modeling library; it does
not depend on Qt Widgets. The modeling target has no Qt dependency.

Prebuilt dynamic libraries come from conda-forge. The [manifest](../environment.yml)
selects the direct dependencies and the [Windows lock](../environment-win-64.lock)
records the tested complete package set and hashes. The [license inventory](dependency-license-inventory.md)
records package metadata for development and transitive dependencies, including
modules that TheCAD does not link. The [Windows runtime package](windows-deployment.md) retains notices for each
copied runtime package in its `licenses/` folder, together with version and source
package metadata. Development tools, headers, import libraries and SDK files are
excluded. No installer or signed public release is provided.

The root [LICENSE](../LICENSE) is unchanged. Dependency license information does
not select or change TheCAD's application license.
