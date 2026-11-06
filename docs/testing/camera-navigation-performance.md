# Camera navigation performance

Work branch: `codex/camera-navigation-performance`, based on accepted commit
`9048234`. No solver, modeling geometry, document schema or visual-quality setting
changes. The existing navigation gestures and selection colors are retained.

## Changes

- Qt schedules redraws. OCCT immediate camera redraws are disabled, so a pan does
  not render synchronously from each input event as well as the Qt paint event.
- Camera input updates the camera and marks annotations dirty. Pending input can
  coalesce into one annotation update per paint rather than rebuild at mouse rate.
- Orbit and pan reuse dimension lines, arrows and labels already attached to the
  sketch plane. Zoom refreshes pixel-sized dimension graphics once per paint.
- Existing relation sprites move using cached anchor/offset data during navigation.
  Their full collision-avoidance layout runs when navigation ends or loses its grab.
  Newly visible relation sprites and final overlap avoidance refresh at that point.
- Remove the duplicate relation-layout call on every camera move.
- Orthographic cursor zoom needs no body-depth picking. Perspective drag zoom picks
  its depth once at gesture start; perspective wheel zoom still picks at the cursor.

## Measurement

Release build, 24 independent horizontal lines, 24 relation symbols and 8 dimensions.
A synchronous burst of 40 orbit mouse events (no intervening paint) measured:

| Input processing | Accepted implementation | Optimized implementation |
| --- | ---: | ---: |
| Time for 40 moves | 326,505 microseconds | 84 microseconds |
| Events replacing relation annotations | 40 | 0 |

These are input-handler CPU timings, not an FPS measurement or a guarantee for
large models/GPU throughput. Rendering, paint-time annotation updates and the final
collision layout are deliberately outside this isolated measurement. Antialiasing,
transparency and model display quality have not been reduced.

## Manual checks

1. Restart TheCAD through the updated desktop shortcut, open the same part, and
   compare middle-drag orbit, Ctrl-middle pan, Shift-middle zoom and wheel zoom.
2. Try both a plain solid and a sketch with dimensions and many green relations.
   Annotations should follow the sketch while moving, with final relation layout
   settling when the drag ends. Continue selecting/drawing after navigation.
3. Check cursor-directed zoom and picked rotation pivots. Repeat at 150% scaling.
4. Orbit the sketch edge-on, return using Ctrl+8, then pan/zoom and edit a dimension.
   No geometry, driving values or constraints should change just from navigation.

## Verification — 2026-11-06

- Windows Release build succeeded with the existing MSVC/Qt/OCCT dependencies.
- All 36 CTest targets passed: engine and existing GUI coverage in the full run,
  then both navigation variants after correcting the synthetic lost-grab event
  in the test fixture. No production changes followed that full run.
- New checks cover annotation reuse across input bursts and orbit/pan frames,
  zoom-arrow refresh, relation hit positions, interrupted gestures, unchanged
  sketch data, orthographic/perspective cursor zoom and 150% display scaling.
- Packaged saved-file launch passed with a system-only PATH and bundled runtime.
- Runtime dependency verification passed; no new dependencies were introduced.
- Desktop shortcut targets this worktree's
  `build/package/TheCAD-Camera-Performance/TheCAD.exe`. The previous package and
  shortcut backup remain available; owner windows were not closed.
- Built/package executable SHA-256:
  `ECF3D42A25CE04076BA4823AFB34FD199AD1B81AA1873100C07AFAA3437CE1AC`.
- Original checkout and its untracked `untitled.tcpart` remain untouched.
