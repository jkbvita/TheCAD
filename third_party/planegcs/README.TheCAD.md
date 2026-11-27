# Standalone PlaneGCS extraction

Source: FreeCAD/FreeCAD, commit 0cafef1e58284d08a244f9eba85aee0002e3e350,
src/Mod/Sketcher/App/planegcs. UPSTREAM.json records original Git blob and SHA-256
identities. Retrieved 2026-11-04. This is a solver-component integration prototype;
TheCAD does not link the FreeCAD application, App, Gui, Python, or Qt through it.

The eleven upstream files retain their authorship and LGPL-2.1-or-later SPDX
identifiers and notices, including Konstantinos Poulios and the FreeCAD
contributors. LICENSE.LGPL-2.1.txt is the upstream license text. Modified files
carry a dated extraction notice. Changes are limited to:

- Replace SketcherGlobal.h with StandaloneExport.h for the replaceable DLL.
- Replace optional Base console/timing facilities with StandaloneSupport.h.
- Include stock Boost graph adjacency_list instead of a FreeCAD wrapper include.
- Replace one C++23 std::unreachable branch with a recoverable logic_error for
  the private C++20 target. The solver's mathematical algorithms are unchanged.
- Add the independent CMake build. The rest of TheCAD remains C++17.

Dependencies: Eigen 3.4.0 (MPL-2.0), compiled with EIGEN_MPL2_ONLY, and Boost
headers 1.86.0 (BSL-1.0). Their supplied license notices are retained under
dependency-notices. Exact package URLs/builds/hashes are in the root
 environment-sketch-solver-win-64.lock. The independent header environment lives
at %LOCALAPPDATA%/TheCAD/dev/sketch-solver by default; override
THECAD_SOLVER_DEPS_PREFIX at CMake configure time. No compiled Eigen/Boost runtime
library is linked. The DLL uses the same MSVC runtime as the application.

Distribution requirements: retain all notices and license texts; provide the
complete corresponding source of this library, its modifications and build
instructions; allow recipients to replace/rebuild the shared library and debug
modifications. Do not impose restrictions that remove those rights. The project
packaging must carry this source directory and notices alongside the DLL.
Header dependencies remain available at their exact pinned package URLs, with
upstream source at https://gitlab.com/libeigen/eigen and https://www.boost.org/.
Future distribution audits must include the actual shipped build and dependency
set. No application license selection is made by this extraction.

TheCAD's public sketch types, dimensions and .thepart records contain no GCS,
Eigen or Boost objects. Only the implementation adapter uses this API. A future
spatial sketch backend can be evaluated separately without exposing it in files.

## Rebuild / replace the DLL

Use Windows x64, MSVC 19.44 (Visual Studio 2022 C++ tools), CMake >=3.24 and
Eigen/Boost headers from the explicit lock. With micromamba already installed:

~~~powershell
micromamba create --yes --prefix C:\TheCAD-deps --file environment-sketch-solver-win-64.lock
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTHECAD_SOLVER_DEPS_PREFIX=C:/TheCAD-deps
cmake --build build --config Release
~~~

Run from this source directory. Copy build/Release/thecad_planegcs.dll beside
TheCAD.exe while TheCAD is closed. The DLL is independently replaceable; preserve
its exported C++ ABI or rebuild the application adapter when changing its API.
Application source and adapter build instructions are in the repository.
Packaged source bundles also include the exact Eigen header sources in Eigen/;
set THECAD_EIGEN_INCLUDE to this directory's absolute path (the directory that
contains Eigen/) to build with them. Boost's complete header distribution is
available at the exact locked conda URL; its BSL-1.0 notice is included.
