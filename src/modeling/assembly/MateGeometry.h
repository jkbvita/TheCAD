#pragma once
#include "modeling/ReferenceGeometry.h"
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS_Shape.hxx>
#include <memory>
namespace thecad::modeling::assembly
{
enum class MateGeometryKind
{
    Point,
    Line,
    Plane,
    Circle,
    Cylinder,
    Cone,
    Sphere,
    Curve,
    Surface,
    Frame
};
enum class MateProfileKind
{
    None,
    Circle,
    Rectangle,
    RegularPolygon,
    Slot
};
struct MateCurveSegment
{
    Handle(Geom_Curve) curve;
    double first = 0, last = 1, length = 0;
    bool reversed = false;
};
struct MatePath
{
    std::vector<MateCurveSegment> segments;
    double length = 0;
    bool closed = false;
    gp_Pnt point(double fraction) const;
    gp_Dir tangent(double fraction) const;
};
struct MateGeometry
{
    MateGeometryKind kind = MateGeometryKind::Point;
    gp_Ax3 frame;
    double radius = 0, semiAngle = 0;
    TopoDS_Shape shape;
    Handle(Geom_Curve) curve;
    double first = 0, last = 1;
    Handle(Geom_Surface) surface;
    double u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    bool reversed = false;
    MateProfileKind profile = MateProfileKind::None;
    gp_Ax3 profileFrame;
    double profileWidth = 0, profileHeight = 0;
    int profileSides = 0;
    std::shared_ptr<const MatePath> path;
};
MateGeometry mateGeometry(const TopoDS_Shape&);
MateGeometry mateGeometry(const DatumGeometry&, bool coordinateFrame = false);
std::shared_ptr<const MatePath> matePath(const std::vector<MateGeometry>& chain);
std::shared_ptr<const MatePath> camPath(const std::vector<MateGeometry>& faces,
                                        gp_Dir& extrusionDirection);
bool mateGeometryDirection(const MateGeometry&);
const char* mateGeometryName(MateGeometryKind);
} // namespace thecad::modeling::assembly
