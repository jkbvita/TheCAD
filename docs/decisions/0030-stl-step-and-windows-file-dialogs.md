# 0030 — STL/STEP export and standard Windows file dialogs

Status: Accepted. Owner explicitly requested STL/STEP saving and the classic
Windows dialog on 2026-11-06. Baseline fa88df8; branch
codex/stl-step-export-native-dialogs. Custom Save As UI is deferred by the owner.

## Behavior

File → Save As offers TheCAD Part (.thepart), STEP (.step/.stp), and STL Mesh
(.stl) in the standard platform dialog. Open also uses the platform dialog.
No custom Save As page is introduced. File extensions follow the selected type;
if normalization changes an existing destination, that actual file requires
replacement confirmation. The stock dialog's overwrite confirmation remains on.

STL/STEP export copies all final solid bodies. It does not change the active
native path, clear the document's dirty state, alter source geometry/meshing,
or record a modeling edit. Plain Save and the Save Changes prompt save editable
.thepart documents; they cannot mistakenly accept a geometry export as a saved
parametric document. Pending feature drafts must be confirmed/canceled first.

The .thepart format and legacy loading remain unchanged. STEP stores solid BRep
geometry in millimeters; STL is a unitless binary triangle format written using
millimeter coordinates. Neither exchange format carries TheCAD feature history,
sketch relations or reference annotations. Import and export options UI are not
part of this request.

## Implementation and dependencies

A QWidget-independent exchange::ShapeExporter receives geometric values and
writes an isolated deep copy. STL triangulation uses at most 0.05 mm chordal
error (scaled down for small models) and 0.1 rad angular deflection. Every face
must have triangulation; incomplete STL meshes are rejected. STEP uses the pinned
OCCT translator, AP214 defaults and explicit millimeter input/output units.

Both writers finish into a private temporary file before QSaveFile atomically
replaces the target. Failures preserve existing destination contents. Unicode
paths, cut geometry, multiple bodies and tilted sketch planes are covered.

The new exchange target uses TKDESTEP, TKDESTL and TKMesh from the already pinned
OCCT 7.9.3 SDK (LGPL 2.1 with the OCCT exception). No new external library/version
or modeling backend is introduced. This SDK's exchange toolkits have transitive
OCCT ApplicationFramework/Visualization dependencies, so exchange is included
with the desktop configuration; GUI-free core/application builds stay separate.
Packaging discovers the extra DLLs and retains their license notices. XSMessage,
SHMessage, XSTEPResource and UnitsAPI resources are bundled and registered at
startup rather than depending on a developer SDK installation.

## Primary references

- Qt QFileDialog native-dialog/default-suffix behavior:
  https://doc.qt.io/qt-6/qfiledialog.html
- Windows default extension follows selected file types:
  https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-setdefaultextension
- Pinned OCCT STEP writer implementation/API:
  https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/STEPControl/STEPControl_Writer.cxx
- Pinned OCCT STL writer implementation/API:
  https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/StlAPI/StlAPI_Writer.cxx

## Verification

The full 40-target CTest suite passed. Focused export, document/UI and architecture
checks passed again after the draft guard and final error text. Export tests read
STEP back and check solids, validity, volume, placement and millimeter units; STL
checks include triangle count, winding, bounds and volume. Source mesh handles
and native document bytes remain unchanged. Unicode paths and failed writes are
covered. UI checks verify native dialog options, extensions, export path/dirty
state, and native-only Save Changes behavior.

Standalone package verification passed for saved-file launch and actual export/
read-back in an isolated probe with only packaged DLLs/resources and Windows on
PATH. Package: 77.4 MiB, 59 runtime DLLs. The built/package executable SHA-256:
2C54A834D4241865B36825EAFB5FAE1969AC88369B21C00852FCB2D78ED6716D.
The optional owner-model startup test was not configured; synthetic fixtures were
used. Native dialog visual checks remain on the manual checklist.

The desktop shortcut now targets build/package/TheCAD-STL-STEP-Export/TheCAD.exe.
Its previous link was backed up under build/desktop-shortcut-backup/
TheCAD-before-stl-step-export-20261106.lnk. Previous packages and the original
checkout's untitled.tcpart were preserved. No owner decision is pending.
