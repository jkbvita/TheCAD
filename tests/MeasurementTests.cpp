#include "modeling/Measurement.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <cmath>
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
    require(value && std::abs(*value - expected) < 1e-7, message);
}

void lengthsAndRadii()
{
    const TopoDS_Shape line = BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(3, 4, 0));
    const auto length = measureGeometry(line);
    require(length && !length->radius && length->anchor, "Expected a single straight edge reading.");
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

    const gp_Cylinder cylinder(gp_Ax3(gp_Pnt(0, 0, 0), gp::DZ()), 4);
    const TopoDS_Shape lateral = BRepBuilderAPI_MakeFace(cylinder, 0, 2 * std::acos(-1.0), 0, 10);
    const auto cylindrical = measureGeometry(lateral);
    require(cylindrical && cylindrical->anchor, "Expected a cylindrical face callout.");
    checkValue(cylindrical->radius, 4, "Cylinder radius.");
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
    const TopoDS_Shape offset = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 12), gp::DZ()), 0, 5, 0, 5);
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
}

int main()
{
    try
    {
        lengthsAndRadii();
        distancesAndAngles();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
