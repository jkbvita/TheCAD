# 0016 — Circle dragging, Equal callouts and local build reconciliation

Status: owner-approved repair scope, completed 2026-11-05. The owner requested
always-visible circle centers, outline dragging, one Equal icon per affected
entity and a desktop shortcut opening the combined implementation.

## Accepted work from concurrent chats

The main checkout contained accepted commit bd71e05 (clean feature tree/fixed
hexagons), while the isolated relations worktree contained db4f515. The desktop
shortcut targeted the main checkout's package and therefore missed relations.
The repair branch `codex/circle-drag-equal-icons` includes both sets of changes,
with conflict resolution retaining contextual construction conversion and the
fixed-hexagon interface. No main branch or pull request is merged. The original
checkout and its untracked untitled.tcpart are untouched.

## Circle interaction

Every circle center has a filled sketch marker during sketch editing, independent
of hover or selection. In Select mode an outline press uses the existing drag
transaction with a cursor-to-center offset, so the circle does not jump to the
cursor. Center snapping uses the translated center rather than the outline grab
position. Shift/Ctrl selection and Smart Dimension picking retain their modes.

Outline dragging preserves the circle radius. Its transient solver target is
omitted when an existing radius dimension or Fix already supplies it. If other
constraints indirectly determine the radius, the adapter can retry without only
the temporary target and validates that the radius did not change. Persisted
constraints and driving dimensions are never removed or rewritten. Fixed circles
remain fixed; tangent circles can slide where the remaining degrees of freedom
permit. Drag preview, Escape cancellation and one-action undo/redo reuse the
existing point-drag transaction.

## Equal command groups

One Equal or Equal Curve Length command stores a shared group ID on its pairwise
PlaneGCS equations. A group of N entities still needs N-1 equations, but each
distinct entity gets one callout for that group. A two-entity relation therefore
shows two callouts, and a three-entity batch shows three, with no duplicate on the
shared reference entity. Independent commands remain separate groups.

All callouts of a group select the same logical relation and highlight every
member. Deletion removes the group's equations together in one history action.
The inspector lists one group, including when filtering by a nonreference member.
Compatible Equal/Equal Curve Length changes apply to the complete group. Changing
a multiple-member group to another kind requires deleting it and applying the
desired relation to the selection; existing binary relation replacement remains.

Schema 10 adds optional `group` metadata. Readers retain schemas 1–9. The original
schema-9 UI generated IDs as UUID-1, UUID-2, etc.; that exact shape recovers the
batch identity on import. Arbitrary legacy pair IDs are not merged heuristically.
Group IDs/types/shared references are validated and included in history keys.
There are no new dependencies or license changes, and upstream PlaneGCS is intact.

## Deployment and verification

The repair package is generated in
`build/relations-worktree/build/package/TheCAD-Relations-Repair` under the original
repository. This avoids replacing the older application while an unsaved sketch
may be open. Desktop `TheCAD.lnk` targets this package directly; its prior contents
are backed up in the isolated worktree's `build/desktop-shortcut-backup` directory.
The packaged executable SHA256 matches the tested Release executable. Packaging
also verifies all 50 imported runtime DLLs and retains existing license notices.

Release compilation succeeded. All 22 CTest cases passed (46.65 seconds), including
circle-center visibility, live outline movement, radius preservation, cancellation,
undo/redo, per-member grouped icons, independent groups, group replacement,
persistence/legacy migration and malformed groups. Normal and high-DPI workflows
also retain the feature-tree/hexagon changes and existing modeling behavior.

The owner should save any open sketch, close the older running application and
reopen it from the repaired desktop shortcut before visual testing.
