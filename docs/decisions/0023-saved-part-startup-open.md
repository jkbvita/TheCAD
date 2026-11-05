# 0023 — Opening saved parts supplied through a desktop shortcut

Status: owner-authorized bug fix, 2026-11-05. The owner reported saving a model,
dropping the saved file onto the desktop shortcut and receiving a blank part.
The owner explicitly confirmed the drop-on-shortcut gesture during investigation.

## Cause and repair

The application constructed an empty MainWindow and entered its event loop without
reading positional arguments. Windows supplied the dropped file's path, but the
program ignored it. The previous package fails the new fresh-process regression
test, displaying `Untitled.thepart* - TheCAD` instead of the supplied saved model.

Main now accepts one positional part path using Qt's command-line parser and opens
it through a shared MainWindow file-loading entry point after showing the window.
File → Open uses the same validation/loading/replacement code. Filename parsing
retains Unicode and spaces, and relative paths resolve against the launch working
directory. Multiple files produce an explicit one-file-at-a-time error.

Only a pristine empty startup document can be replaced without a Save/Cancel
prompt. Existing modified work retains the ordinary unsaved-change flow. Pending
feature/property edits must finish before another document can be opened. Inputs
are validated before prompting and reloaded after a possible save, preserving the
existing same-path save-then-open behavior. Failed opens retain an existing part.
An invalid startup file displays an error and exits after acknowledgment rather
than silently continuing as an empty model.

No automatic last-file restoration or single-instance messaging is introduced.
Launching the general shortcut without a file intentionally creates a new part.
The native extension remains `.thepart`; `.tcpart` stays a legacy input. `.theprt`
is not added as a new format. Serialization, modeling, constraints and schema 13
are unchanged. No new runtime dependencies or licenses are introduced.

## Verification

Fresh-process tests launch the actual TheCAD executable with saved extrusion/cut
and datum features, using absolute/relative paths with spaces and Unicode. They
check that the saved filename becomes the clean document title, the process closes
without an empty-part save prompt and the file bytes remain unchanged. Window
inspection and cleanup are restricted to processes created by the tests.

Shared-loader tests verify the loaded parameters, safe failure and unsaved-change
handling. A cold-viewport test loads before first paint and then checks that the
saved solid's AIS presentation is displayed with the expected volume. An existing
schema-9 extrusion file also opens successfully without a write. Package tests
remove development Qt/shader overrides and use only the system PATH for the child.
See [results and manual checks](../verification/saved-part-startup-open.md).
