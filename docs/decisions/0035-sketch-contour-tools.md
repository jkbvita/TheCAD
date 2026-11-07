# 0035 — Sketch Trim, Extend, Split and Offset

The owner approved all four sketch commands. Baseline `8811b89` and feature
branch `codex/sketch-trim-extend-split-offset`.

## Interaction

- **Trim Entities** uses Trim to Closest. Hover over a straight segment to
  highlight the portion between its neighboring intersections or endpoints.
  Click to remove that portion. An interior trim retains both outer pieces.
- **Extend Entities** previews the nearest intersection beyond the half of
  the line under the pointer. Click again to reach a further intersection.
  Trim and Extend use finite sketch lines and full circles as boundaries.
- **Split Entities** places a joint at the picked point on a straight line.
  Both segments are retained, joined and kept parallel. The first retains the
  original ID and the old end's point references follow the second segment.
  A driving length transfers to a distance between the two outer endpoints.
- **Offset Entities** accepts a full circle, single line, or one connected open
  or closed line chain with distinct corners. It offers distance, reverse,
  both sides, select chain and construction geometry. Closed loops offset
  outward by default. Line joins are mitered and open ends stay aligned with
  the corresponding source ends. Offsets remain associated to their sources.

Line edits preview on hover, commit on click and stay active for another edit.
Escape or Done closes the tool. Undo and redo remain available and close the
active line tool. Offset uses a preview and explicit confirmation. Every accepted
edit is one history entry and viewport picking preserves the camera.

## Reference policy

The line tools retain existing dimensions and relations where the result still
satisfies them. Conflicts, protected linked geometry and edits that invalidate
a downstream feature reject transactionally. New trim and extend ends carry
point-on-boundary relations. Trim, Extend and Split support straight sketch
segments in this batch. Arc editing and other Trim modes remain separate work.

The sketch adapter expresses offsets using existing PlaneGCS equations.
Invalid, self-crossing, collapsed and duplicate offsets reject before commit.
Offset parameters are entered at creation. Deleting a member releases the
remaining geometry of its offset group. Schema 21 stores the offset groups.
No new dependencies are required.

## SolidWorks workflow references

- [Trim to Closest](https://help.solidworks.com/2024/english/Solidworks/sldworks/t_Trimming_with_Trim_to_Closest.htm)
- [Extend Entities](https://help.solidworks.com/2025/english/SolidWorks/sldworks/t_extend_entities.htm)
- [Split Entities](https://help.solidworks.com/2025/English/SolidWorks/sldworks/c_split_entities.htm)
- [Offset Entities PropertyManager](https://help.solidworks.com/2025/english/Solidworks/sldworks/HIDD_OFFSET1.htm)
