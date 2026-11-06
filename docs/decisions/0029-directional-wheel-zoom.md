# 0029 — Wheel direction and asymmetric zoom anchoring

Status: Accepted. The owner explicitly requested this interaction on 2026-11-06.
Baseline: modular architecture commit 4d60782. Branch: codex/wheel-zoom-direction.

Wheel up (positive vertical delta) zooms out around the viewport center. Wheel
down zooms in toward the cursor. This applies consistently to angular and pixel
wheel deltas in part and sketch modes, at different display scales and in both
orthographic and perspective projection. Zero vertical input does not zoom.

The QWidget adapter normalizes wheel input and chooses the anchoring policy.
CameraNavigation owns both camera operations. Centered zoom changes scale only;
it never picks a cursor reference or translates/recenters the camera target.
Cursor zoom retains the existing exact sketch-plane/body-depth behavior.

This updates the wheel portion of the earlier navigation decisions. Middle-button
drag navigation retains its current controls. No solver, geometry, history,
selection, saved-format or runtime dependency change is introduced.

Tests compare zoom-out cameras from different cursor positions, verify direction
and zoom-in cursor anchoring, cover both projections/input types/sketch contexts,
and retain the existing normal/150% navigation and annotation checks.

## Verification and manual checks

Windows Release build succeeded; 39/39 CTest targets passed. Eight new combinations
(projection × part/sketch × angular/pixel input) passed at both normal and 150%
display scaling. Existing camera, annotation, selection and sketch checks passed.
Packaged saved-file launching and runtime dependency verification also passed.

The desktop shortcut targets build/package/TheCAD-Wheel-Zoom/TheCAD.exe in this
worktree. Built/package SHA-256 match:
7DF00853662D611335E0DAC621970C85A1B20740C8492184489C235E8FEEFD5F.
Previous package/shortcut backup and the original checkout's untitled.tcpart were
preserved. No outstanding owner decisions or known new limitations.

Restart from the desktop shortcut. Scroll down over an off-center model point:
it should grow while that point remains beneath the cursor. Scroll up with the
cursor near different viewport corners: the view should shrink around the same
center without panning toward the cursor. Repeat while editing a sketch.
