#pragma once

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <string>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>
#include <vector>
#include <utility>
#include <optional>

namespace thecad::modeling {

enum class SketchPlane { XY, XZ, YZ };
struct Point2 { double x = 0.0; double y = 0.0; };
enum class EntityKind { Line, Rectangle, Circle, Point, Polygon }; // Polygon is a creation tool; stored boundaries are lines.
struct SketchEntity {
    EntityKind kind = EntityKind::Line;
    Point2 first;
    Point2 second;
    double radius = 1.0;
    std::string id;
    bool construction = false;
};
enum class PointRole { Origin, First, Second, Center, Midpoint }; // Midpoint is a derived snap target, never serialized as a point reference.
struct SketchPointRef {
    std::string entityId;
    PointRole role = PointRole::Origin;
};
enum class RelationKind { Coincident, Horizontal, Vertical, FixedPoint, Parallel, Perpendicular, Tangent, PointOnLine, PointOnCircle, Collinear, Equal, Concentric, EqualCurveLength, FixedEntity, Midpoint };
struct SketchRelation {
    std::string id;
    RelationKind kind = RelationKind::Coincident;
    SketchPointRef first;
    SketchPointRef second;
    Point2 fixedPosition; // Used only by FixedPoint; independent of solver storage.
    Point2 fixedSecond; // FixedEntity captures line endpoints or circle center/radius.
    double fixedRadius=1;
    std::string groupId; // One Equal command may own several pairwise solver equations.
};
enum class SketchSpace { Planar2D, Spatial3D }; // Spatial3D is reserved, not implemented.
enum class DimensionKind { LineLength, HorizontalDistance, VerticalDistance, CircleRadius, CircleDiameter, PointDistance, LineDistance, LineAngle };
struct SketchDimension {
    std::string id;
    DimensionKind kind = DimensionKind::LineLength;
    SketchPointRef first, second;
    double value = 1.0;
    bool driving = true;
    Point2 annotation; // Absolute placement for legacy import and initial attachment.
    int sense = 1; // Preserves the current side for unsigned H/V distances.
    std::optional<Point2> placement; // Coordinates in the current reference frame; legacy annotation remains readable.
};
struct FaceAttachment {
    std::string sourceFeature;
    int faceIndex = 0; // One-based OCCT face map in the strictly validated source body.
    std::string sourceSignature;
    gp_Ax3 frame;
};
struct Sketch {
    SketchPlane plane = SketchPlane::XY;
    std::vector<SketchEntity> entities;
    std::vector<SketchRelation> relations;
    std::optional<FaceAttachment> attachment;
    std::vector<SketchDimension> dimensions;
    SketchSpace space = SketchSpace::Planar2D;
    struct RegularPolygon { std::string id,centerId; std::vector<std::string> edges; };
    std::vector<RegularPolygon> polygons;
};
// Point references address the immutable local origin or stable entity IDs.
Point2 sketchPointValue(const Sketch& sketch, const SketchPointRef& reference);
std::vector<std::pair<SketchPointRef, Point2>> sketchPoints(const Sketch& sketch);
void validateSketchConstraintReferences(const Sketch& sketch);
void validateSketchRelations(const Sketch& sketch);
double dimensionMeasurement(const Sketch& sketch,const SketchDimension& dimension);
Sketch canonicalizeSketchEqualities(const Sketch& sketch,const std::vector<std::pair<SketchPointRef,Point2>>& anchors = {});
// Edits are transactional. Dimensioned/nonlinear sketches use the solver adapter;
// legacy coordinate equalities retain exact propagation.
Sketch editSketchEntity(const Sketch& sketch, const SketchEntity& replacement);
std::vector<SketchPointRef> coincidentSketchPoints(const Sketch& sketch, const SketchPointRef& reference);
bool areSketchPointsCoincident(const Sketch& sketch, const SketchPointRef& first, const SketchPointRef& second);
// Move one point through the constraint system; optionally join a stable
// target reference. Returns a validated candidate and never mutates the source.
Sketch moveSketchPoint(const Sketch& sketch, const SketchPointRef& reference, Point2 position,
                      const std::optional<SketchPointRef>& target = {}, bool preserveCircleRadius = false);
Sketch removeSketchEntity(const Sketch& sketch, const std::string& entityId);

enum class Direction { Positive, Negative };
// Region boundaries retain whole-entity IDs where possible, and versioned
// source/intersection fragment references for subdivided sketch geometry.
using RegionReferences = std::vector<std::vector<std::string>>;
struct Contour {
    std::vector<std::string> entityIds; // Persistent boundary references, including derived fragments.
    TopoDS_Face face;
};

// Right-handed planes: XY=(X,Y,+Z), XZ=(X,Z,-Y), YZ=(Y,Z,+X).
gp_Ax3 sketchAxes(SketchPlane plane);
gp_Ax3 sketchAxes(const Sketch& sketch);
gp_Ax3 planarFaceAxes(const TopoDS_Face& face);
gp_Pnt sketchPoint(const Sketch& sketch, Point2 point);
gp_Pnt sketchPoint(SketchPlane plane, Point2 point);

// Open geometry is displayable. Extrusion requires one simple, closed profile.
TopoDS_Shape sketchShape(const Sketch& sketch);
void validateClosedProfile(const Sketch& sketch);
std::vector<Contour> closedContours(const Sketch& sketch);
Sketch selectContour(const Sketch& sketch, const std::vector<std::string>& entityIds);
bool contourContains(const Contour& contour, SketchPlane plane, Point2 point);
bool contourContains(const Contour& contour, const Sketch& sketch, Point2 point);
TopoDS_Shape makeExtrusion(const Sketch& sketch, double length, Direction direction,
                          const TopoDS_Shape& base = {});
TopoDS_Shape makeExtrusions(const Sketch& sketch,const RegionReferences& regions,double length,Direction direction,const TopoDS_Shape& base = {});
TopoDS_Shape makeCuts(const TopoDS_Shape& base,const Sketch& sketch,const RegionReferences& regions,double depth,bool throughAll,Direction direction);
TopoDS_Shape makeCut(const TopoDS_Shape& base, const Sketch& sketch, double depth,
                    bool throughAll, Direction direction);

} // namespace thecad::modeling
