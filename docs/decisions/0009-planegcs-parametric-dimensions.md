# 0009 — Provisional PlaneGCS and parametric sketch dimensions

Status: owner approved for integration prototype and evaluation, 2026-11-04.
Evidence: owner's attachment “TheCAD — Parametric Sketch Constraint Solver and
Smart Dimension” in this chat. This supersedes the solver proposal and earlier
milestone exclusions of dimensions/advanced relations. It does not approve a
permanent solver choice or a FreeCAD application dependency.

Integrate only PlaneGCS behind a Qt-independent adapter. TheCAD owns entities,
point references, constraint IDs, dimensions, annotation placement, history and
serialization. Failed solves and dependent regeneration retain the last valid
state. Expose degrees of freedom, conflicts, redundancy, failure and nonconvergence
with stable constraint IDs where available. Initial supported geometry is planar
lines/circles and existing rectangle records.

Approved relations: Coincident, Horizontal, Vertical, Fixed point, Parallel,
Perpendicular and supported Tangent. Approved dimensions: line length, horizontal
and vertical point distances, radius and diameter; driving/reference status,
viewport placement and double-click value edits. Preserve constraints during drag.
Unsigned H/V distances retain their existing side through a stored +/- sense;
line-circle tangency retains its side from the previous geometry; current
circle-circle tangency is external. Other tangent branches need separate scope.

Fully parametric 3D sketches are approved long-term, not implemented now.
SketchSpace distinguishes planar and future spatial data; gp_Ax3 frames and point
references contain no backend types. A future spatial entity representation,
spatial schema version and adapter can use another solver (including a separately
evaluated SolveSpace). PlaneGCS is never treated as a 3D backend. Current 2D
Point2/PointRole definitions will need explicit spatial counterparts/extensions;
the reserved enum is a seam, not a claim that 3D geometry is implemented.

The extraction is pinned to FreeCAD commit
0cafef1e58284d08a244f9eba85aee0002e3e350. Eleven upstream files retain
LGPL-2.1-or-later notices. Build adaptation is documented in
[the source audit](../../third_party/planegcs/README.TheCAD.md), with original
hashes, Eigen 3.4.0 MPL-2.0 and Boost headers 1.86.0 BSL-1.0. Use a replaceable DLL,
retain attribution/license texts, and distribute corresponding modified library
source and rebuild instructions. Eigen compiles with EIGEN_MPL2_ONLY. No new
application license is selected. This dependency audit concerns this build;
future release changes require reviewing what is actually distributed.

Conservative face attachments remain as approved in 0008: a changed supporting
BRep rejects downstream regeneration instead of guessing a face. No automatic
constraint deletion, topology repair, body naming or undo framework is introduced.
