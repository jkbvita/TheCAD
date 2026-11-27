#include "modeling/Measurement.h"
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Circ.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace
{
using thecad::modeling::measureGeometry;

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void checkValue(std::optional<double> value, double expected, const char* message)
{
    if (!value || std::abs(*value - expected) >= 1e-7)
        throw std::runtime_error(std::string(message) + " expected " + std::to_string(expected) +
                                 ", got " + (value ? std::to_string(*value) : "unavailable"));
}

void lengthsAndRadii()
{
    const TopoDS_Shape line = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(3, 4, 0));
    const auto length = measureGeometry(line);
    require(length && !length->radius && length->anchor,
            "Expected a single straight edge reading.");
    checkValue(length->length, 5, "Straight edge length.");

    const gp_Circ first(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 3);
    const gp_Circ second(gp_Ax2(gp_Pnt(12, 0, 0), gp::DZ()), 2);
    const TopoDS_Shape ring = BRepBuilderAPI_MakeEdge(first);
    const TopoDS_Shape other = BRepBuilderAPI_MakeEdge(second);
    const auto radius = measureGeometry(ring);
    require(radius.has_value(), "Expected a circle measurement.");
    checkValue(radius->radius, 3, "Circle radius.");
    checkValue(radius->length, 6 * std::acos(-1.0), "Exact circle circumference.");
    const auto spacing = measureGeometry(ring, other);
    require(spacing.has_value(), "Expected circular edge spacing.");
    checkValue(spacing->distance, 7, "Minimum circle clearance.");
    checkValue(spacing->centerDistance, 12, "Center-to-center spacing.");
    require(spacing->centerSpan && spacing->centerSpan->first.IsEqual(first.Location(), 1e-8) &&
                spacing->centerSpan->second.IsEqual(second.Location(), 1e-8),
            "Center callout endpoints.");

    const gp_Cylinder cylinder(gp_Ax3(gp_Pnt(0, 0, 0), gp::DZ()), 4);
    const TopoDS_Shape lateral = BRepBuilderAPI_MakeFace(cylinder, 0, 2 * std::acos(-1.0), 0, 10);
    const auto cylindrical = measureGeometry(lateral);
    require(cylindrical && cylindrical->anchor, "Expected a cylindrical face callout.");
    checkValue(cylindrical->radius, 4, "Cylinder radius.");
    checkValue(cylindrical->area, 80 * std::acos(-1.0), "Cylinder face area.");
}

void distancesAndAngles()
{
    const TopoDS_Shape a = BRepBuilderAPI_MakeVertex(gp_Pnt(1, 2, 3));
    const TopoDS_Shape b = BRepBuilderAPI_MakeVertex(gp_Pnt(4, 6, 15));
    const auto point = measureGeometry(a);
    require(point && point->coordinates && point->coordinates->IsEqual(gp_Pnt(1, 2, 3), 1e-8),
            "Vertex world coordinates.");
    const auto points = measureGeometry(a, b);
    require(points && points->span && points->anchor, "Point distance needs a span and callout.");
    checkValue(points->distance, 13, "Distance between vertices.");

    const TopoDS_Shape xy = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp::DZ()), 0, 5, 0, 5);
    const TopoDS_Shape xz = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp::DY()), 0, 5, 0, 5);
    const TopoDS_Shape offset =
        BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 12), gp::DZ()), 0, 5, 0, 5);
    const auto planar = measureGeometry(xy);
    require(planar && planar->anchor, "Expected a planar face callout.");
    checkValue(planar->area, 25, "Planar face area.");
    const auto planes = measureGeometry(xy, xz);
    require(planes.has_value(), "Expected a face pair measurement.");
    checkValue(planes->angleDegrees, 90, "Angle between perpendicular planar faces.");
    checkValue(planes->distance, 0, "Touching faces should measure zero clearance.");
    const auto parallel = measureGeometry(xy, offset.Reversed());
    require(parallel.has_value(), "Expected measurements for parallel faces.");
    checkValue(parallel->distance, 12, "Clearance between trimmed parallel faces.");
    checkValue(parallel->angleDegrees, 0, "An opposed normal should still report parallel faces.");

    const TopoDS_Shape x = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(3, 0, 0));
    const TopoDS_Shape diagonal = BRepBuilderAPI_MakeEdge(gp_Pnt(4, 4, 0), gp_Pnt(0, 0, 0));
    const auto edges = measureGeometry(x, diagonal);
    require(edges.has_value(), "Expected a straight edge pair measurement.");
    checkValue(edges->angleDegrees, 45, "Smaller angle between straight edges.");
    require(!measureGeometry({}), "An empty pick has no measurement.");
}
} // namespace

void inspectionFixtures()
{
    using namespace thecad::modeling;
    const double pi = std::acos(-1.);
    auto edge = [](gp_Pnt a, gp_Pnt b) -> TopoDS_Shape { return BRepBuilderAPI_MakeEdge(a, b); };
    auto vertex = [](gp_Pnt p) -> TopoDS_Shape { return BRepBuilderAPI_MakeVertex(p); };
    const auto x = edge({0, 0, 0}, {4, 0, 0}), y = edge({4, 0, 0}, {4, 3, 0}),
               z = edge({4, 3, 0}, {4, 3, 12});
    checkValue(measureGeometry(vertex({2, 3, 0}), x)->distance, 3, "Point to trimmed line.");
    checkValue(measureEntities({{x}, {y}, {z}})->totalLength, 19, "Three-edge length.");
    require(!measureEntities({{x}, {y}, {vertex({0, 0, 0})}}), "Unsupported mixed aggregate.");
    const gp_Circ ring(gp_Ax2({0, 0, 0}, gp::DZ()), 4);
    auto arc = measureGeometry(BRepBuilderAPI_MakeEdge(ring, 0, pi / 2));
    checkValue(arc->length, 2 * pi, "Arc length.");
    checkValue(arc->sweepDegrees, 90, "Arc sweep.");
    checkValue(arc->radius, 4, "Arc radius.");
    require(arc->center->IsEqual(gp::Origin(), 1e-8), "Arc center.");
    const auto pair =
        measureGeometry(BRepBuilderAPI_MakeEdge(ring),
                        BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2({10, 0, 0}, gp::DZ()), 2)));
    const auto farthest = circularSpan(*pair, ArcCondition::Far, ArcCondition::Far);
    checkValue(farthest->first.Distance(farthest->second), 16, "Two ring maximum.");
    const auto custom = circularSpan(*pair, ArcCondition::Near, ArcCondition::Center);
    checkValue(custom->first.Distance(custom->second), 6, "Mixed arc conditions.");
    auto trimmed = measureGeometry(BRepBuilderAPI_MakeEdge(ring, 0, pi / 2), vertex({8, 0, 0}));
    auto pointArc = circularSpan(*trimmed, ArcCondition::Far, ArcCondition::Far);
    require(pointArc.has_value(), "Point to arc maximum available.");
    checkValue(pointArc->first.Distance(pointArc->second), std::sqrt(80.), "Trimmed arc maximum.");
    const MeasureSpan span{{10, 5, 1}, {13, 9, 13}};
    const auto d = measureComponents(span, gp_Ax3());
    checkValue(d.X(), 3, "dX");
    checkValue(d.Y(), 4, "dY");
    checkValue(d.Z(), 12, "dZ");
    const gp_Ax3 frame({10, 5, 1}, gp::DZ(), gp::DY());
    const auto coords = measureCoordinates(span.second, frame);
    checkValue(coords.X(), 4, "Relative X");
    checkValue(coords.Y(), -3, "Relative Y");
    checkValue(coords.Z(), 12, "Relative Z");
    const auto projected = projectMeasurement(span, gp_Pln({0, 0, -4}, gp::DZ()));
    checkValue(projected.inPlane.first.Distance(projected.inPlane.second), 5,
               "Projected distance.");
    checkValue(projected.normal.first.Distance(projected.normal.second), 12, "Normal component.");
    const MeasureEntity plane{{}, gp_Pln({0, 0, 20}, gp::DZ())},
        axis{{}, {}, gp_Lin({100, 0, 0}, gp::DZ())};
    checkValue(measureEntities({plane, axis})->distance, 0, "Infinite axis pierces datum.");
    checkValue(measureEntities({plane, {{}, gp_Pln({800, 900, 23}, gp::DZ())}})->distance, 3,
               "Unbounded parallel planes.");
    checkValue(measureEntities({plane, {vertex({1e5, 150, 13})}})->distance, 7,
               "Far outside displayed plane.");
    gp_Cylinder c1(gp_Ax3({0, 0, 0}, gp::DZ()), 4), c2(gp_Ax3({10, 0, 0}, gp::DZ()), 2);
    auto cylinders = measureGeometry(BRepBuilderAPI_MakeFace(c1, 0, 2 * pi, 0, 5),
                                     BRepBuilderAPI_MakeFace(c2, 0, 2 * pi, 0, 5));
    checkValue(cylinders->distance, 4, "Cylindrical face clearance.");
    checkValue(cylinders->centerDistance, 10, "Cylinder axis spacing.");
    auto radial = circularSpan(*cylinders, ArcCondition::Far, ArcCondition::Far);
    require(radial.has_value(), "Overlapping cylinder sections.");
    checkValue(radial->first.Distance(radial->second), 16, "Cylinder maximum radial span.");
    const auto box = BRepPrimAPI_MakeBox(10, 8, 5).Shape();
    checkValue(measureGeometry(box)->area, 340, "Body surface area.");
    checkValue(projectedMeasureArea(box, gp_Pln(gp::XOY())), 80, "Projected body silhouette.");
    const auto hole = BRepPrimAPI_MakeCylinder(gp_Ax2({5, 4, -1}, gp::DZ()), 1., 7.).Shape();
    checkValue(projectedMeasureArea(BRepAlgoAPI_Cut(box, hole).Shape(), gp_Pln(gp::XOY())), 80 - pi,
               "Through-hole in projection.");
    BRep_Builder builder;
    TopoDS_Edge degenerate;
    builder.MakeEdge(degenerate);
    builder.Degenerated(degenerate, true);
    require(!measureGeometry(degenerate), "Degenerate edge.");
}

// Optional owner-supplied STEP counterpart of the part inspected in SolidWorks.
// Match both geometry and measured properties, independent of STEP face order.
void compareReferencePlate(const char* path)
{
    STEPControl_Reader reader;
    require(reader.ReadFile(path) == IFSelect_RetDone, "Cannot load the reference plate.");
    reader.SetSystemLengthUnit(1.);
    require(reader.TransferRoots() > 0, "The STEP plate has no transferable roots.");
    const auto plate = reader.OneShape();
    std::vector<TopoDS_Shape> lower, upper;
    // STEP translation and the reference application's integration can differ at
    // the last displayed digit. The analytic fixtures above use 1e-7 instead.
    auto matches = [](std::optional<double> value, double displayed)
    { return value && std::abs(*value - displayed) <= 0.01; };
    for (TopExp_Explorer face(plate, TopAbs_FACE); face.More(); face.Next())
    {
        const auto r = measureGeometry(face.Current());
        if (!r || !r->planarFace)
            continue;
        if (matches(r->area, 883.86) && matches(r->perimeter, 200.87))
            lower.push_back(face.Current());
        if (matches(r->area, 1068.20) && matches(r->perimeter, 137.98))
            upper.push_back(face.Current());
    }
    for (const auto& a : lower)
        for (const auto& b : upper)
        {
            const auto r = measureGeometry(a, b);
            if (r && matches(r->distance, 7.28) && matches(r->totalArea, 1952.06) &&
                matches(r->angleDegrees, 0))
            {
                const auto f = *measureGeometry(a), g = *measureGeometry(b);
                std::cout << std::setprecision(11) << "Face areas (mm^2) " << *f.area << ", "
                          << *g.area << " | perimeters (mm) " << *f.perimeter << ", "
                          << *g.perimeter << " | separation (mm) " << *r->distance
                          << " | total area (mm^2) " << *r->totalArea << '\n';
                return;
            }
        }
    throw std::runtime_error("STEP counterpart did not reproduce the recorded two-face reading.");
}

int main(int argc, char** argv)
{
    try
    {
        lengthsAndRadii();
        distancesAndAngles();
        inspectionFixtures();
        if (argc == 2)
            compareReferencePlate(argv[1]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
