#include "modeling/MeasurementCircular.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <Extrema_ExtCC.hxx>
#include <Extrema_POnCurv.hxx>
#include <ElCLib.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::modeling
{
namespace
{
struct Arc
{
    gp_Circ circle;
    double start, end;
    gp_Pnt at(double u) const { return ElCLib::Value(u, circle); }
    gp_Pnt side(const gp_Pnt& reference, bool farthest) const
    {
        gp_Vec d(circle.Location(), reference), n(circle.Axis().Direction());
        d -= n * d.Dot(n);
        if (d.Magnitude() <= Precision::Confusion())
            return at(start);
        d.Normalize();
        if (farthest)
            d.Reverse();
        auto radial = circle.Location().Translated(d * circle.Radius());
        double period = 2 * std::acos(-1.);
        auto u = ElCLib::InPeriod(ElCLib::Parameter(circle, radial), start, start + period);
        if (u <= end + Precision::PConfusion())
            return at(std::clamp(u, start, end));
        auto p = at(start), q = at(end);
        return (p.SquareDistance(reference) > q.SquareDistance(reference)) == farthest ? p : q;
    }
};
std::optional<Arc> arc(const TopoDS_Shape& s)
{
    if (s.IsNull() || s.ShapeType() != TopAbs_EDGE)
        return {};
    BRepAdaptor_Curve c(TopoDS::Edge(s));
    if (c.GetType() != GeomAbs_Circle)
        return {};
    return Arc{c.Circle(), c.FirstParameter(), c.LastParameter()};
}
void pairConditions(const Arc& x, const Arc& y, Measurement& r)
{
    gp_Pnt a = x.circle.Location(), b = y.circle.Location();
    auto side = [](const Arc& a, const gp_Pnt& other, int i)
    { return i ? a.side(other, i == 2) : a.circle.Location(); };
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.arcConditions[3 * i + j] = MeasureSpan{side(x, b, i), side(y, a, j)};
    r.customArcConditions = true;
}
std::optional<MeasureSpan> maximumArcs(const Arc& x, const Arc& y, const TopoDS_Shape& first,
                                       const TopoDS_Shape& second)
{
    MeasureSpan best{x.at(x.start), y.at(y.start)};
    auto candidate = [&](gp_Pnt a, gp_Pnt b)
    {
        if (a.SquareDistance(b) > best.first.SquareDistance(best.second))
            best = {a, b};
    };
    for (double u : {x.start, x.end})
    {
        auto a = x.at(u);
        candidate(a, y.side(a, true));
    }
    for (double v : {y.start, y.end})
    {
        auto b = y.at(v);
        candidate(x.side(b, true), b);
    }
    if (x.circle.Axis().Direction().IsParallel(y.circle.Axis().Direction(), 1e-12))
    {
        for (bool farthest : {false, true})
        {
            auto a = x.side(y.circle.Location(), farthest);
            candidate(a, y.side(a, true));
            auto b = y.side(x.circle.Location(), farthest);
            candidate(x.side(b, true), b);
        }
    }
    else
    {
        BRepAdaptor_Curve a(TopoDS::Edge(first)), b(TopoDS::Edge(second));
        Extrema_ExtCC extrema(a, b, x.start, x.end, y.start, y.end);
        if (!extrema.IsDone() || extrema.IsParallel())
            return {};
        for (int i = 1; i <= extrema.NbExt(); ++i)
        {
            Extrema_POnCurv p, q;
            extrema.Points(i, p, q);
            candidate(p.Value(), q.Value());
        }
    }
    return best;
}
} // namespace
void measureCircularConditions(const MeasureEntity& a, const MeasureEntity& b, Measurement& r)
{
    try
    {
        auto x = arc(a.shape), y = arc(b.shape);
        if (x && y)
        {
            pairConditions(*x, *y, r);
            r.arcConditions[8] = maximumArcs(*x, *y, a.shape, b.shape);
            return;
        }
        if (x || y)
        {
            const auto& other = x ? b.shape : a.shape;
            if (!other.IsNull() && other.ShapeType() == TopAbs_VERTEX)
            {
                auto p = BRep_Tool::Pnt(TopoDS::Vertex(other));
                auto q = (x ? *x : *y).side(p, true);
                r.arcConditions[8] = x ? MeasureSpan{q, p} : MeasureSpan{p, q};
            }
            return;
        }
        if (a.shape.IsNull() || b.shape.IsNull() || a.shape.ShapeType() != TopAbs_FACE ||
            b.shape.ShapeType() != TopAbs_FACE)
            return;
        auto fa = TopoDS::Face(a.shape), fb = TopoDS::Face(b.shape);
        BRepAdaptor_Surface s(fa), t(fb);
        if (s.GetType() != GeomAbs_Cylinder || t.GetType() != GeomAbs_Cylinder)
            return;
        auto ca = s.Cylinder(), cb = t.Cylinder();
        auto dir = ca.Axis().Direction();
        if (!dir.IsParallel(cb.Axis().Direction(), 1e-12))
            return;
        auto axial = [&](const gp_Pnt& p) { return gp_Vec(ca.Location(), p).Dot(gp_Vec(dir)); };
        auto v0 = axial(t.Value(0, t.FirstVParameter())),
             v1 = axial(t.Value(0, t.LastVParameter()));
        auto lo = std::max(s.FirstVParameter(), std::min(v0, v1)),
             hi = std::min(s.LastVParameter(), std::max(v0, v1));
        if (lo > hi)
            return;
        auto p = ca.Location().Translated(gp_Vec(dir) * ((hi + lo) * .5));
        auto q = cb.Location().Translated(gp_Vec(dir) * gp_Vec(cb.Location(), p).Dot(gp_Vec(dir)));
        const double period = 2 * std::acos(-1.);
        Arc aa{gp_Circ(gp_Ax2(p, dir, ca.XAxis().Direction()), ca.Radius()), 0, period};
        Arc bb{gp_Circ(gp_Ax2(q, dir, cb.XAxis().Direction()), cb.Radius()), 0, period};
        pairConditions(aa, bb, r);
        // On coaxial rings any radial direction works. Use opposing witnesses for the far
        // condition.
        if (p.Distance(q) <= Precision::Confusion())
            r.arcConditions[8] = MeasureSpan{aa.at(0), bb.side(aa.at(0), true)};
        for (int i = 0; i < 9; ++i)
        {
            const auto& span = *r.arcConditions[i];
            auto inside = [](const TopoDS_Face& face, gp_Pnt point)
            {
                auto state = BRepClass_FaceClassifier(face, point, Precision::Confusion()).State();
                return state == TopAbs_IN || state == TopAbs_ON;
            };
            if ((i / 3 && !inside(fa, span.first)) || (i % 3 && !inside(fb, span.second)))
                r.arcConditions[i].reset();
        }
        r.customArcConditions = std::all_of(r.arcConditions.begin(), r.arcConditions.end(),
                                            [](const auto& s) { return bool(s); });
        r.radialCylinderSpan = true;
    }
    catch (const Standard_Failure&)
    {
        r.arcConditions = {};
        r.customArcConditions = false;
    }
}
} // namespace thecad::modeling
