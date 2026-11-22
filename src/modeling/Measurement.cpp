#include "modeling/Measurement.h"
#include "modeling/MeasurementCircular.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IntAna_QuadQuadGeo.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::modeling
{
namespace
{
const double pi = std::acos(-1.);
gp_Pnt middle(const MeasureSpan& span)
{
    return gp_Pnt((span.first.XYZ() + span.second.XYZ()) * .5);
}
gp_Pnt onPlane(const gp_Pnt& p, const gp_Pln& plane)
{
    const gp_Vec n(plane.Axis().Direction());
    return p.Translated(-n * gp_Vec(plane.Location(), p).Dot(n));
}
gp_Pnt onLine(const gp_Pnt& p, const gp_Lin& line)
{
    const gp_Vec d(line.Direction());
    return line.Location().Translated(d * gp_Vec(line.Location(), p).Dot(d));
}
MeasureSpan axesSpan(const gp_Lin& a, const gp_Lin& b)
{
    const gp_Vec u(a.Direction()), v(b.Direction()), w(b.Location(), a.Location());
    const auto cross = u.Crossed(v);
    const auto p =
        cross.SquareMagnitude() <= 1e-24
            ? a.Location()
            : a.Location().Translated(u * (-w.Crossed(v).Dot(cross) / cross.SquareMagnitude()));
    return {p, onLine(p, b)};
}
std::optional<gp_Pln> planeOf(const MeasureEntity& e)
{
    if (e.plane)
        return e.plane;
    if (!e.shape.IsNull() && e.shape.ShapeType() == TopAbs_FACE)
    {
        BRepAdaptor_Surface s(TopoDS::Face(e.shape));
        if (s.GetType() == GeomAbs_Plane)
            return s.Plane();
    }
    return {};
}
std::optional<gp_Lin> lineOf(const MeasureEntity& e)
{
    if (e.axis)
        return e.axis;
    if (!e.shape.IsNull() && e.shape.ShapeType() == TopAbs_EDGE)
    {
        BRepAdaptor_Curve c(TopoDS::Edge(e.shape));
        if (c.GetType() == GeomAbs_Line)
            return c.Line();
    }
    return {};
}
std::optional<gp_Lin> cylinderAxis(const MeasureEntity& e)
{
    if (!e.shape.IsNull() && e.shape.ShapeType() == TopAbs_FACE)
    {
        BRepAdaptor_Surface s(TopoDS::Face(e.shape));
        if (s.GetType() == GeomAbs_Cylinder)
            return gp_Lin(s.Cylinder().Axis());
    }
    return {};
}
std::optional<gp_Circ> circleOf(const MeasureEntity& e, bool full = false)
{
    if (!e.shape.IsNull() && e.shape.ShapeType() == TopAbs_EDGE)
    {
        BRepAdaptor_Curve c(TopoDS::Edge(e.shape));
        if (c.GetType() == GeomAbs_Circle &&
            (!full || std::abs(c.LastParameter() - c.FirstParameter() - 2 * pi) < 1e-9))
            return c.Circle();
    }
    return {};
}
double edgeLength(const TopoDS_Shape& edge)
{
    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);
    return props.Mass();
}
std::optional<Measurement> single(const MeasureEntity& e)
{
    Measurement r;
    if (e.plane || e.axis)
    {
        r.anchor = e.plane ? e.plane->Location() : e.axis->Location();
        r.direction = e.plane ? e.plane->Axis().Direction() : e.axis->Direction();
        return r;
    }
    const auto& s = e.shape;
    if (s.IsNull())
        return {};
    if (s.ShapeType() == TopAbs_VERTEX)
    {
        r.coordinates = BRep_Tool::Pnt(TopoDS::Vertex(s));
        r.anchor = r.coordinates;
        return r;
    }
    if (s.ShapeType() == TopAbs_EDGE)
    {
        if (BRep_Tool::Degenerated(TopoDS::Edge(s)))
            return {};
        BRepAdaptor_Curve c(TopoDS::Edge(s));
        r.length = edgeLength(s);
        if (!std::isfinite(*r.length) || *r.length <= Precision::Confusion())
            return {};
        r.anchor = c.Value((c.FirstParameter() + c.LastParameter()) * .5);
        if (auto l = lineOf(e))
            r.direction = l->Direction();
        if (auto ring = circleOf(e))
        {
            r.center = ring->Location();
            r.radius = ring->Radius();
            r.circumference = 2 * pi * *r.radius;
            r.sweepDegrees = (c.LastParameter() - c.FirstParameter()) * 180 / pi;
            r.diameterSpan = MeasureSpan{*r.center, *r.anchor};
            if (circleOf(e, true))
                r.diameterSpan->first = r.anchor->Mirrored(*r.center);
        }
        return r;
    }
    if (s.ShapeType() != TopAbs_FACE && s.ShapeType() != TopAbs_SOLID &&
        s.ShapeType() != TopAbs_COMPOUND)
        return {};
    if (s.ShapeType() == TopAbs_COMPOUND)
    {
        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(s, TopAbs_SOLID, solids);
        if (solids.IsEmpty())
            return {};
    }
    GProp_GProps props;
    BRepGProp::SurfaceProperties(s, props, 1e-10);
    r.area = props.Mass();
    if (!std::isfinite(*r.area) || *r.area <= 0)
        return {};
    r.anchor = props.CentreOfMass();
    if (s.ShapeType() == TopAbs_FACE)
    {
        const auto face = TopoDS::Face(s);
        BRepAdaptor_Surface surf(face);
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(s, TopAbs_EDGE, edges);
        double perimeter = 0;
        for (int i = 1; i <= edges.Extent(); ++i)
        {
            auto edge = TopoDS::Edge(edges(i));
            if (!BRep_Tool::IsClosed(edge, face) && !BRep_Tool::Degenerated(edge))
                perimeter += edgeLength(edge);
        }
        r.perimeter = perimeter;
        if (auto plane = planeOf(e))
        {
            r.planarFace = true;
            r.direction = plane->Axis().Direction();
        }
        if (auto axis = cylinderAxis(e))
        {
            r.radius = surf.Cylinder().Radius();
            r.direction = axis->Direction();
            r.center = onLine(*r.anchor, *axis);
        }
    }
    return r;
}
// Clip an infinite support to a domain containing the orthogonal projection of
// every point of the finite selection. Bounds only limit the OCCT query.
TopoDS_Shape bounded(const MeasureEntity& support, const TopoDS_Shape& other)
{
    if (!support.shape.IsNull())
        return support.shape;
    Bnd_Box box;
    BRepBndLib::AddOptimal(other, box, false, true);
    if (box.IsVoid() || box.IsOpen())
        return {};
    const gp_Pnt c = middle({box.CornerMin(), box.CornerMax()});
    const double h = box.CornerMin().Distance(box.CornerMax()) + 1.;
    if (support.plane)
    {
        auto p = *support.plane;
        p.SetLocation(onPlane(c, p));
        return BRepBuilderAPI_MakeFace(p, -h, h, -h, h).Face();
    }
    const auto p = onLine(c, *support.axis);
    const gp_Vec v(support.axis->Direction());
    return BRepBuilderAPI_MakeEdge(p.Translated(-v * h), p.Translated(v * h)).Edge();
}
MeasureSpan infiniteSpan(const MeasureEntity& a, const MeasureEntity& b)
{
    if (a.axis && b.axis)
        return axesSpan(*a.axis, *b.axis);
    if (a.plane && b.plane)
    {
        if (a.plane->Axis().Direction().IsParallel(b.plane->Axis().Direction(), 1e-12))
            return {a.plane->Location(), onPlane(a.plane->Location(), *b.plane)};
        IntAna_QuadQuadGeo cross(*a.plane, *b.plane, 1e-12, Precision::Confusion());
        auto p = cross.Line(1).Location();
        return {p, p};
    }
    const auto& line = a.axis ? *a.axis : *b.axis;
    const auto& plane = a.plane ? *a.plane : *b.plane;
    auto p = line.Location();
    auto q = onPlane(p, plane);
    const gp_Vec n(plane.Axis().Direction()), v(line.Direction());
    if (std::abs(n.Dot(v)) > 1e-12)
    {
        p.Translate(v * (gp_Vec(p, q).Dot(n) / v.Dot(n)));
        q = p;
    }
    return a.axis ? MeasureSpan{p, q} : MeasureSpan{q, p};
}
std::optional<MeasureSpan> closest(const MeasureEntity& a, const MeasureEntity& b)
{
    if (a.shape.IsNull() && b.shape.IsNull())
        return infiniteSpan(a, b);
    auto first = bounded(a, b.shape), second = bounded(b, a.shape);
    if (first.IsNull() || second.IsNull())
        return {};
    BRepExtrema_DistShapeShape d(first, second);
    if (!d.IsDone() || d.NbSolution() < 1)
        return {};
    // A solid contains a selected interior point. The extrema tool can return
    // zero for containment even when boundary witnesses are on different faces.
    if (std::abs(d.PointOnShape1(1).Distance(d.PointOnShape2(1)) - d.Value()) >
        Precision::Confusion())
        return {};
    return MeasureSpan{d.PointOnShape1(1), d.PointOnShape2(1)};
}
} // namespace

std::optional<Measurement> measureEntities(const std::vector<MeasureEntity>& es)
{
    if (es.empty())
        return {};
    try
    {
        for (const auto& e : es)
            if (e.shape.IsNull() && !e.axis && !e.plane)
                return {};
        if (es.size() == 1)
            return single(es.front());
        Measurement r;
        bool edges = true, faces = true;
        double total = 0;
        for (const auto& e : es)
        {
            edges &= !e.shape.IsNull() && e.shape.ShapeType() == TopAbs_EDGE;
            faces &= !e.shape.IsNull() && e.shape.ShapeType() == TopAbs_FACE;
        }
        if (edges || faces)
        {
            for (const auto& e : es)
            {
                const auto v = single(e);
                if (!v)
                    return {};
                total += edges ? *v->length : *v->area;
                r.anchor = v->anchor;
            }
            if (edges)
                r.totalLength = total;
            else
                r.totalArea = total;
        }
        if (es.size() > 2)
            return edges || faces ? std::optional<Measurement>(r) : std::nullopt;
        const auto& a = es.front();
        const auto& b = es.back();
        r.span = closest(a, b);
        if (!r.span)
            return {};
        r.distance = r.span->first.Distance(r.span->second);
        r.anchor = middle(*r.span);
        auto p = planeOf(a), q = planeOf(b);
        auto l = lineOf(a), m = lineOf(b);
        auto ca = cylinderAxis(a), cb = cylinderAxis(b);
        auto direction = [](const auto& plane, const auto& line,
                            const auto& cylinder) -> std::optional<gp_Dir>
        {
            if (plane)
                return plane->Axis().Direction();
            if (line)
                return line->Direction();
            if (cylinder)
                return cylinder->Direction();
            return {};
        };
        auto u = direction(p, l, ca), v = direction(q, m, cb);
        if (u && v)
        {
            double angle = u->Angle(*v);
            if (angle > pi / 2)
            {
                angle = pi - angle;
                v->Reverse();
            }
            if (bool(p) != bool(q))
            {
                angle = pi / 2 - angle;
                auto& normal = p ? *u : *v;
                const auto other = p ? *v : *u;
                gp_Vec tangent = gp_Vec(other) - gp_Vec(normal) * normal.Dot(other);
                if (tangent.Magnitude() > 1e-12)
                    normal = gp_Dir(tangent);
                else
                    normal = (p ? *p : *q).XAxis().Direction();
            }
            r.angleDegrees = angle * 180 / pi;
            r.angleDirections = std::pair{*u, *v};
        }
        if (p && q && p->Axis().Direction().IsParallel(q->Axis().Direction(), 1e-12))
            r.normalSpan = MeasureSpan{r.span->first, onPlane(r.span->first, *q)};
        else if (p || q)
        {
            const auto plane = p ? *p : *q;
            const auto witness = p ? r.span->second : r.span->first;
            const auto straight = p ? m : l;
            const auto cyl = p ? cb : ca;
            if ((straight && straight->Direction().IsNormal(plane.Axis().Direction(), 1e-12)) ||
                (cyl && cyl->Direction().IsNormal(plane.Axis().Direction(), 1e-12)) ||
                (!(p ? b : a).shape.IsNull() && (p ? b : a).shape.ShapeType() == TopAbs_VERTEX))
                r.normalSpan = MeasureSpan{witness, onPlane(witness, plane)};
        }
        if (r.normalSpan)
            r.normalDistance = r.normalSpan->first.Distance(r.normalSpan->second);
        auto ring = circleOf(a), other = circleOf(b);
        if (ring && other)
        {
            r.centerSpan = MeasureSpan{ring->Location(), other->Location()};
        }
        else if (ca && cb)
            r.centerSpan = axesSpan(*ca, *cb);
        else if (ring || other || ca || cb)
        {
            MeasureEntity aa = a, bb = b;
            if (ring)
                aa = {BRepBuilderAPI_MakeVertex(ring->Location()).Vertex()};
            if (other)
                bb = {BRepBuilderAPI_MakeVertex(other->Location()).Vertex()};
            if (ca)
                aa = {{}, {}, ca};
            if (cb)
                bb = {{}, {}, cb};
            r.centerSpan = closest(aa, bb);
        }
        if (r.centerSpan)
        {
            r.centerDistance = r.centerSpan->first.Distance(r.centerSpan->second);
            measureCircularConditions(a, b, r);
        }
        return r;
    }
    catch (const Standard_Failure&)
    {
        return {};
    }
}
std::optional<Measurement> measureGeometry(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    return b.IsNull() ? measureEntities({{a}}) : measureEntities({{a}, {b}});
}
std::optional<MeasureSpan> circularSpan(const Measurement& r, ArcCondition a, ArcCondition b)
{
    if (a == ArcCondition::Center && b == ArcCondition::Center)
        return r.centerSpan;
    if (a == ArcCondition::Near && b == ArcCondition::Near)
        return r.span;
    return r.arcConditions[3 * int(a) + int(b)];
}
gp_XYZ measureComponents(const MeasureSpan& s, const gp_Ax3& f)
{
    const gp_Vec v(s.first, s.second);
    return {v.Dot(gp_Vec(f.XDirection())), v.Dot(gp_Vec(f.YDirection())),
            v.Dot(gp_Vec(f.Direction()))};
}
gp_XYZ measureCoordinates(const gp_Pnt& p, const gp_Ax3& f)
{
    return measureComponents({f.Location(), p}, f);
}
MeasureProjection projectMeasurement(const MeasureSpan& s, const gp_Pln& plane)
{
    const auto a = onPlane(s.first, plane), b = onPlane(s.second, plane);
    return {{a, b}, {b, s.second.Translated(gp_Vec(s.first, a))}};
}
} // namespace thecad::modeling
