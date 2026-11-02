# 0002: Native document names and provisional part persistence

- Status: Owner-approved milestone and naming; provisional serialization details.
- Date: 2026-11-02.
- Owner approval: Explicit request in this chat to improve the existing prototype,
  fix the full-window viewport, and add functional New/Open/Save/Save As/Exit.
  The owner approved `.tcpart`, `.tcasmb`, and `.tcdrw` as permanent extensions,
  authorized a provisional versioned internal structure, and limited this
  implementation to parts.
- Requirements: [Window and part documents](../requirements/part-documents-viewport.md).
- Supersedes / superseded by: None; builds on provisional decision 0001.

## Decision and implementation scope

Keep the existing C++/Qt/OCCT application. Use standard Qt maximized startup,
menu actions, native file dialogs, and Save/Discard/Cancel prompts.

For the only supported model (one box), persist a UTF-8 JSON feature recipe with
format identity, version, units, a stable feature ID, typed dimensions,
dependencies, and a result reference. Rebuild and validate the actual OCCT BRep
from those authoritative parameters when opening. This retains all currently
supported model data without storing display meshes or introducing an opaque
binary dependency. See the exact [version 1 structure](../file-formats.md).

This is an ordinary implementation choice within the owner's explicit authority
to use a provisional internal serialization structure. It does not approve a
permanent JSON format, feature-graph architecture, or future modeling behaviors.
The geometry library remains independent of Qt. A small document library uses
Qt Core for validation and atomic file I/O; the UI manages dialogs and lifecycle.

## Safety and consequences

Loading validates a candidate before replacement. Unknown structure or unsupported
features/versions are rejected rather than silently dropped. Atomic saves retain
existing files on failure. New parts contain the default box and are unsaved;
view navigation does not dirty them. Only part dialogs/commands are exposed.

The viewport fix synchronizes OCCT's cached OpenGL surface with the native child
window's physical client size at paint time. Qt show/resize events can precede the
final HWND size, which previously left the 320 x 240 initialization surface active
inside a larger central widget. No fixed rendering dimensions or offsets are used.

## Validation

Regression checks compare OCCT render dimensions with physical widget dimensions
before image export across maximized startup, restore, and manual resizing, at
normal and 150% DPI. Document and menu tests exercise parameter/solid round trips,
Save/Save As, cancellation, dirty prompts, failed writes, and invalid files.
Actual desktop verification is recorded in the [milestone report](../verification/part-documents-viewport.md).
