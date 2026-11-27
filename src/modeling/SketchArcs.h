#pragma once

#include "modeling/Sketch.h"
#include <array>
#include <TopoDS_Edge.hxx>

namespace thecad::modeling
{
struct ArcAngles
{
    double start;
    double sweep; // Signed radians.
};

ArcAngles arcAngles(const SketchEntity& arc);
Point2 arcPoint(const SketchEntity& arc, double fraction);
Point2 closestArcPoint(const SketchEntity& arc, Point2 target);
TopoDS_Edge sketchArcEdge(const Sketch& sketch, const SketchEntity& arc);
SketchEntity threePointArc(Point2 first, Point2 second, Point2 through, const std::string& id);
std::array<SketchEntity, 4> straightSlot(Point2 firstCenter, Point2 secondCenter, Point2 widthPick,
                                        const std::string& id);
void addStraightSlot(Sketch& sketch, const std::array<SketchEntity, 4>& outline, const std::string& id);
}
