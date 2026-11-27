#pragma once
#include "modeling/Sketch.h"
#include <Geom_BSplineCurve.hxx>
#include <TopoDS_Edge.hxx>

namespace thecad::modeling {
// Native interpolating B-splines; no polyline approximation is stored.
SketchEntity interpolatingSpline(const std::vector<Point2>& points, const std::string& id = {});
std::vector<Point2> splineFitPoints(const SketchEntity& entity);
Handle(Geom_BSplineCurve) sketchSplineCurve(const Sketch& sketch, const SketchEntity& entity);
TopoDS_Edge sketchSplineEdge(const Sketch& sketch, const SketchEntity& entity);
std::vector<Point2> splinePickPolyline(const SketchEntity& entity, double deflection);
}
