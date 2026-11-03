#include "modeling/ReferencePlanes.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace thecad::modeling;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void requireNear(double value, double expected)
{
    require(std::abs(value - expected) < 1e-6, "Plane geometry or ray distance is incorrect.");
}
void same(const gp_Vec& value, const gp_Vec& expected)
{
    require((value - expected).Magnitude() < 1e-10, "Origin-plane frame mapping changed.");
}
void rejected(const std::function<void()>& action)
{
    bool didReject = false;
    try { action(); }
    catch (const std::runtime_error& error) { didReject = error.what()[0] != '\0'; }
    require(didReject, "Invalid reference-plane input must be rejected clearly.");
}
void hit(const gp_Pnt& origin, const gp_Dir& direction, SketchPlane expected, const gp_Pnt& point)
{
    const auto result = pickReferencePlane(gp_Lin(origin, direction), 25.0);
    require(result.has_value(), "Expected a geometric plane hit.");
    require(result->plane == expected, "The nearest reference-plane hit is incorrect.");
    require(result->point.Distance(point) < 1e-6, "Reference-plane hit point is incorrect.");
    requireNear(result->distance, origin.Distance(point));
}
}

int main()
{
    try {
        require(std::string(referencePlaneName(SketchPlane::XY)) == "Front", "XY must be Front.");
        require(std::string(referencePlaneName(SketchPlane::XZ)) == "Top", "XZ must be Top.");
        require(std::string(referencePlaneName(SketchPlane::YZ)) == "Right", "YZ must be Right.");
        for (const auto plane : {SketchPlane::XY, SketchPlane::XZ, SketchPlane::YZ}) {
            const auto axes = sketchAxes(plane);
            require(axes.Location().Distance(gp_Pnt(0, 0, 0)) < 1e-12, "Reference planes must share the model origin.");
            same(gp_Vec(axes.XDirection()).Crossed(gp_Vec(axes.YDirection())), gp_Vec(axes.Direction()));
            if (plane == SketchPlane::XY) {
                same(gp_Vec(axes.XDirection()), gp_Vec(1, 0, 0));
                same(gp_Vec(axes.YDirection()), gp_Vec(0, 1, 0));
                same(gp_Vec(axes.Direction()), gp_Vec(0, 0, 1));
            } else if (plane == SketchPlane::XZ) {
                same(gp_Vec(axes.XDirection()), gp_Vec(1, 0, 0));
                same(gp_Vec(axes.YDirection()), gp_Vec(0, 0, 1));
                same(gp_Vec(axes.Direction()), gp_Vec(0, -1, 0));
            } else {
                same(gp_Vec(axes.XDirection()), gp_Vec(0, 1, 0));
                same(gp_Vec(axes.YDirection()), gp_Vec(0, 0, 1));
                same(gp_Vec(axes.Direction()), gp_Vec(1, 0, 0));
            }
            const auto face = referencePlaneFace(plane, 25.0);
            require(!face.IsNull() && BRepCheck_Analyzer(face).IsValid(), "Reference patches must be valid BRep faces.");
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(face, properties);
            requireNear(properties.Mass(), 2500.0);
            // The finite face is only a display/picking aid, not the actual sketch domain.
            const auto outside = sketchPoint(plane, {2500.0, -3000.0});
            const gp_Vec local(axes.Location(), outside);
            requireNear(local.Dot(gp_Vec(axes.XDirection())), 2500.0);
            requireNear(local.Dot(gp_Vec(axes.YDirection())), -3000.0);
        }

        hit({10, 15, 80}, {0, 0, -1}, SketchPlane::XY, {10, 15, 0});
        hit({10, -80, 15}, {0, 1, 0}, SketchPlane::XZ, {10, 0, 15});
        hit({80, 10, 15}, {-1, 0, 0}, SketchPlane::YZ, {0, 10, 15});
        // Both sides are selectable after orbiting, independent of face normal sign.
        hit({10, 15, -80}, {0, 0, 1}, SketchPlane::XY, {10, 15, 0});
        hit({10, 80, 15}, {0, -1, 0}, SketchPlane::XZ, {10, 0, 15});
        hit({-80, 10, 15}, {1, 0, 0}, SketchPlane::YZ, {0, 10, 15});

        // Several patches overlap in projection; depth resolves the nearest actual region.
        hit({30, 40, 50}, {-1, -1, -1}, SketchPlane::YZ, {0, 10, 20});
        hit({-30, -40, -50}, {1, 1, 1}, SketchPlane::YZ, {0, -10, -20});
        // All three intersect at the origin; tie order is fixed Front, Top, Right.
        hit({30, 30, 30}, {-1, -1, -1}, SketchPlane::XY, {0, 0, 0});
        // Front is parallel here; the exact Top/Right tie chooses Top.
        hit({30, 30, 10}, {-1, -1, 0}, SketchPlane::XZ, {0, 0, 10});

        // Arbitrary oblique ray through an interior Front point, without a competing patch.
        hit({5, 8, 80}, {0.05, 0.04, -1}, SketchPlane::XY, {9, 11.2, 0});
        require(!pickReferencePlane(gp_Lin({40, 40, 80}, {0, 0, -1}), 25.0), "Outside display patches must not be selected.");
        require(!pickReferencePlane(gp_Lin({10, 15, 80}, {0, 0, 1}), 25.0), "Planes behind the ray must not be selected.");
        require(!pickReferencePlane(gp_Lin({0, 30, 30}, {1, 0, 0}), 25.0), "A parallel or out-of-bounds ray must not select a patch.");
        hit({25, 10, 80}, {0, 0, -1}, SketchPlane::XY, {25, 10, 0});
        require(!pickReferencePlane(gp_Lin({25.001, 10, 80}, {0, 0, -1}), 25.0), "A point beyond an actual face edge must not hit.");

        for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN(), 1e10}) {
            rejected([&] { (void)referencePlaneFace(SketchPlane::XY, invalid); });
            rejected([&] { (void)pickReferencePlane(gp_Lin({0, 0, 80}, {0, 0, -1}), invalid); });
        }
        rejected([] { (void)referencePlaneName(static_cast<SketchPlane>(99)); });
        rejected([] { (void)referencePlaneFace(static_cast<SketchPlane>(99), 25); });
        rejected([] { (void)pickReferencePlane(gp_Lin(gp_Pnt(std::numeric_limits<double>::quiet_NaN(), 0, 80), gp_Dir(0, 0, -1)), 25); });
        std::cout << "PASS: origin-plane names/frames, BRep display faces, geometric rotated/two-sided ray picking, overlap ordering, bounds, and invalid inputs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
