# Windows part-document milestone verification

Date: 2026-11-02. Branch: `codex/part-documents-viewport`.

## Build

Built the existing application in Release with MSVC 19.44, Windows SDK
10.0.26100.0, Qt 6.8.4, OCCT 7.9.3, and CMake 3.31.8 using:

```powershell
.\scripts\build.ps1 -GuiTests
```

All five CTest suites passed after the final graphics-close fix:

| Suite | Coverage | Result |
| --- | --- | --- |
| `modeling.box` | Valid solid topology, face/solid counts, volume/bounds, invalid dimensions | Passed |
| `document.part` | Exact parameter and BRep round-trip, stable IDs, Unicode paths, Save/Save As, schema/version/type/dependency rejection, locked-destination atomic failure | Passed |
| `ui.viewport` | Actual OCCT surface size at maximized startup, restore and multiple window sizes; orbit, both pan bindings, zoom, shape replacement, fit and rendering | Passed |
| `ui.documents` | Menu actions and shortcuts, live viewport replacement, dirty prompts, Save/Discard/Cancel, same-path reopening after save, failed Save As preserving the existing part/path | Passed |
| `ui.viewport.hidpi` | Same real OpenGL viewport checks with `QT_SCALE_FACTOR=1.5` | Passed |

The document UI report contains 14 passing QtTest entries (12 cases plus setup/
cleanup); each viewport report contains 9 (7 cases including both pan rows, plus
setup/cleanup). Reports and rendered PNGs are generated under `build/windows/`.
GUI suites run serially. Lifecycle tests override dialog choices so all branches
can be exercised deterministically; they use actual files and the real geometry
and document code.

## Desktop observations

Launched the actual TheCAD executable locally through `scripts/run.ps1`.
Confirmed standard maximized startup, normal title-bar controls, a centered
visible solid, and full central viewport coverage. Also observed a restored
normal window with full viewport coverage. Automated desktop tests additionally
exercise maximize/restore/manual resize and navigation at normal and 150% DPI.
The original small bottom-left rendering region was reproduced before the fix.

Confirmed that Save and Save As open the native Windows part-file dialog.
A successful native-dialog save produced `untitled.tcpart` in the working folder;
its contents were inspected and contain version 1, `TheCAD.Part`, the stable box
feature ID and real 100 x 70 x 40 dimensions. That local part is preserved outside
the code commit. Storage round-trip and all lifecycle outcomes are covered by
actual file/geometry tests. Native shell-dialog automation was interleaved with
local user activity, so deterministic lifecycle tests provide the full branch
coverage rather than claiming a completed manual check of every dialog choice.

The graphics cleanup diagnostic found during test review was fixed by releasing
OCCT resources before an accepted close destroys the native window and by deleting
the viewport before its parent destructor. The final CTest log contains no
`wglMakeCurrent` diagnostic.

## Other checks and limits

- Checked Markdown links, conflict markers, staged whitespace, and unchanged root LICENSE.
- No new third-party dependency; geometry, document storage, and UI remain separate targets.
- No physical mixed-DPI multi-monitor test was performed; automated 150% scaling was tested.
- Version 1 supports one box feature, not a general feature evaluator or arbitrary solids.
- There are no parameter-editing controls, sketches, extrusion tools, assemblies, or drawings.
- Internal serialization is provisional. The three approved extensions are permanent.
