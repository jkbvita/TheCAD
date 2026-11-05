#pragma once
#include "modeling/Sketch.h"
#include <gp_Vec.hxx>
#include <vector>
namespace thecad::modeling {
enum class DatumKind { Point, Axis, Plane };
enum class DatumMethod { PointXYZ, PointOnReference, PointAxisPlane, PointTwoAxes, PointProjection,
    AxisXYZ, AxisOnReference, AxisTwoPoints, AxisTwoPlanes, AxisNormalToPlane,
    PlaneXYZ, PlaneOffset, PlaneThreePoints, PlaneAtAngle, PlaneThroughPoint };
enum class ReferenceSource { Origin=0, BasePlane=1, BaseAxis=2, Datum=3, SketchPoint=4, SketchLine=5, BodyVertex=6, BodyEdge=7, BodyFace=8, BodyCircleCenter=9 };
struct GeometryReference {
    ReferenceSource source=ReferenceSource::Origin;
    std::string featureId,entityId,signature;
    PointRole role=PointRole::First;
    int index=0; // Base-plane/axis index, or one-based conservative topology index.
};
struct DatumGeometry { DatumKind kind=DatumKind::Point; gp_Ax3 frame; };
struct DatumDefinition {
    DatumMethod method=DatumMethod::PointXYZ;
    std::vector<GeometryReference> references;
    gp_Pnt position;
    gp_Vec direction{0,0,1};
    double value=0; // Signed offset in mm, or rotation in degrees.
};
DatumKind datumKind(DatumMethod method);
const char* datumMethodName(DatumMethod method);
const char* datumMethodKey(DatumMethod method);
DatumMethod datumMethodFromKey(const std::string& key);
std::vector<DatumKind> datumReferenceKinds(DatumMethod method);
DatumGeometry evaluateDatum(const DatumDefinition&,const std::vector<DatumGeometry>& references);
TopoDS_Shape datumDisplayShape(const DatumGeometry&,double halfExtent);
}
