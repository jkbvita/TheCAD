# Sketch relations popup verification

Branch: `codex/sketch-relations-popup`. Package in the isolated worktree:
`build/package/TheCAD-Relations-Popup/TheCAD.exe`.
Save/close older copies and reopen the desktop TheCAD shortcut for this build.

## Automated checks

Release build and the full 26-case CTest suite pass. The modeling workflow runs
at normal and 150% scaling. It checks real Shift selection and key release,
type-appropriate commands, positioning at the cursor and viewport boundaries,
moving onto a relation button without dismissal, one-click application and closing,
normal/side-panel key release, preservation of keyboard focus, native-child overlay
ownership, auto-repeat filtering, repeated use, empty/no-op selection, outside-click
dismissal, sketch-exit cancellation, Undo/Redo and stable relation IDs.
Existing circle/construction/context-conflict, relation editing/deletion, feature
preview and document persistence cases remain passing. No repeated screenshot
inspection was used; the owner performs primary native visual testing.

## Manual owner checks

1. Edit a sketch containing two oblique lines. Hold Shift and click each line's
   body. Both should turn light blue without moving or starting a new line.
   Release Shift: the relation bar should appear beside the cursor.
2. Move onto the bar and hover icons to read their names. Click Parallel or Equal.
   The relation should apply immediately, show its green symbols, and close the
   bar. Ctrl+Z should undo it; Ctrl+Y should restore it.
3. Make another selection and repeat. Try circles: circle-specific options should
   replace line-only choices. Try construction lines and points as well.
4. While holding Shift after selection, move focus to a sketch side-panel control
   before releasing Shift. The bar should still appear. Showing it should not steal
   keyboard focus or activate a separate window.
5. Press Esc, click outside the bar or move well away. It should disappear without
   applying anything. Shift release with no new selection should not reopen it.
6. Repeat near viewport edges and at your usual Windows scaling. The bar must stay
   visible above the sketch and fit within the viewport. Resize while it is open:
   it should dismiss instead of remaining at a stale cursor position.

Limitations: choices are filtered by supported entity types, while the solver still
rejects conflicting or redundant combinations. This implements the owner's one-shot
Shift workflow; the separate persistent right-click variant is not included.
No additional owner behavior decisions are required for this task.
