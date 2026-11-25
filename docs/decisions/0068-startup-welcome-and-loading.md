# 0068 — Welcome window and workspace loading

Date: 2026-11-25. Status: approved by the owner in the startup-screen request.
Drawing remains disabled with “Coming later,” as explicitly confirmed by the owner.

## Workflow and scope

An ordinary launch shows a lightweight Welcome window before constructing any
CAD viewport. Home offers Part, Assembly, Drawing (disabled), Open, recent
documents, and recent/pinned folders. Recent supports name/path and document-type
filters. Right-click menus pin, unpin, remove, or clear unpinned entries. Add Folder
uses the native Windows folder picker and pins the chosen folder. Clicking a folder
opens the native document picker there. The File menu can reopen Welcome or browse
recent documents/folders in both workspaces.

Reference: official SolidWorks [Home](https://help.solidworks.com/2025/English/SolidWorks/sldworks/c_home_tab.htm),
[Recent](https://help.solidworks.com/2025/english/solidworks/sldworks/c_recent_tab.htm),
and [Welcome](https://help.solidworks.com/2026/english/SolidWorks/sldworks/c_welcome_dialog_box.htm)
documentation. No SolidWorks GUI interaction was used in this task. Original icons
and the existing TheCAD palette are used; no proprietary assets were copied.

The startup panel deliberately omits SolidWorks account/news/training links,
alternate templates, document thumbnails, and unsupported drawing functionality.
There is no new document format or modeling subsystem.

## Integration

* `application/RecentDocuments` is a Qt Core preferences service. It accepts an
  injected QSettings store for isolated tests, normalizes absolute paths, deduplicates
  case-insensitively, and retains 50 unpinned documents / 20 unpinned folders plus
  pinned entries. Successful open/save operations record native documents; exports,
  missing files and failed loads do not become recent documents. A missing entry
  remains visible and removable, without silently opening an empty document.
* `ui/startup/WelcomeWindow` displays the lists and emits typed workspace/open
  requests. Merely showing Welcome never creates an empty part or initializes OCCT.
* `ui/startup/WorkspaceLauncher` creates the existing MainWindow or AssemblyWindow,
  loads a supplied file using the existing path, and presents a minimal loading panel.
  The native workspace is initially transparent; it is revealed only after the
  viewport reports a completed redraw. Graphics initialization failures also release
  the cover, allowing the existing error reporting/recovery UI to remain accessible.
  There is no artificial two-second delay or guessed progress percentage.
* Direct filename arguments (including shortcut drops, Unicode, spaces, relative
  paths, and legacy `.tcpart`) bypass Welcome and route to the appropriate workspace.
* Welcome opens documents in separate windows and never replaces unsaved work in an
  existing window. Existing Ctrl+N/Open/Save behavior inside a workspace is retained.
* `app.welcome` and `file.open_recent` are cataloged as legacy-backed UI commands;
  they do not claim headless modeling support.

All preference data uses the existing TheCAD settings namespace. No new libraries
are required. The consolidated desktop target remains `TheCAD-current`.

## Verification

Headless tests cover routing, persistence, pinning, ordering, case-insensitive
deduplication, bounded history, missing/unsupported files, and removal without disk
deletion. Offscreen Qt tests cover both new-document requests, disabled Drawing,
recent-file activation, missing-file feedback, filtering, and the loading/reveal
gate in dark and light themes at different display scales. They do not construct
OCCT viewports or open native dialogs.

The Release app must compile. Native OpenGL startup, desktop shortcut/drop behavior,
native pickers, and transition appearance require the owner's manual check; tests
that pop up windows are intentionally not run.

Result: Release app compiled successfully. Seven focused suites passed:
`application.commands`, `application.session`, `application.recent-documents`,
`assembly.document-session`, `architecture.boundaries`, `ui.welcome-offscreen`,
and `ui.welcome-offscreen.hidpi`. Offscreen tests load the installed Segoe UI font
explicitly because Qt's Windows offscreen platform has no native font database.
The actual application continues to use the normal Windows font configuration.
The existing native assembly-entry regression now waits for the deferred workspace
and its first-frame reveal before checking that the original part remains intact.
It is compiled but deliberately not run, because it opens visible native windows.

Manual checklist:

1. Start from the desktop shortcut: Welcome appears with no empty viewport flash.
2. Choose Part, then repeat with Assembly: loading panel gives way to the correct workspace.
3. Save/open documents, restart, and open them from Recent; pin/unpin and filter entries.
4. Add a folder; clicking it opens the file picker in that location.
5. Move a recent file externally: its entry reports that it is missing instead of opening an empty part.
6. Drop a saved part/assembly onto the shortcut: it opens directly with the loading panel.
7. Open Welcome from File with unsaved work present; that work stays intact.
8. Check Dark/Light and Windows display scaling. Drawing remains disabled.
