#pragma once

#include "modeling/Sketch.h"
#include <TopoDS_Face.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <optional>

namespace thecad::modeling {

// Keep these names consistent with persisted origin-plane sketch frames.
// Front=XY (+Z), Top=XZ (-Y), Right=YZ (+X).
const char* referencePlaneName(SketchPlane plane);

// The face is a bounded selection/display patch, not a bound on sketch coordinates.
TopoDS_Face referencePlaneFace(SketchPlane plane, double halfExtent);

struct ReferencePlaneHit {
    SketchPlane plane;
    gp_Pnt point;
    double distance;
};

// Two-sided, actual face-region picking along the forward ray (parameter >= 0).
// Nearest hit wins; distances within max(kernel tolerance, halfExtent*1e-9)
// tie in the deterministic order Front, Top, Right. Labels are never hit targets.
std::optional<ReferencePlaneHit> pickReferencePlane(const gp_Lin& forwardRay, double halfExtent);

} // namespace thecad::modeling
