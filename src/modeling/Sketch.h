#pragma once

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <string>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <vector>

namespace thecad::modeling {

enum class SketchPlane { XY, XZ, YZ };
struct Point2 { double x = 0.0; double y = 0.0; };
enum class EntityKind { Line, Rectangle, Circle };
struct SketchEntity {
    EntityKind kind = EntityKind::Line;
    Point2 first;
    Point2 second;
    double radius = 1.0;
    std::string id;
};
struct Sketch {
    SketchPlane plane = SketchPlane::XY;
    std::vector<SketchEntity> entities;
};
enum class Direction { Positive, Negative };
struct Contour {
    std::vector<std::string> entityIds;
    TopoDS_Face face;
};

// Right-handed planes: XY=(X,Y,+Z), XZ=(X,Z,-Y), YZ=(Y,Z,+X).
gp_Ax3 sketchAxes(SketchPlane plane);
gp_Pnt sketchPoint(SketchPlane plane, Point2 point);

// Open geometry is displayable. Extrusion requires one simple, closed profile.
TopoDS_Shape sketchShape(const Sketch& sketch);
void validateClosedProfile(const Sketch& sketch);
std::vector<Contour> closedContours(const Sketch& sketch);
Sketch selectContour(const Sketch& sketch, const std::vector<std::string>& entityIds);
bool contourContains(const Contour& contour, SketchPlane plane, Point2 point);
TopoDS_Shape makeExtrusion(const Sketch& sketch, double length, Direction direction,
                          const TopoDS_Shape& base = {});
TopoDS_Shape makeCut(const TopoDS_Shape& base, const Sketch& sketch, double depth,
                    bool throughAll, Direction direction);

} // namespace thecad::modeling
