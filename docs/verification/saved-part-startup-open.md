# Saved-part startup and shortcut verification

Branch: `codex/saved-part-startup-open`. New package in the isolated worktree:
`build/package/TheCAD-Saved-File-Open/TheCAD.exe`. The previous package and shortcut
backup are retained; no saved model was rewritten during investigation.

## Automated checks

Release build and the full 28-case CTest suite pass. The dedicated
`ui.startup-file-launch` test launches the real executable, passes a saved part as
Windows does for a drop onto an executable shortcut, checks the clean saved-part
title and normal close, and compares file bytes before/after. It covers absolute
and relative paths containing spaces and Unicode, and current-schema features
including extrusion, cut and reference geometry. An optional existing schema-9
extrusion file was also opened successfully without modification.

The same regression test fails against the prior desktop package: it stays on an
unsaved `Untitled` document while ignoring the supplied filename. The fixed build
passes. Packaged-executable runs clear development Qt/shader overrides and restrict
the child PATH to Windows system directories, proving desktop runtime independence.

UI tests cover loading before the viewport's first paint, a displayed solid with
the expected volume, pristine startup without an unnecessary prompt, preservation
of modified work after Cancel, Save before opening another file, missing/damaged
file errors, and rejection of an unsupported `.theprt` suffix. Existing save/open,
legacy conversion, modeling and 150%-scaling workflows remain passing.

To repeat the optional read-only saved-file test:

```powershell
$env:THECAD_TEST_SAVED_PART = 'C:\path to\existing.thepart'
$env:THECAD_TEST_EXECUTABLE = 'C:\path to\package\TheCAD.exe'
# Run thecad_startup_launch_tests.exe from the configured build environment.
```

## Manual owner checks

1. Save and close any old TheCAD instance. Drag one existing `.thepart` from
   Explorer onto the updated desktop TheCAD shortcut. Check the model, feature
   tree and saved filename appear, with no unsaved marker or startup save prompt.
2. Repeat with a saved copy whose folder or filename contains spaces. Repeat with
   accented characters if used in your normal filenames.
3. Edit the model, Save, close it, then drop that file onto the shortcut again.
   Check the saved dimensions/features and body are restored.
4. Open the same model through File → Open and compare. Try a missing or damaged
   test file: an error must be shown, with an existing model preserved.
5. Launch the general shortcut alone. It should create a new blank part. Open a
   saved model with File → Open or by dropping the file onto the shortcut.

The supported action opens one supplied file per new process. Automatic session
restoration and multiple-file launch are outside this fix. No owner decision is
pending; primary native drag-and-drop confirmation remains for the owner.
