#include "modeling/ReferencePlanes.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace thecad::modeling {
namespace {
void validateExtent(double halfExtent)
{
    if (!std::isfinite(halfExtent) || halfExtent <= Precision::Confusion() || halfExtent > 1e9) {
        throw std::runtime_error("Reference-plane display size must be finite, positive, and no larger than 1e9 mm.");
    }
}

void validateRay(const gp_Lin& ray)
{
    const auto& location = ray.Location();
    const auto& direction = ray.Direction();
    for (const double value : {location.X(), location.Y(), location.Z(),
                              direction.X(), direction.Y(), direction.Z()}) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("Reference-plane picking requires a finite 3D ray.");
        }
    }
}
}

const char* referencePlaneName(SketchPlane plane)
{
    switch (plane) {
    case SketchPlane::XY: return "Front";
    case SketchPlane::XZ: return "Top";
    case SketchPlane::YZ: return "Right";
    }
    throw std::runtime_error("Unsupported origin reference plane.");
}

TopoDS_Face referencePlaneFace(SketchPlane plane, double halfExtent)
{
    validateExtent(halfExtent);
    try {
        BRepBuilderAPI_MakeFace face(gp_Pln(sketchAxes(plane)),
                                    -halfExtent, halfExtent, -halfExtent, halfExtent);
        if (!face.IsDone()) {
            throw std::runtime_error("Cannot construct a reference-plane display patch.");
        }
        return face.Face();
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("Cannot construct reference plane: ") + error.GetMessageString());
    }
}

std::optional<ReferencePlaneHit> pickReferencePlane(const gp_Lin& forwardRay, double halfExtent)
{
    validateExtent(halfExtent);
    validateRay(forwardRay);
    const double tolerance = std::max(Precision::Confusion(), halfExtent * 1e-9);
    std::optional<ReferencePlaneHit> closest;
    try {
        for (const auto plane : {SketchPlane::XY, SketchPlane::XZ, SketchPlane::YZ}) {
            const auto axes = sketchAxes(plane);
            const gp_Vec normal(axes.Direction());
            const gp_Vec direction(forwardRay.Direction());
            const double denominator = direction.Dot(normal);
            if (std::abs(denominator) <= 1e-12) {
                continue; // Edge-on planes have no unique intersection with this ray.
            }
            const double distance = gp_Vec(forwardRay.Location(), axes.Location()).Dot(normal) / denominator;
            if (!std::isfinite(distance) || distance < -tolerance) {
                continue;
            }
            const gp_Pnt point = forwardRay.Location().Translated(direction * std::max(0.0, distance));
            const auto face = referencePlaneFace(plane, halfExtent);
            BRepClass_FaceClassifier classifier(face, point, tolerance);
            if (classifier.State() != TopAbs_IN && classifier.State() != TopAbs_ON) {
                continue;
            }
            const double forwardDistance = std::max(0.0, distance);
            if (!closest || forwardDistance < closest->distance - tolerance) {
                closest = ReferencePlaneHit{plane, point, forwardDistance};
            }
        }
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("Cannot pick reference plane: ") + error.GetMessageString());
    }
    return closest;
}

} // namespace thecad::modeling
