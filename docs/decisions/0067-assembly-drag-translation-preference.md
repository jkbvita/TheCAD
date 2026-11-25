# 0067 — Prefer translation during ordinary component dragging

Status: Requested by the owner on 2026-11-25 after testing mate-aware dragging.

Small imperfections in a left-drag cursor path should not unintentionally rotate
a component while the user slides it. The existing solver fits every visible
cursor displacement, including tiny transverse errors on a concentric mate.

Left drag now first searches for mate-valid motion with the grabbed component's
orientation held. If this leaves visible translational freedom, the solver allows
six logical pixels of residual cursor displacement before using rotation. The
allowance is consumed gradually, so crossing its edge does not cause a jump.
The viewport converts it to model units at the grab depth, independent of the
picked point's snapping offset. Repeated jitter does not accumulate rotation.

Rotation-only mechanisms retain their precise response. Explicit right-button
rotation is unchanged. The additional orientation hold is temporary; no mate or
document format is changed. Non-UI callers retain exact point targeting by
default and can supply a rotation allowance when needed.

Automated checks cover repeated wobble during axial sliding at three scales,
accurate translation, deliberate transverse rotation, precise hinge rotation,
and planar sliding without unwanted rotation. Existing rotation and history tests
remain applicable. Native viewport testing is left to the owner to avoid opening
test windows. The six-pixel value is an initial tuning choice, not a new setting.
