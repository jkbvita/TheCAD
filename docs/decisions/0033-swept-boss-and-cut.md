# 0033 — Swept Boss/Base and Swept Cut

The owner selected this batch on 2026-11-07. Baseline `c0f43f6` and feature
branch `codex/swept-boss-and-cut`.

## Initial workflow

- Draw a closed profile on one sketch and an open chain of connected lines on a
  separate sketch. Exactly one path endpoint lies on the profile plane, and its
  first segment leaves the plane. Sketch points and construction geometry are
  ignored when assembling the path.
- Start either sweep from Features. Choose the profile sketch and the path in
  the PropertyManager or FeatureManager. Pick the region within the profile or
  the path in the viewport using their respective pick controls. A sketch with
  one closed region selects it automatically.
- The sweep follows the path, using mitered bends. Regions may contain holes.
  Boss/Base adds material and joins intersecting bodies. Cut requires an
  existing solid and removes material along the path.
- Preview and edit the selection before confirmation. Selection preserves the
  camera. Confirmation commits a single undoable operation. Both sketches remain
  editable from the sweep feature in the tree.

## Implementation and limits

The document stores both sketch IDs and a region's stable boundary references
in schema 19. It regenerates transactionally. The modeling module builds a pipe
shell using the existing OCCT SDK's TKOffset toolkit, caps the shell, and uses
non-destructive Booleans to hollow, fuse, or subtract material. OCCT's generated
topology identifies caps and side faces. A split or ambiguous face requires
explicit reselection. TKOffset is covered by the pinned OCCT 7.9.3 LGPL 2.1
license and OCCT exception. No external package or solver change is required.

The initial scope is a single sketch region and one open line chain. Closed,
curved and model-edge paths, guide curves, twist controls, and solid tool bodies
are outside this batch. Invalid or self-crossing paths, self-intersecting tools,
and failed or material-neutral operations leave the part intact.

## SolidWorks workflow references

- [Creating a Basic Sweep](https://help.solidworks.com/2023/english/SolidWorks/sldworks/t_Creating_a_Sweep.htm)
- [Swept Boss/Base PropertyManager](https://help.solidworks.com/2022/english/SolidWorks/sldworks/r_swept_boss_base_property_manager.htm)
- [Swept Cut PropertyManager](https://help.solidworks.com/2020/English/SolidWorks/sldworks/r_swept_cut_property_manager.htm)
