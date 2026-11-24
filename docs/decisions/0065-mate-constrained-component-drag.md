# 0065 — Drag components through mate freedom

Status: Accepted by the owner on 2026-11-24.

## Behavior

Dragging a floating assembly component by a picked point requests that point's
screen-plane position. The mate solver finds the nearest valid component pose and
may translate or rotate the component according to its remaining degrees of
freedom. A free component translates under ordinary left drag. A component
seated on a concentric axis can rotate about that axis when its picked point is
off-axis. Fixed and fully constrained components do not move. Escape discards
the preview; release commits one undoable placement.

Mouse-path curvature no longer chooses a translation or rotation mode. The
picked point and the current cursor target are the drag inputs. Mate references
and the committed document remain authoritative throughout preview.

## Reference and scope

[SOLIDWORKS Moving Components](https://help.solidworks.com/2023/english/SolidWorks/sldworks/t_Moving_a_Component_SWassy.htm?id=7.1.7.0)
and [Rotating Components](https://help.solidworks.com/2021/english/SolidWorks/sldworks/t_Rotating_a_Component_SWassy.htm)
describe component motion within the degrees of freedom left by mates.
[Degrees of Freedom](https://help.solidworks.com/2022/english/solidworks/motionstudies/c_degrees_freedom_motion.htm)
identifies the remaining axial translation and rotation of a concentric mate.
This decision implements mate-aware left dragging; collision response and a
separate right-drag free-rotation gesture remain outside this change.
