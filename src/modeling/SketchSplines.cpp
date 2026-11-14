#include "modeling/SketchSplines.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace thecad::modeling {
std::vector<Point2> splineFitPoints(const SketchEntity& entity)
{
    std::vector<Point2> points{entity.first};
    points.insert(points.end(),entity.splinePoints.begin(),entity.splinePoints.end());
    points.push_back(entity.second); return points;
}
SketchEntity interpolatingSpline(const std::vector<Point2>& points, const std::string& id)
{
    if (points.size()<2 || points.size()>128) throw std::runtime_error("A spline needs 2 to 128 fit points.");
    SketchEntity entity; entity.kind=EntityKind::Spline; entity.id=id;
    entity.first=points.front(); entity.second=points.back();
    entity.splinePoints.assign(points.begin()+1,points.end()-1);
    (void)sketchSplineCurve(Sketch{},entity); return entity;
}
Handle(Geom_BSplineCurve) sketchSplineCurve(const Sketch& sketch, const SketchEntity& entity)
{
    if (entity.kind!=EntityKind::Spline || entity.splinePoints.size()>126)
        throw std::runtime_error("Invalid spline fit-point data.");
    auto points=splineFitPoints(entity);
    for (std::size_t i=0;i<points.size();++i) {
        const auto p=points[i];
        if (!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e9||std::abs(p.y)>1e9)
            throw std::runtime_error("Spline points must be finite and within 1e9 mm.");
        if (i && std::hypot(p.x-points[i-1].x,p.y-points[i-1].y)<=1e-6)
            throw std::runtime_error("Consecutive spline points must be distinct.");
    }
    const bool closed=std::hypot(points.front().x-points.back().x,points.front().y-points.back().y)<=1e-6;
    if (closed) points.pop_back();
    if (points.size()<(closed?3u:2u)) throw std::runtime_error("A closed spline needs at least three distinct fit points.");
    try {
        Handle(TColgp_HArray1OfPnt) fit=new TColgp_HArray1OfPnt(1,int(points.size()));
        for (int i=1;i<=int(points.size());++i) fit->SetValue(i,sketchPoint(sketch,points[std::size_t(i-1)]));
        GeomAPI_Interpolate interpolation(fit,closed,1e-7); interpolation.Perform();
        if (!interpolation.IsDone()) throw std::runtime_error("The spline could not be interpolated.");
        return interpolation.Curve();
    } catch (const Standard_Failure& error) {
        throw std::runtime_error(std::string("Cannot create spline: ")+error.GetMessageString());
    }
}
TopoDS_Edge sketchSplineEdge(const Sketch& sketch, const SketchEntity& entity)
{
    return BRepBuilderAPI_MakeEdge(sketchSplineCurve(sketch,entity)).Edge();
}
std::vector<Point2> splinePickPolyline(const SketchEntity& entity, double deflection)
{
    GeomAdaptor_Curve curve(sketchSplineCurve(Sketch{},entity));
    GCPnts_QuasiUniformDeflection sample(curve,std::max(1e-7,deflection));
    if (!sample.IsDone() || sample.NbPoints()>20000) return {};
    std::vector<Point2> points; points.reserve(sample.NbPoints());
    for (int i=1;i<=sample.NbPoints();++i) {const auto p=sample.Value(i); points.push_back({p.X(),p.Y()});}
    return points;
}
}
