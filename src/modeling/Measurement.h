#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>
#include <gp_Circ.hxx>
#include <array>
#include <gp_Pln.hxx>
#include <optional>
#include <utility>
#include <vector>

namespace thecad::modeling
{
using MeasureSpan = std::pair<gp_Pnt, gp_Pnt>;

// A datum is mathematically unbounded. Its viewport rectangle or line is never measured.
struct MeasureEntity
{
    TopoDS_Shape shape;
    std::optional<gp_Pln> plane;
    std::optional<gp_Lin> axis;
};

enum class ArcCondition
{
    Center,
    Near,
    Far
};
struct Measurement
{
    std::optional<double> length, area, radius, distance, angleDegrees, centerDistance;
    std::optional<double> perimeter, circumference, sweepDegrees, totalLength, totalArea,
        normalDistance;
    std::optional<gp_Pnt> coordinates, anchor, center;
    std::optional<gp_Dir> direction;
    std::optional<MeasureSpan> span, centerSpan, diameterSpan, normalSpan;
    std::optional<std::pair<gp_Dir, gp_Dir>> angleDirections;
    std::array<std::optional<MeasureSpan>, 9> arcConditions;
    bool customArcConditions = false, radialCylinderSpan = false;
    bool planarFace = false;
};

std::optional<Measurement> measureEntities(const std::vector<MeasureEntity>& entities);
std::optional<Measurement> measureGeometry(const TopoDS_Shape& first,
                                           const TopoDS_Shape& second = {});
std::optional<MeasureSpan> circularSpan(const Measurement&, ArcCondition first,
                                        ArcCondition second);
gp_XYZ measureCoordinates(const gp_Pnt&, const gp_Ax3&);
gp_XYZ measureComponents(const MeasureSpan&, const gp_Ax3&);
struct MeasureProjection
{
    MeasureSpan inPlane, normal;
};
MeasureProjection projectMeasurement(const MeasureSpan&, const gp_Pln&);
std::optional<double> projectedMeasureArea(const TopoDS_Shape&, const gp_Pln&);
} // namespace thecad::modeling
