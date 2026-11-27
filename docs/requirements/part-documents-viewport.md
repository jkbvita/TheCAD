# Window, viewport, and native part-document milestone

- Status: Owner-approved scope.
- Approval: Project owner's implementation request in this chat, 2026-11-02.
- Related record: [0002](../decisions/0002-native-part-documents.md).

Improve the existing standalone C++ / Qt 6 / OCCT prototype in place.

## Acceptance criteria

- Start maximized using Qt's normal window behavior, with title bar, window
  controls, taskbar access, restore, and manual resize preserved.
- Render across the full available central viewport at startup, maximize,
  restore, resize, and high DPI. Use actual native surface dimensions, without
  fixed rendering rectangles. Preserve orbit, pan, zoom, Fit All, and Isometric.
- Provide a native Qt File menu: New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S),
  Save As (Ctrl+Shift+S), and Exit (Alt+F4), with normal Windows file dialogs.
- Save and reopen actual supported part geometry and parameters using a documented,
  versioned native file structure, with space for future feature dependencies.
- Save reuses the active path; Save As switches it only after a successful save.
- Track unsaved model state. Before replacement or close, allow Save, Discard,
  or Cancel. Failed or canceled saves block the pending replacement/close.
- Reject invalid or unsupported files clearly while retaining the active part.
- Keep UI, document data/storage, and geometry code logically separate.
- Document permanent `.tcpart`, `.tcasmb`, and `.tcdrw` extensions, implementing
  only `.tcpart` in this milestone.
- Build, launch, and test locally; commit and push a feature branch without merging.

## Boundaries

The file structure is provisional; the three extensions are permanent naming
conventions. This work does not add sketches, extrusion tools, assemblies,
drawings, parameter-editing controls, a general feature evaluator, or a permanent
parametric architecture. New retains the current default prototype box. Camera
changes are view state, not unsaved model edits. See [file formats](../file-formats.md).
