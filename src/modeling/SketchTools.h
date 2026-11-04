#pragma once
#include "modeling/Sketch.h"
namespace thecad::modeling {
struct PolygonParameters { Point2 center; double radius=10,angle=0; int sides=6; }; // Angle in radians; circumradius.
Sketch addRegularPolygon(const Sketch&,const std::string& id,PolygonParameters);
Sketch editRegularPolygon(const Sketch&,const std::string& id,PolygonParameters);
PolygonParameters polygonParameters(const Sketch&,const Sketch::RegularPolygon&);
const Sketch::RegularPolygon* polygonForEntity(const Sketch&,const std::string& entityId);
void validatePolygons(const Sketch&,bool geometry);
}
