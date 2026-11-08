#include "modeling/Measurement.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::modeling
{
namespace
{
bool measurable(const TopoDS_Shape& shape)
{
    return !shape.IsNull() && (shape.ShapeType() == TopAbs_VERTEX ||
                              shape.ShapeType() == TopAbs_EDGE || shape.ShapeType() == TopAbs_FACE);
}

std::optional<gp_Dir> direction(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() == TopAbs_EDGE)
    {
        const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        if (curve.GetType() == GeomAbs_Line)
            return curve.Line().Direction();
    }
    else if (shape.ShapeType() == TopAbs_FACE)
    {
        const BRepAdaptor_Surface surface(TopoDS::Face(shape));
        if (surface.GetType() == GeomAbs_Plane)
            return surface.Plane().Axis().Direction();
    }
    return {};
}

std::optional<gp_Pnt> circleCenter(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() != TopAbs_EDGE)
        return {};
    const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
    if (curve.GetType() == GeomAbs_Circle)
        return curve.Circle().Location();
    return {};
}
}

std::optional<Measurement> measureGeometry(const TopoDS_Shape& first, const TopoDS_Shape& second)
{
    if (!measurable(first) || (!second.IsNull() && !measurable(second)))
        return {};
    try
    {
        Measurement result;
        if (!second.IsNull())
        {
            BRepExtrema_DistShapeShape extrema(first, second);
            if (!extrema.IsDone() || extrema.NbSolution() < 1)
                return {};
            result.distance = extrema.Value();
            const auto a = extrema.PointOnShape1(1);
            const auto b = extrema.PointOnShape2(1);
            result.span = std::pair{a, b};
            result.anchor = gp_Pnt((a.XYZ() + b.XYZ()) * 0.5);

            if (first.ShapeType() == second.ShapeType())
            {
                const auto from = direction(first);
                const auto to = direction(second);
                if (from && to)
                {
                    const double angle = from->Angle(*to);
                    // Edge orientations and face normal signs are arbitrary for this reading.
                    const double pi = std::acos(-1.0);
                    result.angleDegrees = std::min(angle, pi - angle) * 180.0 / pi;
                }
            }
            const auto fromCenter = circleCenter(first);
            const auto toCenter = circleCenter(second);
            if (fromCenter && toCenter)
                result.centerDistance = fromCenter->Distance(*toCenter);
        }
        else if (first.ShapeType() == TopAbs_VERTEX)
        {
            result.coordinates = BRep_Tool::Pnt(TopoDS::Vertex(first));
            result.anchor = result.coordinates;
        }
        else if (first.ShapeType() == TopAbs_EDGE)
        {
            const auto edge = TopoDS::Edge(first);
            GProp_GProps props;
            BRepGProp::LinearProperties(edge, props);
            result.length = props.Mass();
            const BRepAdaptor_Curve curve(edge);
            result.anchor = curve.Value((curve.FirstParameter() + curve.LastParameter()) * 0.5);
            if (curve.GetType() == GeomAbs_Circle)
                result.radius = curve.Circle().Radius();
        }
        else
        {
            const BRepAdaptor_Surface surface(TopoDS::Face(first));
            if (surface.GetType() == GeomAbs_Cylinder)
            {
                result.radius = surface.Cylinder().Radius();
                result.anchor = surface.Value((surface.FirstUParameter() + surface.LastUParameter()) * 0.5,
                                              (surface.FirstVParameter() + surface.LastVParameter()) * 0.5);
            }
        }
        return result;
    }
    catch (const Standard_Failure&)
    {
        return {};
    }
}
}
