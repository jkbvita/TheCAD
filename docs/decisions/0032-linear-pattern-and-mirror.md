# 0032 — Linear Pattern and Mirror

The owner selected Linear Pattern and Mirror on 2026-11-07. Baseline `185208e`
and feature branch `codex/linear-pattern-mirror`.

## Initial scope

- Linear Pattern copies along one straight direction. Select a straight solid
  edge, sketch line or reference axis. Set spacing and count, including the
  original, and reverse the direction when needed.
- Mirror reflects across an origin or custom plane or a named planar face on
  the preceding solid.
- Select earlier solid features in the FeatureManager or PropertyManager.
  Feature seeds contribute the solid material they added and removed in
  history. The copies use that geometry at its original depth and shape.
- All solid bodies uses the entire preceding solid result as a single seed.
  Copies fuse where they meet and remain separate bodies where disjoint.
- The docked PropertyManager previews added and removed material, validates
  before confirmation and supports cancellation and later editing. Each
  confirmation is one undoable document edit. Reference selection leaves the
  camera in place.

## Regeneration and storage

The document retains the preceding solid, the source direction or plane, and
seed feature IDs in history order. The modeling module isolates material with
OCCT Boolean differences, transforms it and combines it with the current
solid. Instance and seed identities contribute to persistent face names.
Regeneration rejects invalid solids, missing or ambiguous references and
placements that produce no material change. The limit is 100 transformed
copies per feature, with at most 100 instances including the linear seed.

Parts using either command write schema 18. Previous file versions remain
readable. No new dependencies are required.

## Limits

The initial scope excludes a second pattern direction, skipped instances,
individual body or face seeds, and reevaluating end conditions at each instance.
Downstream features can block edits that remove or split their named support.

## SolidWorks workflow references

- [Linear Patterns and the PropertyManager](https://help.solidworks.com/2021/english/SolidWorks/sldworks/t_Linear_Patterns_Overview.htm)
- [Mirror Feature PropertyManager](https://help.solidworks.com/2023/english/SolidWorks/sldworks/HIDD_DVE_FEAT_MIRROR.htm?id=5ec36568f6854c1c970a5ec139a446ce)
- [Patterns and Mirroring](https://help.solidworks.com/2025/english/SolidWorks/sldworks/c_Patterns_and_Mirroring_top.htm?id=89c228f3f031449898ffa44e0d978ef2)
