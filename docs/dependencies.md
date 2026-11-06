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
excluded except for the corresponding solver/Eigen source bundle described below. No installer or signed public release is provided.

The root [LICENSE](../LICENSE) is unchanged. Dependency license information does
not select or change TheCAD's application license.

The approved revolve/edge-feature batch also links OCCT TKFillet from this same
7.9.3 SDK for fillets and chamfers. It has the existing OCCT LGPL 2.1/exception
license; no new package/version is introduced. The package script discovers the
additional runtime DLL and retains its notices.

## Standalone sketch solver

PlaneGCS is a provisional owner-approved dependency, independently extracted from
FreeCAD commit 0cafef1e58284d08a244f9eba85aee0002e3e350. TheCAD does not depend
on the FreeCAD application. It links the replaceable thecad_planegcs.dll, using
only a private backend-neutral modeling adapter. The public model and document
format have no GCS/Eigen/Boost types. This target alone requires C++20.

| Component | Pin | License / distribution |
| --- | --- | --- |
| PlaneGCS | Exact commit above; eleven source files | LGPL-2.1-or-later; preserved upstream notices, modified corresponding source and independent rebuild instructions |
| Eigen | 3.4.0 h5112557_2 | MPL-2.0; EIGEN_MPL2_ONLY; retained license and packaged header sources |
| Boost headers | 1.86.0 h57928b3_5 | BSL-1.0; retained license; no compiled Boost library |

See [source, hashes, modifications and obligations](../third_party/planegcs/README.TheCAD.md)
and the [exact header dependency lock](../environment-sketch-solver-win-64.lock).
The isolated sketch-solver environment supplements the existing prototype SDK.
The package carries these notices and the complete modified PlaneGCS source under
licenses/planegcs/, alongside the independently replaceable DLL. This audit does
not approve a permanent application license or imply future packages need no audit.
